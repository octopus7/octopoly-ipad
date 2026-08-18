#include "octopoly/project_codec.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <new>
#include <utility>

namespace octopoly::project {

struct MeshProjectAccess {
    static Mesh make(std::vector<Vertex> vertices, std::vector<Face> faces,
                     VertexId nextVertexId, FaceId nextFaceId,
                     std::uint64_t revision) {
        Mesh mesh;
        mesh.vertices_ = std::move(vertices);
        mesh.faces_ = std::move(faces);
        mesh.nextVertexId_ = nextVertexId;
        mesh.nextFaceId_ = nextFaceId;
        mesh.revision_ = revision;
        mesh.rebuildVertexLookup();
        return mesh;
    }
};

namespace {

constexpr std::size_t kHeaderSize = 32;
constexpr std::size_t kPayloadPrefixSize = 48;
constexpr std::uint8_t kMagic[]{'O', 'C', 'T', 'O', 'P', 'O', 'L', 'Y'};

bool checkedAdd(std::uint64_t first, std::uint64_t second, std::uint64_t& result) noexcept {
    if (second > std::numeric_limits<std::uint64_t>::max() - first) {
        return false;
    }
    result = first + second;
    return true;
}

bool checkedMultiply(std::uint64_t first, std::uint64_t second, std::uint64_t& result) noexcept {
    if (first != 0 && second > std::numeric_limits<std::uint64_t>::max() / first) {
        return false;
    }
    result = first * second;
    return true;
}

void appendU16(std::vector<std::uint8_t>& output, std::uint16_t value) {
    for (unsigned shift = 0; shift < 16; shift += 8) {
        output.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }
}

void appendU32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        output.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }
}

void appendU64(std::vector<std::uint8_t>& output, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) {
        output.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }
}

void appendF64(std::vector<std::uint8_t>& output, double value) {
    appendU64(output, std::bit_cast<std::uint64_t>(value));
}

std::uint16_t readU16(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(bytes[offset]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 1]) << 8U);
}

std::uint32_t readU32(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
    std::uint32_t value = 0;
    for (unsigned index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(bytes[offset + index]) << (index * 8U);
    }
    return value;
}

std::uint64_t readU64(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
    std::uint64_t value = 0;
    for (unsigned index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(bytes[offset + index]) << (index * 8U);
    }
    return value;
}

double readF64(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
    return std::bit_cast<double>(readU64(bytes, offset));
}

std::uint32_t crc32(const std::uint8_t* data, std::size_t size) noexcept {
    std::uint32_t crc = 0xffffffffU;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (unsigned bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xedb88320U & mask);
        }
    }
    return ~crc;
}

EncodeResult encodeFailure(EncodeErrorCode code, std::string_view message) noexcept {
    return {false, {}, {code, message}};
}

DecodeResult decodeFailure(DecodeErrorCategory category, DecodeErrorCode code,
                           std::size_t offset, std::string_view message) noexcept {
    return {false, {}, {category, code, offset, message}};
}

using IdLocation = std::pair<std::uint64_t, std::size_t>;

std::size_t sortAndFindFirstDuplicate(std::vector<IdLocation>& locations) {
    std::sort(locations.begin(), locations.end());
    std::size_t duplicateOffset = std::numeric_limits<std::size_t>::max();
    for (std::size_t index = 1; index < locations.size(); ++index) {
        if (locations[index - 1].first == locations[index].first) {
            duplicateOffset = std::min(duplicateOffset, locations[index].second);
        }
    }
    return duplicateOffset;
}

}  // namespace

