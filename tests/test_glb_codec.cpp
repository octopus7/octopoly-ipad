#include "octopoly/glb_codec.hpp"
#include "octopoly/mesh.hpp"
#include "octopoly/project_codec.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using octopoly::Mesh;
using octopoly::glb::DecodeErrorCode;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename A, typename B>
void requireEqual(const A& actual, const B& expected, const std::string& message) {
    if (!(actual == expected)) throw std::runtime_error(message);
}

std::uint32_t readU32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    require(offset + 4U <= bytes.size(), "u32 read must be in bounds");
    std::uint32_t value = 0;
    for (unsigned byte = 0; byte < 4U; ++byte) {
        value |= static_cast<std::uint32_t>(bytes[offset + byte]) << (byte * 8U);
    }
    return value;
}

void writeU32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    require(offset + 4U <= bytes.size(), "u32 write must be in bounds");
    for (unsigned byte = 0; byte < 4U; ++byte) {
        bytes[offset + byte] = static_cast<std::uint8_t>((value >> (byte * 8U)) & 0xffU);
    }
}

std::uint32_t crc32(const std::uint8_t* data, std::size_t size) {
    std::uint32_t crc = 0xffffffffU;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (unsigned bit = 0; bit < 8U; ++bit) {
            const std::uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xedb88320U & mask);
        }
    }
    return ~crc;
}

void appendU16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
}

void appendU32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (unsigned byte = 0; byte < 4U; ++byte) {
        bytes.push_back(static_cast<std::uint8_t>((value >> (byte * 8U)) & 0xffU));
    }
}

void appendFloat(std::vector<std::uint8_t>& bytes, float value) {
    appendU32(bytes, std::bit_cast<std::uint32_t>(value));
}

std::vector<std::uint8_t> makeGlb(std::string json, std::vector<std::uint8_t> bin) {
    const std::size_t actualBinLength = bin.size();
    while (json.size() % 4U != 0U) json.push_back(' ');
    while (bin.size() % 4U != 0U) bin.push_back(0U);
    require(json.size() <= UINT32_MAX && bin.size() <= UINT32_MAX,
            "test GLB chunks must fit uint32");
    std::vector<std::uint8_t> bytes;
    appendU32(bytes, 0x46546c67U);
    appendU32(bytes, 2U);
    appendU32(bytes, static_cast<std::uint32_t>(12U + 8U + json.size() + 8U + bin.size()));
    appendU32(bytes, static_cast<std::uint32_t>(json.size()));
    appendU32(bytes, 0x4e4f534aU);
    bytes.insert(bytes.end(), json.begin(), json.end());
    appendU32(bytes, static_cast<std::uint32_t>(bin.size()));
    appendU32(bytes, 0x004e4942U);
    bytes.insert(bytes.end(), bin.begin(), bin.end());
    require(actualBinLength <= bin.size(), "test fixture actual BIN length");
    return bytes;
}

std::string triangleJson(std::size_t binLength, std::uint32_t indexComponent,
                         std::size_t indexBytes, bool indexed = true,
                         std::string_view extraAttribute = {},
                         std::string_view primitiveExtra = {},
                         std::string_view nodeExtra = {},
                         std::string_view bufferExtra = {},
                         std::string_view positionExtra = {}) {
    std::string json =
        "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,"
        "\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0";
    json += nodeExtra;
    json += "}],\"meshes\":[{\"primitives\":[{\"attributes\":{";
    if (!extraAttribute.empty()) {
        json += extraAttribute;
        json += ',';
    }
    json += "\"POSITION\":0}";
    if (indexed) json += ",\"indices\":1";
    json += primitiveExtra;
    json += "}]}],\"buffers\":[{\"byteLength\":" + std::to_string(binLength);
    json += bufferExtra;
    json += "}],\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36}";
    if (indexed) {
        json += ",{\"buffer\":0,\"byteOffset\":36,\"byteLength\":" +
                std::to_string(indexBytes) + "}";
    }
    json += "],\"accessors\":[{\"bufferView\":0,\"componentType\":5126,"
            "\"count\":3,\"type\":\"VEC3\"";
    json += positionExtra;
    json += '}';
    if (indexed) {
        json += ",{\"bufferView\":1,\"componentType\":" +
                std::to_string(indexComponent) +
                ",\"count\":3,\"type\":\"SCALAR\"}";
    }
    json += "]}";
    return json;
}

