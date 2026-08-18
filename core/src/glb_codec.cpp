#include "octopoly/glb_codec.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace octopoly::glb {

struct MeshGlbAccess {
    static Mesh make(std::vector<Vertex> vertices, std::vector<Face> faces) {
        Mesh mesh;
        mesh.vertices_ = std::move(vertices);
        mesh.faces_ = std::move(faces);
        mesh.nextVertexId_ = static_cast<VertexId>(mesh.vertices_.size()) + 1U;
        mesh.nextFaceId_ = static_cast<FaceId>(mesh.faces_.size()) + 1U;
        mesh.revision_ = 0;
        mesh.rebuildVertexLookup();
        return mesh;
    }
};

namespace {

constexpr std::uint32_t kGlbMagic = 0x46546c67U;
constexpr std::uint32_t kJsonChunk = 0x4e4f534aU;
constexpr std::uint32_t kBinChunk = 0x004e4942U;
constexpr std::uint32_t kTriangles = 4U;
constexpr std::uint32_t kFloat = 5126U;
constexpr std::uint32_t kUnsignedByte = 5121U;
constexpr std::uint32_t kUnsignedShort = 5123U;
constexpr std::uint32_t kUnsignedInt = 5125U;

bool checkedAdd(std::size_t first, std::size_t second, std::size_t& result) noexcept {
    if (second > std::numeric_limits<std::size_t>::max() - first) {
        return false;
    }
    result = first + second;
    return true;
}

bool checkedMultiply(std::size_t first, std::size_t second, std::size_t& result) noexcept {
    if (first != 0U && second > std::numeric_limits<std::size_t>::max() / first) {
        return false;
    }
    result = first * second;
    return true;
}

std::size_t align4(std::size_t value) noexcept {
    return (value + 3U) & ~std::size_t{3U};
}

void appendU16(std::vector<std::uint8_t>& output, std::uint16_t value) {
    output.push_back(static_cast<std::uint8_t>(value & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
}

void appendU32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    for (unsigned byte = 0; byte < 4U; ++byte) {
        output.push_back(static_cast<std::uint8_t>((value >> (byte * 8U)) & 0xffU));
    }
}

std::uint16_t readU16(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(bytes[offset]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U);
}

std::uint32_t readU32(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
    std::uint32_t value = 0;
    for (unsigned byte = 0; byte < 4U; ++byte) {
        value |= static_cast<std::uint32_t>(bytes[offset + byte]) << (byte * 8U);
    }
    return value;
}

float readFloat(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
    return std::bit_cast<float>(readU32(bytes, offset));
}

void appendFloat(std::vector<std::uint8_t>& output, float value) {
    appendU32(output, std::bit_cast<std::uint32_t>(value));
}

EncodeResult encodeFailure(ErrorCategory category, EncodeErrorCode code,
                           std::string_view message) noexcept {
    return {false, {}, {category, code, 0, message}};
}

DecodeResult decodeFailure(ErrorCategory category, DecodeErrorCode code,
                           std::size_t offset, std::string_view message) noexcept {
    return {false, {}, {}, {category, code, offset, message}};
}

struct JsonValue {
    enum class Kind { object, array, string, number, boolean, nullValue };
    Kind kind{Kind::nullValue};
    std::size_t offset{};
    std::string stringValue;
    double numberValue{};
    std::string_view numberLexeme;
    bool boolValue{};
    std::vector<JsonValue> arrayValue;
    std::vector<std::pair<std::string, JsonValue>> objectValue;
    std::vector<std::size_t> objectKeyOffsets;
};

class JsonParser {
public:
    JsonParser(std::string_view text, std::size_t maxDepth, std::size_t maxNodes,
               std::size_t maxObjectMembers)
        : text_(text), maxDepth_(maxDepth), maxNodes_(maxNodes),
          maxObjectMembers_(maxObjectMembers) {}

    bool parse(JsonValue& result) {
        skipWhitespace();
        if (!parseValue(result, 1U)) {
            return false;
        }
        while (cursor_ < text_.size() && text_[cursor_] == ' ') {
            ++cursor_;
        }
        if (cursor_ != text_.size()) {
            fail(DecodeErrorCode::jsonTrailingGarbage, cursor_,
                 "JSON chunk has trailing garbage or illegal padding");
            return false;
        }
        return true;
    }

    [[nodiscard]] DecodeErrorCode code() const noexcept { return code_; }
    [[nodiscard]] std::size_t offset() const noexcept { return errorOffset_; }
    [[nodiscard]] std::string_view message() const noexcept { return message_; }

private:
    void fail(DecodeErrorCode code, std::size_t offset, std::string_view message) noexcept {
        if (code_ == DecodeErrorCode::none) {
            code_ = code;
            errorOffset_ = offset;
            message_ = message;
        }
    }

    void skipWhitespace() noexcept {
        while (cursor_ < text_.size()) {
            const char value = text_[cursor_];
            if (value != ' ' && value != '\t' && value != '\n' && value != '\r') {
                break;
            }
            ++cursor_;
        }
    }

    bool beginNode(std::size_t depth) noexcept {
        if (depth > maxDepth_) {
            fail(DecodeErrorCode::jsonDepthLimit, cursor_,
                 "JSON nesting exceeds the configured depth limit");
            return false;
        }
        if (nodeCount_ >= maxNodes_) {
            fail(DecodeErrorCode::jsonNodeLimit, cursor_,
                 "JSON value count exceeds the configured node limit");
            return false;
        }
        ++nodeCount_;
        return true;
    }

    bool parseValue(JsonValue& result, std::size_t depth) {
        skipWhitespace();
        if (!beginNode(depth)) {
            return false;
        }
        if (cursor_ >= text_.size()) {
            fail(DecodeErrorCode::jsonSyntax, cursor_, "JSON value is truncated");
            return false;
        }
        result.offset = cursor_;
        switch (text_[cursor_]) {
        case '{':
            return parseObject(result, depth);
        case '[':
            return parseArray(result, depth);
        case '"':
            result.kind = JsonValue::Kind::string;
            return parseString(result.stringValue);
        case 't':
            result.kind = JsonValue::Kind::boolean;
            result.boolValue = true;
            return consumeLiteral("true");
        case 'f':
            result.kind = JsonValue::Kind::boolean;
            result.boolValue = false;
            return consumeLiteral("false");
        case 'n':
            result.kind = JsonValue::Kind::nullValue;
            return consumeLiteral("null");
        default:
            result.kind = JsonValue::Kind::number;
            return parseNumber(result);
        }
    }

    bool consumeLiteral(std::string_view literal) {
        if (text_.substr(cursor_, literal.size()) != literal) {
            fail(DecodeErrorCode::jsonSyntax, cursor_, "invalid JSON literal");
            return false;
        }
        cursor_ += literal.size();
        return true;
    }

    static void appendUtf8(std::string& output, std::uint32_t scalar) {
        if (scalar <= 0x7fU) {
            output.push_back(static_cast<char>(scalar));
        } else if (scalar <= 0x7ffU) {
            output.push_back(static_cast<char>(0xc0U | (scalar >> 6U)));
            output.push_back(static_cast<char>(0x80U | (scalar & 0x3fU)));
        } else if (scalar <= 0xffffU) {
            output.push_back(static_cast<char>(0xe0U | (scalar >> 12U)));
            output.push_back(static_cast<char>(0x80U | ((scalar >> 6U) & 0x3fU)));
            output.push_back(static_cast<char>(0x80U | (scalar & 0x3fU)));
        } else {
            output.push_back(static_cast<char>(0xf0U | (scalar >> 18U)));
            output.push_back(static_cast<char>(0x80U | ((scalar >> 12U) & 0x3fU)));
            output.push_back(static_cast<char>(0x80U | ((scalar >> 6U) & 0x3fU)));
            output.push_back(static_cast<char>(0x80U | (scalar & 0x3fU)));
        }
    }

    bool parseHex4(std::uint32_t& value) noexcept {
        if (text_.size() - cursor_ < 4U) {
            fail(DecodeErrorCode::jsonSyntax, cursor_, "truncated JSON unicode escape");
            return false;
        }
        value = 0;
        for (unsigned index = 0; index < 4U; ++index) {
            const char digit = text_[cursor_++];
            value <<= 4U;
            if (digit >= '0' && digit <= '9') {
                value |= static_cast<std::uint32_t>(digit - '0');
            } else if (digit >= 'a' && digit <= 'f') {
                value |= static_cast<std::uint32_t>(digit - 'a' + 10);
            } else if (digit >= 'A' && digit <= 'F') {
                value |= static_cast<std::uint32_t>(digit - 'A' + 10);
            } else {
                fail(DecodeErrorCode::jsonSyntax, cursor_ - 1U,
                     "invalid JSON unicode escape");
                return false;
            }
        }
        return true;
    }

    bool parseString(std::string& output) {
        if (text_[cursor_] != '"') {
            fail(DecodeErrorCode::jsonSyntax, cursor_, "expected a JSON string");
            return false;
        }
        ++cursor_;
        while (cursor_ < text_.size()) {
            const unsigned char value = static_cast<unsigned char>(text_[cursor_++]);
            if (value == '"') {
                return true;
            }
            if (value < 0x20U) {
                fail(DecodeErrorCode::jsonSyntax, cursor_ - 1U,
                     "unescaped control character in JSON string");
                return false;
            }
            if (value != '\\') {
                if (value < 0x80U) {
                    output.push_back(static_cast<char>(value));
                    continue;
                }
                const std::size_t sequenceOffset = cursor_ - 1U;
                std::size_t continuationCount = 0;
                std::uint32_t scalar = 0;
                if (value >= 0xc2U && value <= 0xdfU) {
                    continuationCount = 1U;
                    scalar = value & 0x1fU;
                } else if (value >= 0xe0U && value <= 0xefU) {
                    continuationCount = 2U;
                    scalar = value & 0x0fU;
                } else if (value >= 0xf0U && value <= 0xf4U) {
                    continuationCount = 3U;
                    scalar = value & 0x07U;
                } else {
                    fail(DecodeErrorCode::jsonSyntax, sequenceOffset,
                         "invalid UTF-8 leading byte in JSON string");
                    return false;
                }
                if (text_.size() - cursor_ < continuationCount) {
                    fail(DecodeErrorCode::jsonSyntax, sequenceOffset,
                         "truncated UTF-8 sequence in JSON string");
                    return false;
                }
                for (std::size_t index = 0; index < continuationCount; ++index) {
                    const auto continuation =
                        static_cast<unsigned char>(text_[cursor_ + index]);
                    if ((continuation & 0xc0U) != 0x80U) {
                        fail(DecodeErrorCode::jsonSyntax, cursor_ + index,
                             "invalid UTF-8 continuation byte in JSON string");
                        return false;
                    }
                    scalar = (scalar << 6U) | (continuation & 0x3fU);
                }
                const bool overlong =
                    (continuationCount == 1U && scalar < 0x80U) ||
                    (continuationCount == 2U && scalar < 0x800U) ||
                    (continuationCount == 3U && scalar < 0x10000U);
                if (overlong || scalar > 0x10ffffU ||
                    (scalar >= 0xd800U && scalar <= 0xdfffU)) {
                    fail(DecodeErrorCode::jsonSyntax, sequenceOffset,
                         "invalid Unicode scalar in JSON UTF-8 string");
                    return false;
                }
                output.append(text_.substr(sequenceOffset, continuationCount + 1U));
                cursor_ += continuationCount;
                continue;
            }
            if (cursor_ >= text_.size()) {
                fail(DecodeErrorCode::jsonSyntax, cursor_, "truncated JSON escape");
                return false;
            }
            const char escaped = text_[cursor_++];
            switch (escaped) {
            case '"': output.push_back('"'); break;
            case '\\': output.push_back('\\'); break;
            case '/': output.push_back('/'); break;
            case 'b': output.push_back('\b'); break;
            case 'f': output.push_back('\f'); break;
            case 'n': output.push_back('\n'); break;
            case 'r': output.push_back('\r'); break;
            case 't': output.push_back('\t'); break;
            case 'u': {
                std::uint32_t scalar = 0;
                if (!parseHex4(scalar)) {
                    return false;
                }
                if (scalar >= 0xd800U && scalar <= 0xdbffU) {
                    if (text_.size() - cursor_ < 6U || text_[cursor_] != '\\' ||
                        text_[cursor_ + 1U] != 'u') {
                        fail(DecodeErrorCode::jsonSyntax, cursor_,
                             "JSON high surrogate is not followed by a low surrogate");
                        return false;
                    }
                    cursor_ += 2U;
                    std::uint32_t low = 0;
                    if (!parseHex4(low) || low < 0xdc00U || low > 0xdfffU) {
                        fail(DecodeErrorCode::jsonSyntax, cursor_ - 4U,
                             "invalid JSON low surrogate");
                        return false;
                    }
                    scalar = 0x10000U + ((scalar - 0xd800U) << 10U) + (low - 0xdc00U);
                } else if (scalar >= 0xdc00U && scalar <= 0xdfffU) {
                    fail(DecodeErrorCode::jsonSyntax, cursor_ - 4U,
                         "unpaired JSON low surrogate");
                    return false;
                }
                appendUtf8(output, scalar);
                break;
            }
            default:
                fail(DecodeErrorCode::jsonSyntax, cursor_ - 1U, "invalid JSON escape");
                return false;
            }
        }
        fail(DecodeErrorCode::jsonSyntax, cursor_, "unterminated JSON string");
        return false;
    }

    bool parseObject(JsonValue& result, std::size_t depth) {
        result.kind = JsonValue::Kind::object;
        ++cursor_;
        skipWhitespace();
        if (cursor_ < text_.size() && text_[cursor_] == '}') {
            ++cursor_;
            return true;
        }
        while (cursor_ < text_.size()) {
            skipWhitespace();
            if (result.objectValue.size() >= maxObjectMembers_) {
                fail(DecodeErrorCode::jsonObjectMemberLimit, cursor_,
                     "JSON object member count exceeds the configured limit");
                return false;
            }
            const std::size_t keyOffset = cursor_;
            std::string key;
            if (cursor_ >= text_.size() || text_[cursor_] != '"' || !parseString(key)) {
                if (code_ == DecodeErrorCode::none) {
                    fail(DecodeErrorCode::jsonSyntax, cursor_, "expected JSON object key");
                }
                return false;
            }
            skipWhitespace();
            if (cursor_ >= text_.size() || text_[cursor_] != ':') {
                fail(DecodeErrorCode::jsonSyntax, cursor_, "expected colon after JSON key");
                return false;
            }
            ++cursor_;
            JsonValue value;
            if (!parseValue(value, depth + 1U)) {
                return false;
            }
            result.objectValue.emplace_back(std::move(key), std::move(value));
            result.objectKeyOffsets.push_back(keyOffset);
            skipWhitespace();
            if (cursor_ >= text_.size()) {
                fail(DecodeErrorCode::jsonSyntax, cursor_, "unterminated JSON object");
                return false;
            }
            if (text_[cursor_] == '}') {
                ++cursor_;
                return validateObjectKeys(result);
            }
            if (text_[cursor_] != ',') {
                fail(DecodeErrorCode::jsonSyntax, cursor_, "expected comma in JSON object");
                return false;
            }
            ++cursor_;
        }
        fail(DecodeErrorCode::jsonSyntax, cursor_, "unterminated JSON object");
        return false;
    }

    bool validateObjectKeys(const JsonValue& object) {
        std::vector<std::size_t> order(object.objectValue.size());
        for (std::size_t index = 0; index < order.size(); ++index) {
            order[index] = index;
        }
        std::sort(order.begin(), order.end(), [&](std::size_t first, std::size_t second) {
            return object.objectValue[first].first < object.objectValue[second].first;
        });
        for (std::size_t index = 1; index < order.size(); ++index) {
            const std::size_t first = order[index - 1U];
            const std::size_t second = order[index];
            if (object.objectValue[first].first == object.objectValue[second].first) {
                fail(DecodeErrorCode::jsonDuplicateKey,
                     object.objectKeyOffsets[std::max(first, second)],
                     "duplicate JSON object key");
                return false;
            }
        }
        return true;
    }

    bool parseArray(JsonValue& result, std::size_t depth) {
        result.kind = JsonValue::Kind::array;
        ++cursor_;
        skipWhitespace();
        if (cursor_ < text_.size() && text_[cursor_] == ']') {
            ++cursor_;
            return true;
        }
        while (cursor_ < text_.size()) {
            JsonValue value;
            if (!parseValue(value, depth + 1U)) {
                return false;
            }
            result.arrayValue.push_back(std::move(value));
            skipWhitespace();
            if (cursor_ >= text_.size()) {
                fail(DecodeErrorCode::jsonSyntax, cursor_, "unterminated JSON array");
                return false;
            }
            if (text_[cursor_] == ']') {
                ++cursor_;
                return true;
            }
            if (text_[cursor_] != ',') {
                fail(DecodeErrorCode::jsonSyntax, cursor_, "expected comma in JSON array");
                return false;
            }
            ++cursor_;
        }
        fail(DecodeErrorCode::jsonSyntax, cursor_, "unterminated JSON array");
        return false;
    }

    bool parseNumber(JsonValue& output) {
        const std::size_t start = cursor_;
        if (cursor_ < text_.size() && text_[cursor_] == '-') {
            ++cursor_;
        }
        if (cursor_ >= text_.size()) {
            fail(DecodeErrorCode::jsonSyntax, start, "truncated JSON number");
            return false;
        }
        if (text_[cursor_] == '0') {
            ++cursor_;
            if (cursor_ < text_.size() && text_[cursor_] >= '0' && text_[cursor_] <= '9') {
                fail(DecodeErrorCode::jsonSyntax, cursor_, "leading zero in JSON number");
                return false;
            }
        } else if (text_[cursor_] >= '1' && text_[cursor_] <= '9') {
            do {
                ++cursor_;
            } while (cursor_ < text_.size() && text_[cursor_] >= '0' && text_[cursor_] <= '9');
        } else {
            fail(DecodeErrorCode::jsonSyntax, cursor_, "invalid JSON number");
            return false;
        }
        if (cursor_ < text_.size() && text_[cursor_] == '.') {
            ++cursor_;
            if (cursor_ >= text_.size() || text_[cursor_] < '0' || text_[cursor_] > '9') {
                fail(DecodeErrorCode::jsonSyntax, cursor_, "missing JSON fraction digits");
                return false;
            }
            do {
                ++cursor_;
            } while (cursor_ < text_.size() && text_[cursor_] >= '0' && text_[cursor_] <= '9');
        }
        if (cursor_ < text_.size() && (text_[cursor_] == 'e' || text_[cursor_] == 'E')) {
            ++cursor_;
            if (cursor_ < text_.size() && (text_[cursor_] == '+' || text_[cursor_] == '-')) {
                ++cursor_;
            }
            if (cursor_ >= text_.size() || text_[cursor_] < '0' || text_[cursor_] > '9') {
                fail(DecodeErrorCode::jsonSyntax, cursor_, "missing JSON exponent digits");
                return false;
            }
            do {
                ++cursor_;
            } while (cursor_ < text_.size() && text_[cursor_] >= '0' && text_[cursor_] <= '9');
        }
        const char* first = text_.data() + start;
        const char* last = text_.data() + cursor_;
        output.numberLexeme = text_.substr(start, cursor_ - start);
        const auto converted = std::from_chars(first, last, output.numberValue,
                                               std::chars_format::general);
        if (converted.ec != std::errc{} || converted.ptr != last ||
            !std::isfinite(output.numberValue)) {
            fail(DecodeErrorCode::jsonSyntax, start, "JSON number is not finite or representable");
            return false;
        }
        return true;
    }

    std::string_view text_;
    std::size_t maxDepth_{};
    std::size_t maxNodes_{};
    std::size_t maxObjectMembers_{};
    std::size_t cursor_{};
    std::size_t nodeCount_{};
    DecodeErrorCode code_{DecodeErrorCode::none};
    std::size_t errorOffset_{};
    std::string_view message_{};
};

const JsonValue* member(const JsonValue& object, std::string_view key) noexcept {
    if (object.kind != JsonValue::Kind::object) {
        return nullptr;
    }
    for (const auto& item : object.objectValue) {
        if (item.first == key) {
            return &item.second;
        }
    }
    return nullptr;
}

bool hasMember(const JsonValue& object, std::string_view key) noexcept {
    return member(object, key) != nullptr;
}

bool supportedMinimumVersion(std::string_view version) noexcept {
    const std::size_t separator = version.find('.');
    if (separator == std::string_view::npos || separator == 0U ||
        separator + 1U == version.size() ||
        version.find('.', separator + 1U) != std::string_view::npos) {
        return false;
    }
    std::uint64_t major = 0;
    std::uint64_t minor = 0;
    const char* begin = version.data();
    const char* separatorPointer = begin + separator;
    const char* end = begin + version.size();
    const auto majorResult = std::from_chars(begin, separatorPointer, major);
    const auto minorResult = std::from_chars(separatorPointer + 1U, end, minor);
    return majorResult.ec == std::errc{} && majorResult.ptr == separatorPointer &&
           minorResult.ec == std::errc{} && minorResult.ptr == end &&
           (major < 2U || (major == 2U && minor == 0U));
}

bool integerValue(const JsonValue& value, std::size_t& result) noexcept {
    if (value.kind != JsonValue::Kind::number || value.numberLexeme.empty() ||
        (value.numberLexeme.size() > 1U && value.numberLexeme.front() == '0') ||
        !std::all_of(value.numberLexeme.begin(), value.numberLexeme.end(),
                     [](char character) { return character >= '0' && character <= '9'; })) {
        return false;
    }
    const char* first = value.numberLexeme.data();
    const char* last = first + value.numberLexeme.size();
    const auto converted = std::from_chars(first, last, result);
    return converted.ec == std::errc{} && converted.ptr == last;
}

bool optionalInteger(const JsonValue& object, std::string_view key, std::size_t defaultValue,
                     std::size_t& result) noexcept {
    const JsonValue* value = member(object, key);
    if (value == nullptr) {
        result = defaultValue;
        return true;
    }
    return integerValue(*value, result);
}

struct BufferViewInfo {
    std::size_t offset{};
    std::size_t length{};
    std::size_t stride{};
    std::size_t jsonOffset{};
};

struct AccessorInfo {
    std::size_t view{};
    std::size_t offset{};
    std::size_t componentType{};
    std::size_t count{};
    std::string_view type{};
    bool normalized{};
    std::size_t jsonOffset{};
};

struct PrimitiveInfo {
    std::size_t positionAccessor{};
    bool hasIndices{};
    std::size_t indexAccessor{};
    std::size_t jsonOffset{};
};

std::size_t componentSize(std::size_t type) noexcept {
    switch (type) {
    case kUnsignedByte: return 1U;
    case kUnsignedShort: return 2U;
    case kUnsignedInt:
    case kFloat: return 4U;
    default: return 0U;
    }
}

bool accessorRange(const AccessorInfo& accessor, const BufferViewInfo& view,
                   std::size_t elementSize, std::size_t bufferLength,
                   std::size_t& start, std::size_t& stride) noexcept {
    stride = view.stride == 0U ? elementSize : view.stride;
    if (stride < elementSize || stride > 252U ||
        (view.stride != 0U && (view.stride < 4U || view.stride % 4U != 0U))) {
        return false;
    }
    const std::size_t alignment = componentSize(accessor.componentType);
    if (alignment == 0U || accessor.offset % alignment != 0U ||
        view.offset % alignment != 0U || stride % alignment != 0U) {
        return false;
    }
    if (!checkedAdd(view.offset, accessor.offset, start)) {
        return false;
    }
    std::size_t end = start;
    if (accessor.count != 0U) {
        std::size_t tail = 0;
        if (!checkedMultiply(accessor.count - 1U, stride, tail) ||
            !checkedAdd(end, tail, end) || !checkedAdd(end, elementSize, end)) {
            return false;
        }
    }
    std::size_t viewEnd = 0;
    return checkedAdd(view.offset, view.length, viewEnd) && start >= view.offset &&
           end <= viewEnd && end <= bufferLength;
}

std::string formatFloat(float value) {
    char storage[64]{};
    const auto converted = std::to_chars(std::begin(storage), std::end(storage), value,
                                         std::chars_format::general,
                                         std::numeric_limits<float>::max_digits10);
    if (converted.ec != std::errc{}) {
        throw std::runtime_error("float formatting failed");
    }
    return std::string(storage, converted.ptr);
}

void appendJsonUnsigned(std::string& output, std::size_t value) {
    char storage[32]{};
    const auto converted = std::to_chars(std::begin(storage), std::end(storage), value);
    if (converted.ec != std::errc{}) {
        throw std::runtime_error("integer formatting failed");
    }
    output.append(storage, converted.ptr);
}

}  // namespace

EncodeResult encodeGlb(const Mesh& mesh) noexcept {
    try {
        if (!mesh.validate().ok || mesh.vertices().empty()) {
            return encodeFailure(ErrorCategory::topology, EncodeErrorCode::invalidMesh,
                                 "mesh must be valid and contain at least one vertex");
        }
        if (mesh.vertices().size() - 1U > std::numeric_limits<std::uint32_t>::max()) {
            return encodeFailure(ErrorCategory::resource, EncodeErrorCode::tooManyVertices,
                                 "GLB indices cannot address this many vertices");
        }

        std::size_t triangleCount = 0;
        std::size_t maximumReferencedIndex = 0;
        bool triangleOverflow = false;
        bool missingReference = false;
        mesh.visitTriangles([&](const Triangle& triangle) {
            if (triangleCount == std::numeric_limits<std::size_t>::max()) {
                triangleOverflow = true;
                return;
            }
            ++triangleCount;
            for (const VertexId id : triangle.vertices) {
                const Vertex* vertex = mesh.vertex(id);
                if (vertex == nullptr) {
                    missingReference = true;
                    continue;
                }
                const std::size_t index =
                    static_cast<std::size_t>(vertex - mesh.vertices().data());
                maximumReferencedIndex = std::max(maximumReferencedIndex, index);
            }
        });
        if (triangleOverflow || triangleCount > std::numeric_limits<std::uint32_t>::max() / 3U) {
            return encodeFailure(ErrorCategory::resource, EncodeErrorCode::tooManyTriangles,
                                 "GLB index count exceeds the supported range");
        }
        if (missingReference) {
            return encodeFailure(ErrorCategory::topology,
                                 EncodeErrorCode::missingVertexReference,
                                 "triangle references a missing stable vertex ID");
        }
        if (triangleCount == 0U) {
            return encodeFailure(ErrorCategory::topology, EncodeErrorCode::invalidMesh,
                                 "mesh must contain at least one triangle");
        }

        std::array<float, 3> minimum{};
        std::array<float, 3> maximum{};
        bool firstPosition = true;
        std::vector<std::uint8_t> bin;
        std::size_t positionBytes = 0;
        if (!checkedMultiply(mesh.vertices().size(), 12U, positionBytes)) {
            return encodeFailure(ErrorCategory::resource, EncodeErrorCode::integerOverflow,
                                 "GLB position byte count overflows this platform");
        }
        const std::size_t indexSize = maximumReferencedIndex <= 0xffU ? 1U :
                                      maximumReferencedIndex <= 0xffffU ? 2U : 4U;
        const std::uint32_t indexComponent = indexSize == 1U ? kUnsignedByte :
                                             indexSize == 2U ? kUnsignedShort : kUnsignedInt;
        std::size_t indexCount = 0;
        std::size_t indexBytes = 0;
        if (!checkedMultiply(triangleCount, 3U, indexCount) ||
            !checkedMultiply(indexCount, indexSize, indexBytes)) {
            return encodeFailure(ErrorCategory::resource, EncodeErrorCode::integerOverflow,
                                 "GLB index byte count overflows this platform");
        }
        std::size_t binSize = 0;
        if (!checkedAdd(positionBytes, indexBytes, binSize)) {
            return encodeFailure(ErrorCategory::resource, EncodeErrorCode::integerOverflow,
                                 "GLB BIN size overflows this platform");
        }
        bin.reserve(align4(binSize));
        for (const Vertex& vertex : mesh.vertices()) {
            const double components[3]{vertex.position.x, vertex.position.y, vertex.position.z};
            for (std::size_t axis = 0; axis < 3U; ++axis) {
                if (!std::isfinite(components[axis])) {
                    return encodeFailure(ErrorCategory::topology,
                                         EncodeErrorCode::nonFinitePosition,
                                         "GLB POSITION values must be finite");
                }
                if (components[axis] > static_cast<double>(std::numeric_limits<float>::max()) ||
                    components[axis] < -static_cast<double>(std::numeric_limits<float>::max())) {
                    return encodeFailure(ErrorCategory::compatibility,
                                         EncodeErrorCode::positionOutOfFloatRange,
                                         "mesh position cannot be represented as finite float32");
                }
                const float packed = static_cast<float>(components[axis]);
                if (!std::isfinite(packed)) {
                    return encodeFailure(ErrorCategory::compatibility,
                                         EncodeErrorCode::positionOutOfFloatRange,
                                         "mesh position cannot be represented as finite float32");
                }
                appendFloat(bin, packed);
                if (firstPosition) {
                    minimum[axis] = packed;
                    maximum[axis] = packed;
                } else {
                    minimum[axis] = std::min(minimum[axis], packed);
                    maximum[axis] = std::max(maximum[axis], packed);
                }
            }
            firstPosition = false;
        }
        mesh.visitTriangles([&](const Triangle& triangle) {
            for (const VertexId id : triangle.vertices) {
                const Vertex* vertex = mesh.vertex(id);
                const std::size_t index = static_cast<std::size_t>(vertex - mesh.vertices().data());
                if (indexSize == 1U) {
                    bin.push_back(static_cast<std::uint8_t>(index));
                } else if (indexSize == 2U) {
                    appendU16(bin, static_cast<std::uint16_t>(index));
                } else {
                    appendU32(bin, static_cast<std::uint32_t>(index));
                }
            }
        });
        const std::size_t unpaddedBinSize = bin.size();
        bin.resize(align4(bin.size()), 0U);

        std::string json;
        json.reserve(768U);
        json += "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],";
        json += "\"nodes\":[{\"mesh\":0}],\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1,\"mode\":4}]}],";
        json += "\"buffers\":[{\"byteLength\":";
        appendJsonUnsigned(json, unpaddedBinSize);
        json += "}],\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":";
        appendJsonUnsigned(json, positionBytes);
        json += ",\"target\":34962},{\"buffer\":0,\"byteOffset\":";
        appendJsonUnsigned(json, positionBytes);
        json += ",\"byteLength\":";
        appendJsonUnsigned(json, indexBytes);
        json += ",\"target\":34963}],\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":";
        appendJsonUnsigned(json, mesh.vertices().size());
        json += ",\"type\":\"VEC3\",\"min\":[";
        for (std::size_t axis = 0; axis < 3U; ++axis) {
            if (axis != 0U) json.push_back(',');
            json += formatFloat(minimum[axis]);
        }
        json += "],\"max\":[";
        for (std::size_t axis = 0; axis < 3U; ++axis) {
            if (axis != 0U) json.push_back(',');
            json += formatFloat(maximum[axis]);
        }
        json += "]},{\"bufferView\":1,\"componentType\":";
        appendJsonUnsigned(json, indexComponent);
        json += ",\"count\":";
        appendJsonUnsigned(json, indexCount);
        json += ",\"type\":\"SCALAR\"}]}";
        const std::size_t jsonLength = align4(json.size());
        json.resize(jsonLength, ' ');

        std::size_t totalSize = 12U;
        if (!checkedAdd(totalSize, 8U, totalSize) || !checkedAdd(totalSize, json.size(), totalSize) ||
            !checkedAdd(totalSize, 8U, totalSize) || !checkedAdd(totalSize, bin.size(), totalSize) ||
            totalSize > std::numeric_limits<std::uint32_t>::max() ||
            json.size() > std::numeric_limits<std::uint32_t>::max() ||
            bin.size() > std::numeric_limits<std::uint32_t>::max()) {
            return encodeFailure(ErrorCategory::resource, EncodeErrorCode::integerOverflow,
                                 "encoded GLB exceeds the 32-bit container length limit");
        }
        std::vector<std::uint8_t> output;
        output.reserve(totalSize);
        appendU32(output, kGlbMagic);
        appendU32(output, 2U);
        appendU32(output, static_cast<std::uint32_t>(totalSize));
        appendU32(output, static_cast<std::uint32_t>(json.size()));
        appendU32(output, kJsonChunk);
        output.insert(output.end(), json.begin(), json.end());
        appendU32(output, static_cast<std::uint32_t>(bin.size()));
        appendU32(output, kBinChunk);
        output.insert(output.end(), bin.begin(), bin.end());
        return {true, std::move(output), {}};
    } catch (const std::bad_alloc&) {
        return encodeFailure(ErrorCategory::allocation, EncodeErrorCode::allocationFailed,
                             "allocation failed while encoding GLB");
    } catch (...) {
        return encodeFailure(ErrorCategory::internal, EncodeErrorCode::internalError,
                             "unexpected failure while encoding GLB");
    }
}

DecodeResult decodeGlb(std::span<const std::uint8_t> bytes, LoadLimits limits) noexcept {
    try {
        if (bytes.size() > limits.maxBytes) {
            return decodeFailure(ErrorCategory::resource, DecodeErrorCode::inputTooLarge, 0,
                                 "GLB exceeds the configured byte limit");
        }
        if (bytes.size() < 12U) {
            return decodeFailure(ErrorCategory::input, DecodeErrorCode::truncated, bytes.size(),
                                 "GLB header is truncated");
        }
        if (readU32(bytes, 0) != kGlbMagic) {
            return decodeFailure(ErrorCategory::format, DecodeErrorCode::badMagic, 0,
                                 "GLB magic is invalid");
        }
        if (readU32(bytes, 4) != 2U) {
            return decodeFailure(ErrorCategory::compatibility,
                                 DecodeErrorCode::unsupportedVersion, 4,
                                 "only GLB version 2 is supported");
        }
        const std::size_t declaredLength = readU32(bytes, 8);
        if (declaredLength < bytes.size()) {
            return decodeFailure(ErrorCategory::format, DecodeErrorCode::trailingBytes,
                                 declaredLength, "GLB contains bytes beyond its declared length");
        }
        if (declaredLength > bytes.size()) {
            return decodeFailure(ErrorCategory::input, DecodeErrorCode::truncated, bytes.size(),
                                 "GLB is shorter than its declared length");
        }
        if (bytes.size() < 20U) {
            return decodeFailure(ErrorCategory::input, DecodeErrorCode::truncated, bytes.size(),
                                 "GLB JSON chunk header is truncated");
        }
        const std::size_t jsonLength = readU32(bytes, 12);
        if (readU32(bytes, 16) != kJsonChunk) {
            return decodeFailure(ErrorCategory::format, DecodeErrorCode::badChunkOrder, 16,
                                 "first GLB chunk must be JSON");
        }
        if (jsonLength == 0U || jsonLength % 4U != 0U) {
            return decodeFailure(ErrorCategory::format, DecodeErrorCode::invalidChunkPadding, 12,
                                 "JSON chunk length must be nonzero and 4-byte aligned");
        }
        if (jsonLength > limits.maxJsonBytes) {
            return decodeFailure(ErrorCategory::resource, DecodeErrorCode::jsonTooLarge, 12,
                                 "JSON chunk exceeds the configured byte limit");
        }
        std::size_t jsonEnd = 0;
        if (!checkedAdd(20U, jsonLength, jsonEnd)) {
            return decodeFailure(ErrorCategory::format, DecodeErrorCode::integerOverflow, 12,
                                 "JSON chunk length overflows this platform");
        }
        if (jsonEnd > bytes.size()) {
            return decodeFailure(ErrorCategory::input, DecodeErrorCode::truncated, bytes.size(),
                                 "GLB JSON chunk is truncated");
        }
        std::size_t binHeaderEnd = 0;
        if (!checkedAdd(jsonEnd, 8U, binHeaderEnd) || binHeaderEnd > bytes.size()) {
            return decodeFailure(ErrorCategory::format, DecodeErrorCode::missingBinChunk, jsonEnd,
                                 "GLB BIN chunk is required");
        }
        const std::size_t binLength = readU32(bytes, jsonEnd);
        if (readU32(bytes, jsonEnd + 4U) != kBinChunk) {
            return decodeFailure(ErrorCategory::format, DecodeErrorCode::badChunkType,
                                 jsonEnd + 4U, "second GLB chunk must be BIN");
        }
        if (binLength % 4U != 0U) {
            return decodeFailure(ErrorCategory::format, DecodeErrorCode::invalidChunkPadding,
                                 jsonEnd, "BIN chunk length must be 4-byte aligned");
        }
        std::size_t fileEnd = 0;
        if (!checkedAdd(binHeaderEnd, binLength, fileEnd)) {
            return decodeFailure(ErrorCategory::format, DecodeErrorCode::integerOverflow, jsonEnd,
                                 "BIN chunk length overflows this platform");
        }
        if (fileEnd > bytes.size()) {
            return decodeFailure(ErrorCategory::input, DecodeErrorCode::truncated, bytes.size(),
                                 "GLB BIN chunk is truncated");
        }
        if (fileEnd < bytes.size()) {
            return decodeFailure(ErrorCategory::format, DecodeErrorCode::trailingBytes, fileEnd,
                                 "GLB contains an unsupported extra chunk or trailing bytes");
        }

        const std::string_view jsonText(
            reinterpret_cast<const char*>(bytes.data() + 20U), jsonLength);
        JsonValue root;
        JsonParser parser(jsonText, limits.maxJsonDepth, limits.maxJsonNodes,
                          limits.maxJsonObjectMembers);
        if (!parser.parse(root)) {
            const ErrorCategory category =
                parser.code() == DecodeErrorCode::jsonDepthLimit ||
                        parser.code() == DecodeErrorCode::jsonNodeLimit ||
                        parser.code() == DecodeErrorCode::jsonObjectMemberLimit
                    ? ErrorCategory::resource
                    : ErrorCategory::format;
            return decodeFailure(category, parser.code(), 20U + parser.offset(), parser.message());
        }
        if (root.kind != JsonValue::Kind::object) {
            return decodeFailure(ErrorCategory::format, DecodeErrorCode::wrongPropertyType,
                                 20U + root.offset, "glTF JSON root must be an object");
        }
        const auto failAt = [](ErrorCategory category, DecodeErrorCode code,
                               const JsonValue* value, std::string_view message) {
            return decodeFailure(category, code, value == nullptr ? 20U : 20U + value->offset,
                                 message);
        };

        if (hasMember(root, "extensions")) {
            return failAt(ErrorCategory::unsupported, DecodeErrorCode::unsupportedFeature,
                          member(root, "extensions"), "glTF extensions are not supported");
        }

        const JsonValue* asset = member(root, "asset");
        const JsonValue* assetVersion = asset == nullptr ? nullptr : member(*asset, "version");
        if (asset == nullptr || asset->kind != JsonValue::Kind::object ||
            assetVersion == nullptr || assetVersion->kind != JsonValue::Kind::string) {
            return failAt(ErrorCategory::format, DecodeErrorCode::missingRequiredProperty, asset,
                          "glTF asset.version string is required");
        }
        if (assetVersion->stringValue != "2.0") {
            return failAt(ErrorCategory::compatibility, DecodeErrorCode::unsupportedVersion,
                          assetVersion, "only glTF asset version 2.0 is supported");
        }
        if (hasMember(*asset, "extensions")) {
            return failAt(ErrorCategory::unsupported, DecodeErrorCode::unsupportedFeature,
                          member(*asset, "extensions"), "asset extensions are not supported");
        }
        const JsonValue* minimumVersion = member(*asset, "minVersion");
        if (minimumVersion != nullptr &&
            (minimumVersion->kind != JsonValue::Kind::string ||
             !supportedMinimumVersion(minimumVersion->stringValue))) {
            return failAt(ErrorCategory::compatibility, DecodeErrorCode::unsupportedVersion,
                          minimumVersion, "asset.minVersion requires an unsupported glTF version");
        }
        for (std::string_view key : {"extensionsUsed", "extensionsRequired"}) {
            const JsonValue* extensions = member(root, key);
            if (extensions != nullptr &&
                (extensions->kind != JsonValue::Kind::array || !extensions->arrayValue.empty())) {
                return failAt(ErrorCategory::unsupported, DecodeErrorCode::unsupportedFeature,
                              extensions, "glTF extensions are not supported");
            }
        }
        const JsonValue* animations = member(root, "animations");
        if (animations != nullptr &&
            (animations->kind != JsonValue::Kind::array || !animations->arrayValue.empty())) {
            return failAt(ErrorCategory::unsupported, DecodeErrorCode::unsupportedFeature,
                          animations, "animations are not supported");
        }
        const JsonValue* skins = member(root, "skins");
        if (skins != nullptr && (skins->kind != JsonValue::Kind::array || !skins->arrayValue.empty())) {
            return failAt(ErrorCategory::unsupported, DecodeErrorCode::unsupportedFeature, skins,
                          "skins are not supported");
        }

        const JsonValue* buffers = member(root, "buffers");
        if (buffers == nullptr || buffers->kind != JsonValue::Kind::array ||
            buffers->arrayValue.size() != 1U ||
            buffers->arrayValue[0].kind != JsonValue::Kind::object) {
            return failAt(ErrorCategory::format, DecodeErrorCode::missingRequiredProperty, buffers,
                          "exactly one embedded GLB buffer is required");
        }
        const JsonValue& buffer = buffers->arrayValue[0];
        if (hasMember(buffer, "extensions")) {
            return failAt(ErrorCategory::unsupported, DecodeErrorCode::unsupportedFeature,
                          member(buffer, "extensions"), "buffer extensions are not supported");
        }
        if (hasMember(buffer, "uri")) {
            return failAt(ErrorCategory::unsupported,
                          DecodeErrorCode::unsupportedExternalBuffer, member(buffer, "uri"),
                          "external and data URI buffers are not supported");
        }
        const JsonValue* bufferByteLengthValue = member(buffer, "byteLength");
        std::size_t bufferByteLength = 0;
        if (bufferByteLengthValue == nullptr ||
            !integerValue(*bufferByteLengthValue, bufferByteLength)) {
            return failAt(ErrorCategory::format, DecodeErrorCode::wrongPropertyType,
                          bufferByteLengthValue, "buffer.byteLength must be a nonnegative integer");
        }
        if (bufferByteLength > binLength || binLength - bufferByteLength > 3U) {
            return failAt(ErrorCategory::format, DecodeErrorCode::bufferViewOutOfBounds,
                          bufferByteLengthValue,
                          "buffer.byteLength is inconsistent with the GLB BIN chunk");
        }
        for (std::size_t offset = bufferByteLength; offset < binLength; ++offset) {
            if (bytes[binHeaderEnd + offset] != 0U) {
                return decodeFailure(ErrorCategory::format,
                                     DecodeErrorCode::invalidChunkPadding,
                                     binHeaderEnd + offset,
                                     "GLB BIN padding bytes must be zero");
            }
        }
        const auto bin = bytes.subspan(binHeaderEnd, bufferByteLength);

        const JsonValue* viewsValue = member(root, "bufferViews");
        if (viewsValue == nullptr || viewsValue->kind != JsonValue::Kind::array) {
            return failAt(ErrorCategory::format, DecodeErrorCode::missingRequiredProperty,
                          viewsValue, "bufferViews array is required");
        }
        std::vector<BufferViewInfo> views;
        views.reserve(viewsValue->arrayValue.size());
        for (const JsonValue& viewValue : viewsValue->arrayValue) {
            if (viewValue.kind != JsonValue::Kind::object || hasMember(viewValue, "extensions")) {
                return failAt(hasMember(viewValue, "extensions") ? ErrorCategory::unsupported
                                                                  : ErrorCategory::format,
                              hasMember(viewValue, "extensions")
                                  ? DecodeErrorCode::unsupportedFeature
                                  : DecodeErrorCode::wrongPropertyType,
                              &viewValue, "bufferView must be an unextended object");
            }
            std::size_t bufferIndex = 0;
            const JsonValue* bufferIndexValue = member(viewValue, "buffer");
            const JsonValue* lengthValue = member(viewValue, "byteLength");
            BufferViewInfo view;
            view.jsonOffset = viewValue.offset;
            if (bufferIndexValue == nullptr || !integerValue(*bufferIndexValue, bufferIndex) ||
                bufferIndex != 0U || lengthValue == nullptr ||
                !integerValue(*lengthValue, view.length) ||
                !optionalInteger(viewValue, "byteOffset", 0U, view.offset) ||
                !optionalInteger(viewValue, "byteStride", 0U, view.stride)) {
                return failAt(ErrorCategory::format, DecodeErrorCode::wrongPropertyType,
                              &viewValue, "bufferView fields are invalid");
            }
            std::size_t end = 0;
            if (!checkedAdd(view.offset, view.length, end) || end > bufferByteLength) {
                return failAt(ErrorCategory::format, DecodeErrorCode::bufferViewOutOfBounds,
                              &viewValue, "bufferView exceeds the embedded buffer");
            }
            if (view.stride != 0U &&
                (view.stride < 4U || view.stride > 252U || view.stride % 4U != 0U)) {
                return failAt(ErrorCategory::format, DecodeErrorCode::invalidStride,
                              member(viewValue, "byteStride"), "bufferView byteStride is invalid");
            }
            views.push_back(view);
        }

        const JsonValue* accessorsValue = member(root, "accessors");
        if (accessorsValue == nullptr || accessorsValue->kind != JsonValue::Kind::array) {
            return failAt(ErrorCategory::format, DecodeErrorCode::missingRequiredProperty,
                          accessorsValue, "accessors array is required");
        }
        std::vector<AccessorInfo> accessors;
        accessors.reserve(accessorsValue->arrayValue.size());
        for (const JsonValue& accessorValue : accessorsValue->arrayValue) {
            if (accessorValue.kind != JsonValue::Kind::object) {
                return failAt(ErrorCategory::format, DecodeErrorCode::wrongPropertyType,
                              &accessorValue, "accessor must be an object");
            }
            if (hasMember(accessorValue, "sparse") || hasMember(accessorValue, "extensions")) {
                return failAt(ErrorCategory::unsupported, DecodeErrorCode::unsupportedAccessor,
                              &accessorValue, "sparse and extended accessors are not supported");
            }
            const JsonValue* viewIndexValue = member(accessorValue, "bufferView");
            const JsonValue* componentValue = member(accessorValue, "componentType");
            const JsonValue* countValue = member(accessorValue, "count");
            const JsonValue* typeValue = member(accessorValue, "type");
            AccessorInfo accessor;
            accessor.jsonOffset = accessorValue.offset;
            if (viewIndexValue == nullptr || !integerValue(*viewIndexValue, accessor.view) ||
                accessor.view >= views.size() || componentValue == nullptr ||
                !integerValue(*componentValue, accessor.componentType) || countValue == nullptr ||
                !integerValue(*countValue, accessor.count) || typeValue == nullptr ||
                typeValue->kind != JsonValue::Kind::string ||
                !optionalInteger(accessorValue, "byteOffset", 0U, accessor.offset)) {
                return failAt(ErrorCategory::format, DecodeErrorCode::wrongPropertyType,
                              &accessorValue, "accessor fields are invalid");
            }
            accessor.type = typeValue->stringValue;
            const JsonValue* normalized = member(accessorValue, "normalized");
            if (normalized != nullptr && normalized->kind != JsonValue::Kind::boolean) {
                return failAt(ErrorCategory::format, DecodeErrorCode::wrongPropertyType,
                              normalized, "accessor.normalized must be boolean");
            }
            accessor.normalized = normalized != nullptr && normalized->boolValue;
            accessors.push_back(accessor);
        }

        const JsonValue* scenes = member(root, "scenes");
        const JsonValue* nodes = member(root, "nodes");
        const JsonValue* meshes = member(root, "meshes");
        if (scenes == nullptr || scenes->kind != JsonValue::Kind::array ||
            scenes->arrayValue.size() != 1U || scenes->arrayValue[0].kind != JsonValue::Kind::object ||
            nodes == nullptr || nodes->kind != JsonValue::Kind::array ||
            nodes->arrayValue.size() != 1U || nodes->arrayValue[0].kind != JsonValue::Kind::object ||
            meshes == nullptr || meshes->kind != JsonValue::Kind::array ||
            meshes->arrayValue.size() != 1U || meshes->arrayValue[0].kind != JsonValue::Kind::object) {
            return failAt(ErrorCategory::unsupported, DecodeErrorCode::unsupportedFeature, meshes,
                          "exactly one scene, node, and mesh are supported");
        }
        std::size_t defaultScene = 0;
        const JsonValue* sceneValue = member(root, "scene");
        if (sceneValue != nullptr && (!integerValue(*sceneValue, defaultScene) || defaultScene != 0U)) {
            return failAt(ErrorCategory::unsupported, DecodeErrorCode::unsupportedFeature,
                          sceneValue, "only scene 0 is supported");
        }
        const JsonValue& scene = scenes->arrayValue[0];
        if (hasMember(scene, "extensions")) {
            return failAt(ErrorCategory::unsupported, DecodeErrorCode::unsupportedFeature,
                          member(scene, "extensions"), "scene extensions are not supported");
        }
        const JsonValue* sceneNodes = member(scene, "nodes");
        std::size_t sceneNodeIndex = 0;
        if (sceneNodes == nullptr || sceneNodes->kind != JsonValue::Kind::array ||
            sceneNodes->arrayValue.size() != 1U ||
            !integerValue(sceneNodes->arrayValue[0], sceneNodeIndex) || sceneNodeIndex != 0U) {
            return failAt(ErrorCategory::unsupported, DecodeErrorCode::unsupportedFeature,
                          sceneNodes, "scene must contain only node 0");
        }
        const JsonValue& node = nodes->arrayValue[0];
        if (hasMember(node, "extensions")) {
            return failAt(ErrorCategory::unsupported, DecodeErrorCode::unsupportedFeature,
                          member(node, "extensions"), "node extensions are not supported");
        }
        for (std::string_view transform : {"matrix", "translation", "rotation", "scale",
                                           "skin", "children", "weights", "camera"}) {
            if (hasMember(node, transform)) {
                return failAt(ErrorCategory::unsupported, DecodeErrorCode::unsupportedFeature,
                              member(node, transform), "node transforms, skins, and hierarchies are not supported");
            }
        }
        std::size_t meshIndex = 0;
        const JsonValue* meshIndexValue = member(node, "mesh");
        if (meshIndexValue == nullptr || !integerValue(*meshIndexValue, meshIndex) || meshIndex != 0U) {
            return failAt(ErrorCategory::format, DecodeErrorCode::missingRequiredProperty,
                          meshIndexValue, "node.mesh must select mesh 0");
        }

        const JsonValue& meshValue = meshes->arrayValue[0];
        if (hasMember(meshValue, "weights") || hasMember(meshValue, "extensions")) {
            return failAt(ErrorCategory::unsupported, DecodeErrorCode::unsupportedFeature,
                          &meshValue, "mesh morph weights and extensions are not supported");
        }
        const JsonValue* primitivesValue = member(meshValue, "primitives");
        if (primitivesValue == nullptr || primitivesValue->kind != JsonValue::Kind::array ||
            primitivesValue->arrayValue.empty()) {
            return failAt(ErrorCategory::format, DecodeErrorCode::missingRequiredProperty,
                          primitivesValue, "mesh must contain at least one primitive");
        }
        if (primitivesValue->arrayValue.size() > limits.maxPrimitives) {
            return failAt(ErrorCategory::resource, DecodeErrorCode::primitiveLimitExceeded,
                          primitivesValue, "primitive count exceeds the configured limit");
        }

        std::vector<PrimitiveInfo> primitives;
        std::vector<Diagnostic> diagnostics;
        primitives.reserve(primitivesValue->arrayValue.size());
        for (const JsonValue& primitiveValue : primitivesValue->arrayValue) {
            if (primitiveValue.kind != JsonValue::Kind::object ||
                hasMember(primitiveValue, "targets") || hasMember(primitiveValue, "extensions")) {
                return failAt(hasMember(primitiveValue, "targets") || hasMember(primitiveValue, "extensions")
                                  ? ErrorCategory::unsupported
                                  : ErrorCategory::format,
                              hasMember(primitiveValue, "targets") || hasMember(primitiveValue, "extensions")
                                  ? DecodeErrorCode::unsupportedFeature
                                  : DecodeErrorCode::wrongPropertyType,
                              &primitiveValue, "primitive morph targets and extensions are not supported");
            }
            std::size_t mode = kTriangles;
            if (!optionalInteger(primitiveValue, "mode", kTriangles, mode)) {
                return failAt(ErrorCategory::format, DecodeErrorCode::wrongPropertyType,
                              member(primitiveValue, "mode"), "primitive mode must be an integer");
            }
            if (mode != kTriangles) {
                return failAt(ErrorCategory::unsupported,
                              DecodeErrorCode::unsupportedPrimitiveMode,
                              member(primitiveValue, "mode"), "only TRIANGLES primitives are supported");
            }
            const JsonValue* attributes = member(primitiveValue, "attributes");
            if (attributes == nullptr || attributes->kind != JsonValue::Kind::object) {
                return failAt(ErrorCategory::format, DecodeErrorCode::missingRequiredProperty,
                              attributes, "primitive POSITION attributes object is required");
            }
            PrimitiveInfo primitive;
            primitive.jsonOffset = primitiveValue.offset;
            const JsonValue* positionValue = member(*attributes, "POSITION");
            if (positionValue == nullptr || !integerValue(*positionValue, primitive.positionAccessor) ||
                primitive.positionAccessor >= accessors.size()) {
                return failAt(ErrorCategory::format, DecodeErrorCode::missingRequiredProperty,
                              positionValue, "primitive POSITION accessor is invalid");
            }
            for (const auto& attribute : attributes->objectValue) {
                if (attribute.first != "POSITION") {
                    std::size_t ignoredAccessor = 0;
                    if (!integerValue(attribute.second, ignoredAccessor) ||
                        ignoredAccessor >= accessors.size()) {
                        return failAt(ErrorCategory::format, DecodeErrorCode::wrongPropertyType,
                                      &attribute.second,
                                      "visual attribute accessor index is invalid");
                    }
                    diagnostics.push_back({DiagnosticCode::ignoredAttribute,
                                           20U + attribute.second.offset,
                                           "optional visual vertex attribute was ignored"});
                }
            }
            const JsonValue* indicesValue = member(primitiveValue, "indices");
            if (indicesValue != nullptr) {
                primitive.hasIndices = true;
                if (!integerValue(*indicesValue, primitive.indexAccessor) ||
                    primitive.indexAccessor >= accessors.size()) {
                    return failAt(ErrorCategory::format, DecodeErrorCode::wrongPropertyType,
                                  indicesValue, "primitive index accessor is invalid");
                }
            }
            if (hasMember(primitiveValue, "material")) {
                std::size_t ignoredMaterial = 0;
                if (!integerValue(*member(primitiveValue, "material"), ignoredMaterial)) {
                    return failAt(ErrorCategory::format, DecodeErrorCode::wrongPropertyType,
                                  member(primitiveValue, "material"), "material index must be an integer");
                }
                diagnostics.push_back({DiagnosticCode::ignoredMaterial,
                                       20U + member(primitiveValue, "material")->offset,
                                       "primitive material was ignored"});
            }
            primitives.push_back(primitive);
        }

        std::vector<std::size_t> accessorVertexBase(accessors.size(),
                                                    std::numeric_limits<std::size_t>::max());
        std::vector<std::size_t> positionAccessorOrder;
        positionAccessorOrder.reserve(primitives.size());
        std::size_t totalVertices = 0;
        std::size_t totalTriangles = 0;
        for (const PrimitiveInfo& primitive : primitives) {
            const AccessorInfo& position = accessors[primitive.positionAccessor];
            if (position.componentType != kFloat || position.type != "VEC3" || position.normalized) {
                return decodeFailure(ErrorCategory::unsupported,
                                     DecodeErrorCode::unsupportedAccessor,
                                     20U + position.jsonOffset,
                                     "POSITION must be a non-normalized FLOAT VEC3 accessor");
            }
            std::size_t positionStart = 0;
            std::size_t positionStride = 0;
            if (!accessorRange(position, views[position.view], 12U, bufferByteLength,
                               positionStart, positionStride)) {
                return decodeFailure(ErrorCategory::format,
                                     views[position.view].stride == 0U
                                         ? DecodeErrorCode::accessorOutOfBounds
                                         : DecodeErrorCode::invalidStride,
                                     20U + position.jsonOffset,
                                     "POSITION accessor range or stride is invalid");
            }
            if (accessorVertexBase[primitive.positionAccessor] ==
                std::numeric_limits<std::size_t>::max()) {
                accessorVertexBase[primitive.positionAccessor] = totalVertices;
                positionAccessorOrder.push_back(primitive.positionAccessor);
                if (!checkedAdd(totalVertices, position.count, totalVertices)) {
                    return decodeFailure(ErrorCategory::format,
                                         DecodeErrorCode::integerOverflow,
                                         20U + position.jsonOffset,
                                         "combined vertex count overflows this platform");
                }
                if (totalVertices > limits.maxVertices) {
                    return decodeFailure(ErrorCategory::resource,
                                         DecodeErrorCode::vertexLimitExceeded,
                                         20U + position.jsonOffset,
                                         "vertex count exceeds the configured limit");
                }
            }
            std::size_t elementCount = position.count;
            if (primitive.hasIndices) {
                const AccessorInfo& indices = accessors[primitive.indexAccessor];
                if (indices.type != "SCALAR" || indices.normalized ||
                    (indices.componentType != kUnsignedByte &&
                     indices.componentType != kUnsignedShort &&
                     indices.componentType != kUnsignedInt)) {
                    return decodeFailure(ErrorCategory::unsupported,
                                         DecodeErrorCode::unsupportedAccessor,
                                         20U + indices.jsonOffset,
                                         "indices must be non-normalized unsigned SCALAR values");
                }
                std::size_t indexStart = 0;
                std::size_t indexStride = 0;
                if (!accessorRange(indices, views[indices.view],
                                   componentSize(indices.componentType), bufferByteLength,
                                   indexStart, indexStride)) {
                    return decodeFailure(ErrorCategory::format,
                                         views[indices.view].stride == 0U
                                             ? DecodeErrorCode::accessorOutOfBounds
                                             : DecodeErrorCode::invalidStride,
                                         20U + indices.jsonOffset,
                                         "index accessor range or stride is invalid");
                }
                elementCount = indices.count;
            }
            if (elementCount == 0U || elementCount % 3U != 0U) {
                return decodeFailure(ErrorCategory::topology,
                                     DecodeErrorCode::unsupportedAccessor,
                                     20U + primitive.jsonOffset,
                                     "triangle element count must be a positive multiple of three");
            }
            if (!checkedAdd(totalTriangles, elementCount / 3U, totalTriangles)) {
                return decodeFailure(ErrorCategory::format, DecodeErrorCode::integerOverflow,
                                     20U + primitive.jsonOffset,
                                     "combined triangle count overflows this platform");
            }
            if (totalTriangles > limits.maxTriangles) {
                return decodeFailure(ErrorCategory::resource,
                                     DecodeErrorCode::triangleLimitExceeded,
                                     20U + primitive.jsonOffset,
                                     "triangle count exceeds the configured limit");
            }
        }
        if (totalVertices == 0U || totalVertices == std::numeric_limits<VertexId>::max() ||
            totalTriangles == std::numeric_limits<FaceId>::max()) {
            return decodeFailure(ErrorCategory::resource, DecodeErrorCode::vertexLimitExceeded,
                                 20U, "decoded stable ID range is exhausted");
        }

        std::vector<Vertex> vertices;
        std::vector<Face> faces;
        vertices.reserve(totalVertices);
        faces.reserve(totalTriangles);
        for (const std::size_t accessorIndex : positionAccessorOrder) {
            const AccessorInfo& position = accessors[accessorIndex];
            std::size_t start = 0;
            std::size_t stride = 0;
            if (!accessorRange(position, views[position.view], 12U, bufferByteLength, start, stride)) {
                return decodeFailure(ErrorCategory::internal, DecodeErrorCode::internalError,
                                     20U + position.jsonOffset,
                                     "validated POSITION range changed unexpectedly");
            }
            for (std::size_t element = 0; element < position.count; ++element) {
                const std::size_t offset = start + element * stride;
                const float x = readFloat(bin, offset);
                const float y = readFloat(bin, offset + 4U);
                const float z = readFloat(bin, offset + 8U);
                if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
                    return decodeFailure(ErrorCategory::topology,
                                         DecodeErrorCode::nonFinitePosition,
                                         binHeaderEnd + offset,
                                         "POSITION values must be finite");
                }
                vertices.push_back({static_cast<VertexId>(vertices.size()) + 1U,
                                    {static_cast<double>(x), static_cast<double>(y),
                                     static_cast<double>(z)}});
            }
        }

        for (const PrimitiveInfo& primitive : primitives) {
            const AccessorInfo& position = accessors[primitive.positionAccessor];
            const std::size_t base = accessorVertexBase[primitive.positionAccessor];
            std::size_t count = position.count;
            std::size_t indexStart = 0;
            std::size_t indexStride = 0;
            const AccessorInfo* indices = nullptr;
            if (primitive.hasIndices) {
                indices = &accessors[primitive.indexAccessor];
                count = indices->count;
                if (!accessorRange(*indices, views[indices->view],
                                   componentSize(indices->componentType), bufferByteLength,
                                   indexStart, indexStride)) {
                    return decodeFailure(ErrorCategory::internal, DecodeErrorCode::internalError,
                                         20U + indices->jsonOffset,
                                         "validated index range changed unexpectedly");
                }
            }
            for (std::size_t triangle = 0; triangle < count / 3U; ++triangle) {
                Face face{static_cast<FaceId>(faces.size()) + 1U, {}};
                face.vertices.reserve(3U);
                for (std::size_t corner = 0; corner < 3U; ++corner) {
                    const std::size_t element = triangle * 3U + corner;
                    std::size_t index = element;
                    std::size_t inputOffset = 20U + primitive.jsonOffset;
                    if (indices != nullptr) {
                        const std::size_t offset = indexStart + element * indexStride;
                        inputOffset = binHeaderEnd + offset;
                        if (indices->componentType == kUnsignedByte) {
                            index = bin[offset];
                        } else if (indices->componentType == kUnsignedShort) {
                            index = readU16(bin, offset);
                        } else {
                            index = readU32(bin, offset);
                        }
                    }
                    if (index >= position.count) {
                        return decodeFailure(ErrorCategory::topology,
                                             DecodeErrorCode::invalidIndex, inputOffset,
                                             "triangle index exceeds its POSITION accessor count");
                    }
                    face.vertices.push_back(static_cast<VertexId>(base + index) + 1U);
                }
                if (face.vertices[0] == face.vertices[1] ||
                    face.vertices[0] == face.vertices[2] ||
                    face.vertices[1] == face.vertices[2]) {
                    return decodeFailure(ErrorCategory::topology,
                                         DecodeErrorCode::invalidIndex,
                                         20U + primitive.jsonOffset,
                                         "triangle indices must reference three distinct vertices");
                }
                faces.push_back(std::move(face));
            }
        }

        Mesh candidate = MeshGlbAccess::make(std::move(vertices), std::move(faces));
        if (!candidate.validate().ok) {
            return decodeFailure(ErrorCategory::topology,
                                 DecodeErrorCode::meshValidationFailed, 20U,
                                 "decoded GLB mesh failed full validation");
        }
        return {true, std::move(candidate), std::move(diagnostics), {}};
    } catch (const std::bad_alloc&) {
        return decodeFailure(ErrorCategory::allocation, DecodeErrorCode::allocationFailed, 0,
                             "allocation failed while decoding GLB");
    } catch (...) {
        return decodeFailure(ErrorCategory::internal, DecodeErrorCode::internalError, 0,
                             "unexpected failure while decoding GLB");
    }
}

InstallResult installGlb(Mesh& liveMesh, std::span<const std::uint8_t> bytes,
                         LoadLimits limits) noexcept {
    static_assert(noexcept(std::declval<Mesh&>() = std::declval<Mesh&&>()),
                  "atomic GLB install requires non-throwing Mesh move assignment");
    DecodeResult decoded = decodeGlb(bytes, limits);
    if (!decoded.ok) {
        return {false, {}, decoded.error};
    }
    liveMesh = std::move(decoded.mesh);
    return {true, std::move(decoded.diagnostics), {}};
}

}  // namespace octopoly::glb