EncodeResult encodeProject(const Mesh& mesh) noexcept {
    try {
        const ValidationResult validation = mesh.validate();
        if (!validation.ok) {
            return encodeFailure(EncodeErrorCode::invalidMesh,
                                 "mesh validation failed before encoding");
        }

        VertexId maximumVertexId = 0;
        for (const Vertex& vertex : mesh.vertices()) {
            maximumVertexId = std::max(maximumVertexId, vertex.id);
        }
        FaceId maximumFaceId = 0;
        std::uint64_t totalCorners = 0;
        for (const Face& face : mesh.faces()) {
            maximumFaceId = std::max(maximumFaceId, face.id);
            std::uint64_t updated = 0;
            if (!checkedAdd(totalCorners, static_cast<std::uint64_t>(face.vertices.size()), updated)) {
                return encodeFailure(EncodeErrorCode::integerOverflow,
                                     "face corner count overflows the wire format");
            }
            totalCorners = updated;
        }
        if (mesh.nextVertexId() == 0 || mesh.nextVertexId() <= maximumVertexId ||
            mesh.nextFaceId() == 0 || mesh.nextFaceId() <= maximumFaceId) {
            return encodeFailure(EncodeErrorCode::invalidMesh,
                                 "next IDs must be greater than existing IDs");
        }

        std::uint64_t vertexBytes = 0;
        std::uint64_t faceBytes = 0;
        std::uint64_t cornerBytes = 0;
        std::uint64_t payloadSize = kPayloadPrefixSize;
        if (!checkedMultiply(static_cast<std::uint64_t>(mesh.vertices().size()), 32, vertexBytes) ||
            !checkedMultiply(static_cast<std::uint64_t>(mesh.faces().size()), 16, faceBytes) ||
            !checkedMultiply(totalCorners, 8, cornerBytes) ||
            !checkedAdd(payloadSize, vertexBytes, payloadSize) ||
            !checkedAdd(payloadSize, faceBytes, payloadSize) ||
            !checkedAdd(payloadSize, cornerBytes, payloadSize) ||
            payloadSize > std::numeric_limits<std::size_t>::max() - kHeaderSize) {
            return encodeFailure(EncodeErrorCode::integerOverflow,
                                 "encoded project size overflows this platform");
        }

        std::vector<std::uint8_t> output;
        output.reserve(kHeaderSize + static_cast<std::size_t>(payloadSize));
        output.insert(output.end(), std::begin(kMagic), std::end(kMagic));
        appendU16(output, 1);
        appendU16(output, 0);
        output.push_back(1);
        output.insert(output.end(), 3, 0);
        appendU64(output, payloadSize);
        appendU32(output, 0);
        output.insert(output.end(), 4, 0);

        appendU64(output, static_cast<std::uint64_t>(mesh.vertices().size()));
        appendU64(output, static_cast<std::uint64_t>(mesh.faces().size()));
        appendU64(output, totalCorners);
        appendU64(output, mesh.nextVertexId());
        appendU64(output, mesh.nextFaceId());
        appendU64(output, mesh.revision());
        for (const Vertex& vertex : mesh.vertices()) {
            appendU64(output, vertex.id);
            appendF64(output, vertex.position.x);
            appendF64(output, vertex.position.y);
            appendF64(output, vertex.position.z);
        }
        for (const Face& face : mesh.faces()) {
            appendU64(output, face.id);
            appendU64(output, static_cast<std::uint64_t>(face.vertices.size()));
            for (const VertexId id : face.vertices) {
                appendU64(output, id);
            }
        }
        const std::uint32_t checksum = crc32(output.data() + kHeaderSize,
                                             static_cast<std::size_t>(payloadSize));
        for (unsigned shift = 0; shift < 32; shift += 8) {
            output[24 + shift / 8] = static_cast<std::uint8_t>((checksum >> shift) & 0xffU);
        }
        return {true, std::move(output), {}};
    } catch (const std::bad_alloc&) {
        return encodeFailure(EncodeErrorCode::allocationFailed,
                             "allocation failed while encoding project");
    } catch (...) {
        return encodeFailure(EncodeErrorCode::internalError,
                             "unexpected failure while encoding project");
    }
}