std::vector<std::uint8_t> triangleBin(std::uint32_t componentType,
                                      std::array<std::uint32_t, 3> indices = {0, 1, 2}) {
    std::vector<std::uint8_t> bin;
    for (const std::array<float, 3> position :
         {std::array<float, 3>{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}) {
        for (float value : position) appendFloat(bin, value);
    }
    for (std::uint32_t index : indices) {
        if (componentType == 5121U) bin.push_back(static_cast<std::uint8_t>(index));
        else if (componentType == 5123U) appendU16(bin, static_cast<std::uint16_t>(index));
        else appendU32(bin, index);
    }
    return bin;
}

std::vector<std::uint8_t> triangleFixture(std::uint32_t componentType,
                                          std::array<std::uint32_t, 3> indices = {0, 1, 2},
                                          std::string_view extraAttribute = {},
                                          std::string_view primitiveExtra = {},
                                          std::string_view nodeExtra = {},
                                          std::string_view bufferExtra = {},
                                          std::string_view positionExtra = {}) {
    auto bin = triangleBin(componentType, indices);
    const std::size_t binLength = bin.size();
    const std::size_t indexBytes = componentType == 5121U ? 3U :
                                   componentType == 5123U ? 6U : 12U;
    const std::string json = triangleJson(binLength, componentType, indexBytes, true,
                                          extraAttribute, primitiveExtra, nodeExtra,
                                          bufferExtra, positionExtra);
    return makeGlb(json, std::move(bin));
}

std::vector<std::uint8_t> sparseReferenceFixture(
    std::size_t positionCount, std::array<std::uint32_t, 3> indices) {
    std::vector<std::uint8_t> bin;
    bin.reserve(positionCount * 12U + 12U);
    for (std::size_t index = 0; index < positionCount; ++index) {
        appendFloat(bin, static_cast<float>(index));
        appendFloat(bin, static_cast<float>(index % 2U));
        appendFloat(bin, 0.0F);
    }
    for (std::uint32_t index : indices) appendU32(bin, index);
    const std::size_t positionBytes = positionCount * 12U;
    const std::size_t binLength = bin.size();
    const std::string json =
        "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,"
        "\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}],"
        "\"buffers\":[{\"byteLength\":" + std::to_string(binLength) + "}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteLength\":" +
        std::to_string(positionBytes) + "},{\"buffer\":0,\"byteOffset\":" +
        std::to_string(positionBytes) + " ,\"byteLength\":12}],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":" +
        std::to_string(positionCount) +
        ",\"type\":\"VEC3\"},{\"bufferView\":1,\"componentType\":5125,"
        "\"count\":3,\"type\":\"SCALAR\"}]}";
    return makeGlb(json, std::move(bin));
}

std::string encodedJson(const std::vector<std::uint8_t>& bytes) {
    const std::size_t jsonLength = readU32(bytes, 12U);
    require(20U + jsonLength <= bytes.size(), "encoded JSON chunk must be in bounds");
    return std::string(bytes.begin() + 20,
                       bytes.begin() + static_cast<std::ptrdiff_t>(20U + jsonLength));
}

std::string prependRootMembers(std::string json, std::size_t count) {
    require(!json.empty() && json.front() == '{', "root member fixture must be an object");
    std::string members;
    members.reserve(count * 20U);
    for (std::size_t index = 0; index < count; ++index) {
        members += "\"unused" + std::to_string(index) + "\":0,";
    }
    json.insert(1U, members);
    return json;
}

std::string replaceOnce(std::string text, std::string_view oldValue,
                        std::string_view newValue) {
    const std::size_t offset = text.find(oldValue);
    require(offset != std::string::npos, "fixture replacement token must exist");
    text.replace(offset, oldValue.size(), newValue);
    return text;
}

void requireDecodeError(const std::vector<std::uint8_t>& bytes, DecodeErrorCode code,
                        const std::string& label,
                        octopoly::glb::LoadLimits limits = {}) {
    const auto result = octopoly::glb::decodeGlb(bytes, limits);
    require(!result.ok, label + " must fail");
    requireEqual(result.error.code, code, label + " error code");
    require(result.error.category != octopoly::glb::ErrorCategory::none,
            label + " error category");
    require(!result.error.message.empty(), label + " error message");
    require(result.error.offset <= bytes.size(), label + " error offset");
}

void deterministicCubeExportHasExactStructure() {
    const Mesh cube = Mesh::makeDefaultCube();
    const auto first = octopoly::glb::encodeGlb(cube);
    const auto second = octopoly::glb::encodeGlb(cube);
    require(first.ok && second.ok, "cube GLB exports must succeed");
    requireEqual(first.bytes, second.bytes, "repeated GLB export must be byte-identical");
    requireEqual(readU32(first.bytes, 0), 0x46546c67U, "GLB magic");
    requireEqual(readU32(first.bytes, 4), 2U, "GLB version");
    requireEqual(static_cast<std::size_t>(readU32(first.bytes, 8)), first.bytes.size(),
                 "GLB declared length");
    const std::size_t jsonLength = readU32(first.bytes, 12);
    requireEqual(readU32(first.bytes, 16), 0x4e4f534aU, "first chunk JSON");
    require(jsonLength % 4U == 0U, "JSON chunk alignment");
    const std::size_t binHeader = 20U + jsonLength;
    requireEqual(readU32(first.bytes, binHeader + 4U), 0x004e4942U, "second chunk BIN");
    const std::size_t binLength = readU32(first.bytes, binHeader);
    require(binLength % 4U == 0U, "BIN chunk alignment");
    requireEqual(binHeader + 8U + binLength, first.bytes.size(), "exact chunk bounds");
    const std::string json(first.bytes.begin() + 20,
                           first.bytes.begin() + static_cast<std::ptrdiff_t>(binHeader));
    require(json.find("\"scenes\":[{\"nodes\":[0]}]") != std::string::npos,
            "one deterministic scene");
    require(json.find("\"nodes\":[{\"mesh\":0}]") != std::string::npos,
            "one deterministic node");
    require(json.find("\"POSITION\":0") != std::string::npos, "POSITION accessor");
    require(json.find("\"componentType\":5121") != std::string::npos,
            "cube uses smallest uint8 indices");
    require(json.find("\"min\":[-1,-1,-1]") != std::string::npos,
            "POSITION minimum");
    require(json.find("\"max\":[1,1,1]") != std::string::npos,
            "POSITION maximum");
    const std::size_t finalBrace = json.rfind('}');
    require(finalBrace != std::string::npos, "JSON has a root closing brace");
    require(std::all_of(json.begin() + static_cast<std::ptrdiff_t>(finalBrace + 1U),
                        json.end(), [](char value) { return value == ' '; }),
            "any JSON chunk padding uses spaces");
}

void exportIndexWidthUsesMaximumReferencedStorageIndex() {
    struct Boundary {
        std::size_t positionCount;
        std::uint32_t maximumIndex;
        std::uint32_t expectedComponent;
    };
    for (const Boundary boundary : {
             Boundary{300U, 2U, 5121U}, Boundary{256U, 255U, 5121U},
             Boundary{257U, 256U, 5123U}, Boundary{65'536U, 65'535U, 5123U},
             Boundary{65'537U, 65'536U, 5125U}}) {
        const auto decoded = octopoly::glb::decodeGlb(
            sparseReferenceFixture(boundary.positionCount,
                                   {0U, 1U, boundary.maximumIndex}));
        require(decoded.ok, "sparse-reference source fixture must decode");
        requireEqual(decoded.mesh.vertices().size(), boundary.positionCount,
                     "source fixture preserves every POSITION element");
        const auto encoded = octopoly::glb::encodeGlb(decoded.mesh);
        require(encoded.ok, "sparse-reference mesh must export");
        const std::string json = encodedJson(encoded.bytes);
        require(json.find("\"componentType\":" +
                          std::to_string(boundary.expectedComponent) +
                          ",\"count\":3,\"type\":\"SCALAR\"") != std::string::npos,
                "export chooses index width from maximum emitted storage index");
        require(json.find("\"count\":" + std::to_string(boundary.positionCount) +
                          ",\"type\":\"VEC3\"") != std::string::npos,
                "export POSITION accessor retains all stored vertices");
    }
}

void cubeAndEditedRoundTripsAreTriangulatedAndDeterministic() {
    for (bool edited : {false, true}) {
        Mesh source = Mesh::makeDefaultCube();
        if (edited) {
            require(source.insetFace(source.faces().front().id, 0.2).ok,
                    "edited fixture inset");
            require(source.extrudeFace(source.faces()[1].id, {0, 0, -0.25}).ok,
                    "edited fixture extrude");
        }
        const auto encoded = octopoly::glb::encodeGlb(source);
        require(encoded.ok, "round-trip source export");
        auto decoded = octopoly::glb::decodeGlb(encoded.bytes);
        require(decoded.ok, "exported GLB import");
        require(decoded.mesh.validate().ok, "decoded mesh validates");
        requireEqual(decoded.mesh.vertices().size(), source.vertices().size(),
                     "vertex storage order/count preserved");
        requireEqual(decoded.mesh.faces().size(), source.triangulate().size(),
                     "polygons intentionally become triangle faces");
        requireEqual(decoded.mesh.revision(), std::uint64_t{0}, "GLB import revision starts at zero");
        requireEqual(decoded.mesh.nextVertexId(),
                     static_cast<octopoly::VertexId>(decoded.mesh.vertices().size() + 1U),
                     "future vertex ID follows imported vertices");
        requireEqual(decoded.mesh.nextFaceId(),
                     static_cast<octopoly::FaceId>(decoded.mesh.faces().size() + 1U),
                     "future face ID follows imported triangles");
        const auto reencoded = octopoly::glb::encodeGlb(decoded.mesh);
        require(reencoded.ok, "imported GLB re-export");
        requireEqual(reencoded.bytes, encoded.bytes, "GLB round-trip bytes are deterministic");
        const auto edit = decoded.mesh.extrudeFace(decoded.mesh.faces().front().id,
                                                   {0, 0, -0.125});
        require(edit.ok, "future edit after GLB import");
        requireEqual(edit.createdVertices.front(),
                     static_cast<octopoly::VertexId>(source.vertices().size() + 1U),
                     "future stable IDs remain valid");
    }
}

void importsUnsignedIndexWidthsAndNonIndexedTriangles() {
    for (std::uint32_t component : {5121U, 5123U, 5125U}) {
        const auto decoded = octopoly::glb::decodeGlb(triangleFixture(component));
        require(decoded.ok, "supported unsigned index fixture: " +
                                std::string(decoded.error.message));
        requireEqual(decoded.mesh.vertices().size(), std::size_t{3}, "indexed vertex count");
        requireEqual(decoded.mesh.faces().size(), std::size_t{1}, "indexed face count");
        requireEqual(decoded.mesh.faces()[0].vertices,
                     std::vector<octopoly::VertexId>({1, 2, 3}), "indexed winding");
    }
    auto bin = triangleBin(5121U);
    bin.resize(36U);
    const auto bytes = makeGlb(triangleJson(36U, 5121U, 0U, false), std::move(bin));
    const auto decoded = octopoly::glb::decodeGlb(bytes);
    require(decoded.ok, "non-indexed triangle fixture");
    requireEqual(decoded.mesh.faces()[0].vertices,
                 std::vector<octopoly::VertexId>({1, 2, 3}), "non-indexed winding");
}

void importsInterleavedAndMultiplePrimitivesWithAccessorLocalSharing() {
    std::vector<std::uint8_t> interleaved;
    for (const std::array<float, 3> position :
         {std::array<float, 3>{2, 3, 4}, {5, 6, 7}, {8, 9, 10}}) {
        appendU32(interleaved, 0xdeadbeefU);
        for (float value : position) appendFloat(interleaved, value);
    }
    const std::string interleavedJson =
        "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,"
        "\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0}}]}],"
        "\"buffers\":[{\"byteLength\":48}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteLength\":48,\"byteStride\":16}],"
        "\"accessors\":[{\"bufferView\":0,\"byteOffset\":4,"
        "\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"}]}";
    auto decoded = octopoly::glb::decodeGlb(makeGlb(interleavedJson, interleaved));
    require(decoded.ok, "interleaved POSITION fixture");
    requireEqual(decoded.mesh.vertices()[0].position, octopoly::Vec3{2, 3, 4},
                 "interleaved accessor offset/stride");

    std::vector<std::uint8_t> bin;
    for (const std::array<float, 3> position :
         {std::array<float, 3>{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}) {
        for (float value : position) appendFloat(bin, value);
    }
    for (std::uint8_t index : {0, 1, 2, 0, 2, 3}) bin.push_back(index);
    const std::string multipleJson =
        "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,"
        "\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
        "\"meshes\":[{\"primitives\":["
        "{\"attributes\":{\"POSITION\":0},\"indices\":1},"
        "{\"attributes\":{\"POSITION\":0},\"indices\":2}]}],"
        "\"buffers\":[{\"byteLength\":54}],\"bufferViews\":["
        "{\"buffer\":0,\"byteLength\":48},"
        "{\"buffer\":0,\"byteOffset\":48,\"byteLength\":3},"
        "{\"buffer\":0,\"byteOffset\":51,\"byteLength\":3}],"
        "\"accessors\":["
        "{\"bufferView\":0,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"},"
        "{\"bufferView\":1,\"componentType\":5121,\"count\":3,\"type\":\"SCALAR\"},"
        "{\"bufferView\":2,\"componentType\":5121,\"count\":3,\"type\":\"SCALAR\"}]}";
    decoded = octopoly::glb::decodeGlb(makeGlb(multipleJson, bin));
    require(decoded.ok, "multiple primitive fixture");
    requireEqual(decoded.mesh.vertices().size(), std::size_t{4},
                 "shared POSITION accessor is decoded once");
    requireEqual(decoded.mesh.faces().size(), std::size_t{2}, "primitives combine in order");
}

void ignoredVisualDataProducesDeterministicWarnings() {
    const auto bytes = triangleFixture(5121U, {0, 1, 2},
                                       "\"NORMAL\":0,\"TEXCOORD_0\":0",
                                       ",\"material\":7");
    const auto first = octopoly::glb::decodeGlb(bytes);
    const auto second = octopoly::glb::decodeGlb(bytes);
    require(first.ok && second.ok, "visual-data fixture imports: " +
                                        std::string(first.error.message));
    requireEqual(first.diagnostics, second.diagnostics, "warnings are deterministic");
    requireEqual(first.diagnostics.size(), std::size_t{3},
                 "NORMAL, TEXCOORD, and material each warn");
    requireEqual(first.diagnostics[0].code,
                 octopoly::glb::DiagnosticCode::ignoredAttribute, "NORMAL warning");
    requireEqual(first.diagnostics[2].code,
                 octopoly::glb::DiagnosticCode::ignoredMaterial, "material warning");
}

void malformedContainerJsonAndPaddingAreRejected() {
    const auto valid = triangleFixture(5121U);
    auto changed = valid;
    changed[0] = 'X';
    requireDecodeError(changed, DecodeErrorCode::badMagic, "bad magic");
    changed = valid;
    writeU32(changed, 4, 1U);
    requireDecodeError(changed, DecodeErrorCode::unsupportedVersion, "bad version");
    changed = valid;
    writeU32(changed, 8, static_cast<std::uint32_t>(changed.size() - 1U));
    requireDecodeError(changed, DecodeErrorCode::trailingBytes, "short declared length");
    changed = valid;
    writeU32(changed, 8, static_cast<std::uint32_t>(changed.size() + 1U));
    requireDecodeError(changed, DecodeErrorCode::truncated, "long declared length");
    changed = valid;
    writeU32(changed, 16, 0x004e4942U);
    requireDecodeError(changed, DecodeErrorCode::badChunkOrder, "BIN before JSON");
    const std::size_t binHeader = 20U + readU32(valid, 12);
    changed = valid;
    writeU32(changed, binHeader + 4U, 0x12345678U);
    requireDecodeError(changed, DecodeErrorCode::badChunkType, "bad BIN type");
    changed = valid;
    changed.push_back(0U);
    writeU32(changed, 8, static_cast<std::uint32_t>(changed.size()));
    requireDecodeError(changed, DecodeErrorCode::trailingBytes, "trailing byte");

    const auto syntax = makeGlb("{\"asset\":", {});
    requireDecodeError(syntax, DecodeErrorCode::jsonSyntax, "truncated JSON");
    const auto duplicate = makeGlb(
        "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scene\":0}", {});
    requireDecodeError(duplicate, DecodeErrorCode::jsonDuplicateKey, "duplicate key");
    const auto escapedDuplicate = makeGlb(
        "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"sc\\u0065ne\":0}", {});
    requireDecodeError(escapedDuplicate, DecodeErrorCode::jsonDuplicateKey,
                       "escaped-equivalent duplicate key");
    auto badJsonPadding = makeGlb("{}", {});
    const std::size_t jsonLength = readU32(badJsonPadding, 12);
    badJsonPadding[20U + jsonLength - 1U] = 0U;
    requireDecodeError(badJsonPadding, DecodeErrorCode::jsonTrailingGarbage,
                       "NUL JSON padding");

    auto badBinPadding = valid;
    require(readU32(badBinPadding, binHeader) >= 40U, "fixture has padded BIN");
    badBinPadding.back() = 0x7fU;
    requireDecodeError(badBinPadding, DecodeErrorCode::invalidChunkPadding,
                       "nonzero BIN padding");
}