DecodeResult decodeProject(std::span<const std::uint8_t> bytes, LoadLimits limits) noexcept {
    try {
        if (bytes.size() > limits.maxBytes) {
            return decodeFailure(DecodeErrorCategory::resource, DecodeErrorCode::inputTooLarge,
                                 0, "project exceeds the configured byte limit");
        }
        if (bytes.size() < kHeaderSize) {
            return decodeFailure(DecodeErrorCategory::input, DecodeErrorCode::truncated,
                                 bytes.size(), "project header is truncated");
        }
        for (std::size_t index = 0; index < std::size(kMagic); ++index) {
            if (bytes[index] != kMagic[index]) {
                return decodeFailure(DecodeErrorCategory::format, DecodeErrorCode::badMagic,
                                     index, "project magic is invalid");
            }
        }
        if (readU16(bytes, 8) != 1 || readU16(bytes, 10) != 0) {
            return decodeFailure(DecodeErrorCategory::compatibility,
                                 DecodeErrorCode::unsupportedVersion, 8,
                                 "project version is not supported");
        }
        if (bytes[12] != 1) {
            return decodeFailure(DecodeErrorCategory::compatibility,
                                 DecodeErrorCode::unsupportedEndian, 12,
                                 "project endian marker is not supported");
        }
        for (std::size_t offset : {std::size_t{13}, std::size_t{14}, std::size_t{15},
                                   std::size_t{28}, std::size_t{29}, std::size_t{30},
                                   std::size_t{31}}) {
            if (bytes[offset] != 0) {
                return decodeFailure(DecodeErrorCategory::format,
                                     DecodeErrorCode::nonzeroReserved, offset,
                                     "reserved header bytes must be zero");
            }
        }

        const std::uint64_t payloadLength = readU64(bytes, 16);
        if (payloadLength > std::numeric_limits<std::size_t>::max() - kHeaderSize) {
            return decodeFailure(DecodeErrorCategory::format,
                                 DecodeErrorCode::integerOverflow, 16,
                                 "payload length overflows this platform");
        }
        const std::size_t expectedFileSize =
            kHeaderSize + static_cast<std::size_t>(payloadLength);
        if (bytes.size() < expectedFileSize) {
            return decodeFailure(DecodeErrorCategory::input, DecodeErrorCode::truncated,
                                 bytes.size(), "project payload is truncated");
        }
        if (bytes.size() > expectedFileSize) {
            return decodeFailure(DecodeErrorCategory::format, DecodeErrorCode::trailingBytes,
                                 expectedFileSize, "project contains trailing bytes");
        }
        if (crc32(bytes.data() + kHeaderSize, static_cast<std::size_t>(payloadLength)) !=
            readU32(bytes, 24)) {
            return decodeFailure(DecodeErrorCategory::integrity,
                                 DecodeErrorCode::checksumMismatch, 24,
                                 "project payload checksum does not match");
        }
        if (payloadLength < kPayloadPrefixSize) {
            return decodeFailure(DecodeErrorCategory::format,
                                 DecodeErrorCode::malformedLength, 16,
                                 "payload is too short for its fixed fields");
        }

        const std::uint64_t vertexCount = readU64(bytes, 32);
        const std::uint64_t faceCount = readU64(bytes, 40);
        const std::uint64_t totalCorners = readU64(bytes, 48);
        const VertexId nextVertexId = readU64(bytes, 56);
        const FaceId nextFaceId = readU64(bytes, 64);
        const std::uint64_t revision = readU64(bytes, 72);
        if (vertexCount > limits.maxVertices) {
            return decodeFailure(DecodeErrorCategory::resource,
                                 DecodeErrorCode::vertexLimitExceeded, 32,
                                 "vertex count exceeds the configured limit");
        }
        if (faceCount > limits.maxFaces) {
            return decodeFailure(DecodeErrorCategory::resource,
                                 DecodeErrorCode::faceLimitExceeded, 40,
                                 "face count exceeds the configured limit");
        }
        if (totalCorners > limits.maxFaceCorners) {
            return decodeFailure(DecodeErrorCategory::resource,
                                 DecodeErrorCode::cornerLimitExceeded, 48,
                                 "face corner count exceeds the configured limit");
        }
        if (vertexCount > std::numeric_limits<std::size_t>::max() ||
            faceCount > std::numeric_limits<std::size_t>::max() ||
            totalCorners > std::numeric_limits<std::size_t>::max()) {
            return decodeFailure(DecodeErrorCategory::format,
                                 DecodeErrorCode::integerOverflow, 32,
                                 "declared counts overflow this platform");
        }

        std::uint64_t vertexBytes = 0;
        std::uint64_t faceBytes = 0;
        std::uint64_t cornerBytes = 0;
        std::uint64_t requiredPayloadLength = kPayloadPrefixSize;
        if (!checkedMultiply(vertexCount, 32, vertexBytes) ||
            !checkedMultiply(faceCount, 16, faceBytes) ||
            !checkedMultiply(totalCorners, 8, cornerBytes) ||
            !checkedAdd(requiredPayloadLength, vertexBytes, requiredPayloadLength) ||
            !checkedAdd(requiredPayloadLength, faceBytes, requiredPayloadLength) ||
            !checkedAdd(requiredPayloadLength, cornerBytes, requiredPayloadLength)) {
            return decodeFailure(DecodeErrorCategory::format,
                                 DecodeErrorCode::integerOverflow, 32,
                                 "declared counts overflow the wire format");
        }
        if (requiredPayloadLength != payloadLength) {
            return decodeFailure(DecodeErrorCategory::format,
                                 DecodeErrorCode::malformedLength, 16,
                                 "payload length is inconsistent with declared counts");
        }

        std::vector<Vertex> vertices;
        std::vector<Face> faces;
        vertices.reserve(static_cast<std::size_t>(vertexCount));
        faces.reserve(static_cast<std::size_t>(faceCount));
        std::vector<IdLocation> vertexIdLocations;
        std::vector<IdLocation> faceIdLocations;
        vertexIdLocations.reserve(static_cast<std::size_t>(vertexCount));
        faceIdLocations.reserve(static_cast<std::size_t>(faceCount));

        std::size_t cursor = kHeaderSize + kPayloadPrefixSize;
        VertexId maximumVertexId = 0;
        for (std::uint64_t index = 0; index < vertexCount; ++index) {
            const std::size_t idOffset = cursor;
            const VertexId id = readU64(bytes, cursor);
            const Vec3 position{readF64(bytes, cursor + 8), readF64(bytes, cursor + 16),
                                readF64(bytes, cursor + 24)};
            cursor += 32;
            if (id == 0) {
                return decodeFailure(DecodeErrorCategory::topology,
                                     DecodeErrorCode::zeroVertexId, idOffset,
                                     "vertex IDs must be nonzero");
            }
            if (!std::isfinite(position.x)) {
                return decodeFailure(DecodeErrorCategory::topology,
                                     DecodeErrorCode::nonFinitePosition, idOffset + 8,
                                     "vertex positions must be finite");
            }
            if (!std::isfinite(position.y)) {
                return decodeFailure(DecodeErrorCategory::topology,
                                     DecodeErrorCode::nonFinitePosition, idOffset + 16,
                                     "vertex positions must be finite");
            }
            if (!std::isfinite(position.z)) {
                return decodeFailure(DecodeErrorCategory::topology,
                                     DecodeErrorCode::nonFinitePosition, idOffset + 24,
                                     "vertex positions must be finite");
            }
            maximumVertexId = std::max(maximumVertexId, id);
            vertexIdLocations.emplace_back(id, idOffset);
            vertices.push_back({id, position});
        }

        const std::size_t duplicateVertexOffset =
            sortAndFindFirstDuplicate(vertexIdLocations);
        if (duplicateVertexOffset != std::numeric_limits<std::size_t>::max()) {
            return decodeFailure(DecodeErrorCategory::topology,
                                 DecodeErrorCode::duplicateVertexId,
                                 duplicateVertexOffset,
                                 "vertex IDs must be unique");
        }
        std::vector<VertexId> vertexIds;
        vertexIds.reserve(vertexIdLocations.size());
        for (const auto& [id, offset] : vertexIdLocations) {
            static_cast<void>(offset);
            vertexIds.push_back(id);
        }

        FaceId maximumFaceId = 0;
        std::uint64_t parsedCorners = 0;
        for (std::uint64_t index = 0; index < faceCount; ++index) {
            const std::size_t idOffset = cursor;
            const FaceId id = readU64(bytes, cursor);
            const std::uint64_t cornerCount = readU64(bytes, cursor + 8);
            cursor += 16;
            if (id == 0) {
                return decodeFailure(DecodeErrorCategory::topology,
                                     DecodeErrorCode::zeroFaceId, idOffset,
                                     "face IDs must be nonzero");
            }
            if (cornerCount < 3) {
                return decodeFailure(DecodeErrorCategory::topology,
                                     DecodeErrorCode::invalidFaceLoop, idOffset + 8,
                                     "face loops need at least three vertices");
            }
            std::uint64_t updatedCorners = 0;
            if (!checkedAdd(parsedCorners, cornerCount, updatedCorners) ||
                updatedCorners > totalCorners) {
                return decodeFailure(DecodeErrorCategory::format,
                                     DecodeErrorCode::malformedCount, idOffset + 8,
                                     "face corner counts are inconsistent");
            }
            parsedCorners = updatedCorners;

            Face face{id, {}};
            face.vertices.reserve(static_cast<std::size_t>(cornerCount));
            std::vector<IdLocation> loopIdLocations;
            loopIdLocations.reserve(static_cast<std::size_t>(cornerCount));
            std::size_t missingReferenceOffset = std::numeric_limits<std::size_t>::max();
            for (std::uint64_t corner = 0; corner < cornerCount; ++corner) {
                const std::size_t referenceOffset = cursor;
                const VertexId reference = readU64(bytes, cursor);
                cursor += 8;
                if (!std::binary_search(vertexIds.begin(), vertexIds.end(), reference)) {
                    missingReferenceOffset = std::min(missingReferenceOffset,
                                                      referenceOffset);
                }
                loopIdLocations.emplace_back(reference, referenceOffset);
                face.vertices.push_back(reference);
            }
            const std::size_t duplicateLoopOffset =
                sortAndFindFirstDuplicate(loopIdLocations);
            if (missingReferenceOffset < duplicateLoopOffset) {
                return decodeFailure(DecodeErrorCategory::topology,
                                     DecodeErrorCode::missingVertexReference,
                                     missingReferenceOffset,
                                     "face references a missing vertex");
            }
            if (duplicateLoopOffset != std::numeric_limits<std::size_t>::max()) {
                return decodeFailure(DecodeErrorCategory::topology,
                                     DecodeErrorCode::invalidFaceLoop,
                                     duplicateLoopOffset,
                                     "face loop vertices must be unique");
            }
            maximumFaceId = std::max(maximumFaceId, id);
            faceIdLocations.emplace_back(id, idOffset);
            faces.push_back(std::move(face));
        }
        if (parsedCorners != totalCorners || cursor != bytes.size()) {
            return decodeFailure(DecodeErrorCategory::format,
                                 DecodeErrorCode::malformedCount, 48,
                                 "declared face corner count is inconsistent");
        }
        const std::size_t duplicateFaceOffset =
            sortAndFindFirstDuplicate(faceIdLocations);
        if (duplicateFaceOffset != std::numeric_limits<std::size_t>::max()) {
            return decodeFailure(DecodeErrorCategory::topology,
                                 DecodeErrorCode::duplicateFaceId,
                                 duplicateFaceOffset,
                                 "face IDs must be unique");
        }
        if (nextVertexId == 0 || nextVertexId <= maximumVertexId) {
            return decodeFailure(DecodeErrorCategory::topology,
                                 DecodeErrorCode::nextVertexIdInvalid, 56,
                                 "next vertex ID must be greater than existing IDs");
        }
        if (nextFaceId == 0 || nextFaceId <= maximumFaceId) {
            return decodeFailure(DecodeErrorCategory::topology,
                                 DecodeErrorCode::nextFaceIdInvalid, 64,
                                 "next face ID must be greater than existing IDs");
        }

        Mesh candidate = MeshProjectAccess::make(std::move(vertices), std::move(faces),
                                                 nextVertexId, nextFaceId, revision);
        if (!candidate.validate().ok) {
            return decodeFailure(DecodeErrorCategory::topology,
                                 DecodeErrorCode::meshValidationFailed, kHeaderSize,
                                 "decoded mesh failed full validation");
        }
        return {true, std::move(candidate), {}};
    } catch (const std::bad_alloc&) {
        return decodeFailure(DecodeErrorCategory::allocation,
                             DecodeErrorCode::allocationFailed, 0,
                             "allocation failed while decoding project");
    } catch (...) {
        return decodeFailure(DecodeErrorCategory::internal,
                             DecodeErrorCode::internalError, 0,
                             "unexpected failure while decoding project");
    }
}

InstallResult installProject(Mesh& liveMesh, std::span<const std::uint8_t> bytes,
                             LoadLimits limits) noexcept {
    static_assert(noexcept(std::declval<Mesh&>() = std::declval<Mesh&&>()),
                  "atomic install requires non-throwing Mesh move assignment");
    DecodeResult decoded = decodeProject(bytes, limits);
    if (!decoded.ok) {
        return {false, decoded.error};
    }
    liveMesh = std::move(decoded.mesh);
    return {true, {}};
}

}  // namespace octopoly::project