void exactIntegerPropertiesRejectRoundedFractionsExponentsAndOverflow() {
    const auto rejectChangedJson = [](std::string_view oldValue, std::string_view newValue,
                                      DecodeErrorCode expectedCode,
                                      std::string_view label) {
        auto bin = triangleBin(5121U);
        const std::size_t binLength = bin.size();
        const std::string changed = replaceOnce(triangleJson(binLength, 5121U, 3U),
                                                oldValue, newValue);
        requireDecodeError(makeGlb(changed, std::move(bin)),
                           expectedCode, std::string(label));
    };

    rejectChangedJson("\"POSITION\":0", "\"POSITION\":0e0",
                      DecodeErrorCode::missingRequiredProperty,
                      "POSITION accessor exponent");
    rejectChangedJson("\"indices\":1", "\"indices\":1e0",
                      DecodeErrorCode::wrongPropertyType, "index accessor exponent");
    rejectChangedJson("\"buffer\":0", "\"buffer\":0e0",
                      DecodeErrorCode::wrongPropertyType, "bufferView buffer exponent");
    rejectChangedJson("\"componentType\":5126", "\"componentType\":5126.0000000000001",
                      DecodeErrorCode::wrongPropertyType,
                      "accessor component rounded fraction");
    rejectChangedJson("\"count\":3", "\"count\":3.0000000000000001",
                      DecodeErrorCode::wrongPropertyType,
                      "accessor count rounded fraction");
    rejectChangedJson("\"byteLength\":39", "\"byteLength\":39.000000000000001",
                      DecodeErrorCode::wrongPropertyType,
                      "buffer byteLength rounded fraction");

    requireDecodeError(triangleFixture(5121U, {0, 1, 2}, {},
                                       ",\"mode\":4.0000000000000001"),
                       DecodeErrorCode::wrongPropertyType,
                       "primitive mode rounded fraction");
    const auto exactLargeMaterial = octopoly::glb::decodeGlb(
        triangleFixture(5121U, {0, 1, 2}, {},
                        ",\"material\":9007199254740993"));
    require(exactLargeMaterial.ok,
            "exact material integer above double precision must remain valid on size_t");
    requireEqual(exactLargeMaterial.diagnostics.size(), std::size_t{1},
                 "exact large material still produces one ignored-material diagnostic");
    if constexpr (sizeof(std::size_t) == sizeof(std::uint64_t)) {
        const std::string maximumMaterial =
            ",\"material\":" + std::to_string(std::numeric_limits<std::size_t>::max());
        const auto exactMaximum = octopoly::glb::decodeGlb(
            triangleFixture(5121U, {0, 1, 2}, {}, maximumMaterial));
        require(exactMaximum.ok,
                "maximum size_t material index must parse without double narrowing");
    }
    requireDecodeError(triangleFixture(5121U, {0, 1, 2}, {},
                                       ",\"material\":18446744073709551616"),
                       DecodeErrorCode::wrongPropertyType,
                       "material integer beyond size_t range");
}

void ignoredVisualAttributesRequireValidAccessorIndices() {
    for (std::string_view attribute : {
             "\"NORMAL\":{}", "\"NORMAL\":\"0\"", "\"NORMAL\":0.5",
             "\"NORMAL\":0e0", "\"NORMAL\":-1", "\"NORMAL\":2"}) {
        requireDecodeError(triangleFixture(5121U, {0, 1, 2}, attribute),
                           DecodeErrorCode::wrongPropertyType,
                           "malformed ignored visual attribute " + std::string(attribute));
    }
    const auto valid = octopoly::glb::decodeGlb(
        triangleFixture(5121U, {0, 1, 2}, "\"NORMAL\":1"));
    require(valid.ok, "in-range exact ignored attribute accessor must decode");
    requireEqual(valid.diagnostics.size(), std::size_t{1},
                 "valid ignored attribute emits one diagnostic");
    requireEqual(valid.diagnostics[0].code,
                 octopoly::glb::DiagnosticCode::ignoredAttribute,
                 "valid ignored attribute diagnostic code");
}

void unsupportedExtensionsAndNewerMinimumVersionsAreRejected() {
    const auto baseBin = triangleBin(5121U);
    const std::size_t binLength = baseBin.size();
    const std::string baseJson = triangleJson(binLength, 5121U, 3U);
    const auto decodeJson = [&](std::string json) {
        return octopoly::glb::decodeGlb(makeGlb(std::move(json), baseBin));
    };
    const auto requireUnsupportedJson = [&](std::string json, std::string_view label) {
        requireDecodeError(makeGlb(std::move(json), baseBin),
                           DecodeErrorCode::unsupportedFeature, std::string(label));
    };

    requireUnsupportedJson(replaceOnce(baseJson,
                                       "\"version\":\"2.0\"",
                                       "\"version\":\"2.0\",\"extensions\":{}"),
                           "asset extensions payload");
    requireUnsupportedJson(prependRootMembers(baseJson, 0U).insert(
                               1U, "\"extensions\":{},"),
                           "root extensions payload");
    requireUnsupportedJson(replaceOnce(baseJson,
                                       "\"scenes\":[{\"nodes\":[0]}",
                                       "\"scenes\":[{\"nodes\":[0],\"extensions\":{}}"),
                           "scene extensions payload");
    requireUnsupportedJson(replaceOnce(baseJson,
                                       "\"nodes\":[{\"mesh\":0}",
                                       "\"nodes\":[{\"mesh\":0,\"extensions\":{}}"),
                           "node extensions payload");
    requireUnsupportedJson(replaceOnce(baseJson,
                                       "\"buffers\":[{\"byteLength\":39}",
                                       "\"buffers\":[{\"byteLength\":39,\"extensions\":{}}"),
                           "buffer extensions payload");
    requireUnsupportedJson(replaceOnce(baseJson,
                                       "\"meshes\":[{\"primitives\"",
                                       "\"meshes\":[{\"extensions\":{},\"primitives\""),
                           "mesh extensions payload");
    requireUnsupportedJson(replaceOnce(baseJson,
                                       "\"attributes\":{\"POSITION\":0},\"indices\":1",
                                       "\"attributes\":{\"POSITION\":0},\"indices\":1,\"extensions\":{}"),
                           "primitive extensions payload");
    requireUnsupportedJson(replaceOnce(baseJson,
                                       "\"bufferViews\":[{\"buffer\":0",
                                       "\"bufferViews\":[{\"extensions\":{},\"buffer\":0"),
                           "bufferView extensions payload");
    const std::string accessorExtension = replaceOnce(
        baseJson, "\"accessors\":[{\"bufferView\":0",
        "\"accessors\":[{\"extensions\":{},\"bufferView\":0");
    requireDecodeError(makeGlb(accessorExtension, baseBin),
                       DecodeErrorCode::unsupportedAccessor,
                       "accessor extensions payload");

    std::string declarations = baseJson;
    declarations.insert(1U, "\"extensionsUsed\":[],\"extensionsRequired\":[],");
    require(decodeJson(declarations).ok,
            "empty extension declaration arrays without payload remain supported");
    declarations.insert(1U, "\"extensions\":{},");
    requireUnsupportedJson(std::move(declarations),
                           "empty declarations do not permit extension payloads");

    const auto oldMinimum = decodeJson(replaceOnce(
        baseJson, "\"version\":\"2.0\"",
        "\"version\":\"2.0\",\"minVersion\":\"1.0\""));
    require(oldMinimum.ok, "supported older asset.minVersion must decode");
    const auto currentMinimum = decodeJson(replaceOnce(
        baseJson, "\"version\":\"2.0\"",
        "\"version\":\"2.0\",\"minVersion\":\"2.0\""));
    require(currentMinimum.ok, "asset.minVersion 2.0 must decode");
    requireDecodeError(makeGlb(replaceOnce(
                                  baseJson, "\"version\":\"2.0\"",
                                  "\"version\":\"2.0\",\"minVersion\":\"2.1\""),
                              baseBin),
                       DecodeErrorCode::unsupportedVersion,
                       "asset.minVersion 2.1");
    requireDecodeError(makeGlb(replaceOnce(
                                  baseJson, "\"version\":\"2.0\"",
                                  "\"version\":\"2.0\",\"minVersion\":2"),
                              baseBin),
                       DecodeErrorCode::unsupportedVersion,
                       "asset.minVersion must be a supported version string");
}

void jsonObjectMembersAreBoundedAndLargeUniqueObjectsDecode() {
    auto limits = octopoly::glb::LoadLimits{};
    limits.maxJsonObjectMembers = 8U;
    const auto boundary = triangleFixture(5121U);
    const auto boundaryResult = octopoly::glb::decodeGlb(boundary, limits);
    require(boundaryResult.ok, "eight-member root must meet the configured boundary");

    auto bin = triangleBin(5121U);
    const std::size_t binLength = bin.size();
    const auto overLimit = makeGlb(prependRootMembers(
                                       triangleJson(binLength, 5121U, 3U), 1U),
                                   bin);
    requireDecodeError(overLimit, DecodeErrorCode::jsonObjectMemberLimit,
                       "per-object member limit", limits);

    constexpr std::size_t largeUniqueMemberCount = 40'000U;
    const auto large = makeGlb(prependRootMembers(
                                   triangleJson(binLength, 5121U, 3U),
                                   largeUniqueMemberCount),
                               std::move(bin));
    require(large.size() < 4U * 1024U * 1024U,
            "large unique-key regression stays below the default JSON byte limit");
    const auto largeResult = octopoly::glb::decodeGlb(large);
    require(largeResult.ok, "large unique-key object must decode without quadratic lookup");
    requireEqual(largeResult.mesh.vertices().size(), std::size_t{3},
                 "large unique-key decode vertex count");
    requireEqual(largeResult.mesh.faces().size(), std::size_t{1},
                 "large unique-key decode face count");
}

void unsupportedFeaturesTypesBoundsIndicesAndLimitsAreRejected() {
    requireDecodeError(triangleFixture(5121U, {0, 1, 9}), DecodeErrorCode::invalidIndex,
                       "out-of-range index");
    requireDecodeError(triangleFixture(5121U, {0, 1, 2}, {}, {},
                                       ",\"translation\":[0,0,0]"),
                       DecodeErrorCode::unsupportedFeature, "node transform");
    requireDecodeError(triangleFixture(5121U, {0, 1, 2}, {}, ",\"mode\":1"),
                       DecodeErrorCode::unsupportedPrimitiveMode, "line primitive");
    requireDecodeError(triangleFixture(5121U, {0, 1, 2}, {}, {}, {},
                                       ",\"uri\":\"data:application/octet-stream;base64,AA==\""),
                       DecodeErrorCode::unsupportedExternalBuffer, "data URI buffer");
    requireDecodeError(triangleFixture(5121U, {0, 1, 2}, {}, {}, {}, {},
                                       ",\"sparse\":{}"),
                       DecodeErrorCode::unsupportedAccessor, "sparse accessor");
    requireDecodeError(triangleFixture(5121U, {0, 1, 2}, {}, {}, {}, {},
                                       ",\"normalized\":true"),
                       DecodeErrorCode::unsupportedAccessor, "normalized POSITION");

    auto nonfinite = triangleFixture(5121U);
    const std::size_t binHeader = 20U + readU32(nonfinite, 12) + 8U;
    writeU32(nonfinite, binHeader, 0x7f800000U);
    requireDecodeError(nonfinite, DecodeErrorCode::nonFinitePosition,
                       "nonfinite float POSITION");

    auto limits = octopoly::glb::LoadLimits{};
    limits.maxBytes = 4U;
    requireDecodeError(triangleFixture(5121U), DecodeErrorCode::inputTooLarge,
                       "byte limit", limits);
    limits = {};
    limits.maxJsonBytes = 8U;
    requireDecodeError(triangleFixture(5121U), DecodeErrorCode::jsonTooLarge,
                       "JSON byte limit", limits);
    limits = {};
    limits.maxJsonDepth = 2U;
    requireDecodeError(triangleFixture(5121U), DecodeErrorCode::jsonDepthLimit,
                       "JSON depth limit", limits);
    limits = {};
    limits.maxJsonNodes = 2U;
    requireDecodeError(triangleFixture(5121U), DecodeErrorCode::jsonNodeLimit,
                       "JSON node limit", limits);
    limits = {};
    limits.maxVertices = 2U;
    requireDecodeError(triangleFixture(5121U), DecodeErrorCode::vertexLimitExceeded,
                       "vertex limit", limits);
    limits = {};
    limits.maxTriangles = 0U;
    requireDecodeError(triangleFixture(5121U), DecodeErrorCode::triangleLimitExceeded,
                       "triangle limit", limits);
    limits = {};
    limits.maxPrimitives = 0U;
    requireDecodeError(triangleFixture(5121U), DecodeErrorCode::primitiveLimitExceeded,
                       "primitive limit", limits);
}

void truncationMutationAndAtomicInstallStaySafe() {
    const auto valid = triangleFixture(5121U);
    for (std::size_t length = 0; length < valid.size(); ++length) {
        const std::vector<std::uint8_t> prefix(valid.begin(),
                                              valid.begin() + static_cast<std::ptrdiff_t>(length));
        const auto result = octopoly::glb::decodeGlb(prefix);
        require(!result.ok, "every strict prefix must fail");
        require(result.error.code != DecodeErrorCode::none, "truncation typed error");
    }
    std::mt19937_64 random(0x474c425048415345ULL);
    for (std::size_t iteration = 0; iteration < 1000U; ++iteration) {
        auto mutation = valid;
        const std::size_t offset = static_cast<std::size_t>(random() % mutation.size());
        mutation[offset] ^= static_cast<std::uint8_t>(1U << (random() % 8U));
        const auto result = octopoly::glb::decodeGlb(mutation);
        if (result.ok) require(result.mesh.validate().ok, "successful mutation validates");
        else {
            require(result.error.code != DecodeErrorCode::none, "mutation typed code");
            require(result.error.category != octopoly::glb::ErrorCategory::none,
                    "mutation typed category");
            require(!result.error.message.empty(), "mutation message");
        }
    }

    Mesh live = Mesh::makeDefaultCube();
    require(live.insetFace(live.faces().front().id, 0.25).ok, "live edited fixture");
    const auto before = octopoly::project::encodeProject(live);
    require(before.ok, "canonical project snapshot");
    auto damaged = valid;
    damaged[0] ^= 1U;
    const auto failed = octopoly::glb::installGlb(live, damaged);
    require(!failed.ok, "bad GLB install fails");
    const auto after = octopoly::project::encodeProject(live);
    require(after.ok && after.bytes == before.bytes,
            "failed GLB install preserves exact canonical project bytes");
    const auto installed = octopoly::glb::installGlb(live, valid);
    require(installed.ok, "valid GLB installs");
    requireEqual(live.vertices().size(), std::size_t{3}, "success replaces mesh once");
    requireEqual(live.revision(), std::uint64_t{0}, "installed revision reset");
}

void encoderRejectsFloat32Overflow() {
    auto project = octopoly::project::encodeProject(Mesh::makeDefaultCube());
    require(project.ok, "overflow fixture native encode");
    constexpr std::size_t firstX = 88U;
    const double huge = static_cast<double>(std::numeric_limits<float>::max()) * 2.0;
    const std::uint64_t bits = std::bit_cast<std::uint64_t>(huge);
    for (unsigned byte = 0; byte < 8U; ++byte) {
        project.bytes[firstX + byte] = static_cast<std::uint8_t>((bits >> (byte * 8U)) & 0xffU);
    }
    writeU32(project.bytes, 24U, crc32(project.bytes.data() + 32U, project.bytes.size() - 32U));
    const auto decoded = octopoly::project::decodeProject(project.bytes);
    require(decoded.ok, "finite double beyond float32 range is a valid native mesh");
    const auto encoded = octopoly::glb::encodeGlb(decoded.mesh);
    require(!encoded.ok, "float32-overflowing mesh must not export");
    requireEqual(encoded.error.code, octopoly::glb::EncodeErrorCode::positionOutOfFloatRange,
                 "float32 overflow has a typed export error");
    requireEqual(encoded.error.category, octopoly::glb::ErrorCategory::compatibility,
                 "float32 overflow category");
    require(!encoded.error.message.empty(), "float32 overflow message");
}

struct TestCase { std::string name; std::function<void()> run; };

}  // namespace

int main(int argc, char** argv) {
    const std::vector<TestCase> tests{
        {"deterministicCubeExportHasExactStructure", deterministicCubeExportHasExactStructure},
        {"exportIndexWidthUsesMaximumReferencedStorageIndex",
         exportIndexWidthUsesMaximumReferencedStorageIndex},
        {"cubeAndEditedRoundTripsAreTriangulatedAndDeterministic",
         cubeAndEditedRoundTripsAreTriangulatedAndDeterministic},
        {"importsUnsignedIndexWidthsAndNonIndexedTriangles",
         importsUnsignedIndexWidthsAndNonIndexedTriangles},
        {"importsInterleavedAndMultiplePrimitivesWithAccessorLocalSharing",
         importsInterleavedAndMultiplePrimitivesWithAccessorLocalSharing},
        {"ignoredVisualDataProducesDeterministicWarnings",
         ignoredVisualDataProducesDeterministicWarnings},
        {"malformedContainerJsonAndPaddingAreRejected",
         malformedContainerJsonAndPaddingAreRejected},
        {"exactIntegerPropertiesRejectRoundedFractionsExponentsAndOverflow",
         exactIntegerPropertiesRejectRoundedFractionsExponentsAndOverflow},
        {"ignoredVisualAttributesRequireValidAccessorIndices",
         ignoredVisualAttributesRequireValidAccessorIndices},
        {"unsupportedExtensionsAndNewerMinimumVersionsAreRejected",
         unsupportedExtensionsAndNewerMinimumVersionsAreRejected},
        {"jsonObjectMembersAreBoundedAndLargeUniqueObjectsDecode",
         jsonObjectMembersAreBoundedAndLargeUniqueObjectsDecode},
        {"unsupportedFeaturesTypesBoundsIndicesAndLimitsAreRejected",
         unsupportedFeaturesTypesBoundsIndicesAndLimitsAreRejected},
        {"truncationMutationAndAtomicInstallStaySafe",
         truncationMutationAndAtomicInstallStaySafe},
        {"encoderRejectsFloat32Overflow", encoderRejectsFloat32Overflow},
    };
    const std::string filter = argc > 1 ? argv[1] : "";
    int failures = 0;
    int executed = 0;
    for (const auto& test : tests) {
        if (!filter.empty() && filter != test.name) continue;
        ++executed;
        try {
            test.run();
            std::cout << "PASS " << test.name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL " << test.name << ": " << error.what() << '\n';
        }
    }
    if (executed == 0) return 2;
    std::cout << executed << " test(s), " << failures << " failure(s)\n";
    return failures == 0 ? 0 : 1;
}
