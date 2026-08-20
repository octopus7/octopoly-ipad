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
#include <vector>

namespace {

static_assert(static_cast<int>(octopoly::project::EncodeErrorCode::none) == 0);
static_assert(static_cast<int>(octopoly::project::EncodeErrorCode::invalidMesh) == 1);
static_assert(static_cast<int>(octopoly::project::EncodeErrorCode::integerOverflow) == 2);
static_assert(static_cast<int>(octopoly::project::EncodeErrorCode::allocationFailed) == 3);
static_assert(static_cast<int>(octopoly::project::EncodeErrorCode::internalError) == 4);
static_assert(static_cast<int>(octopoly::project::EncodeErrorCode::invalidScene) == 5);

using octopoly::FaceId;
using octopoly::Mesh;
using octopoly::VertexId;
using octopoly::project::decodeProject;
using octopoly::project::encodeProject;
using octopoly::project::installProject;

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename A, typename B>
void require_equal(const A& actual, const B& expected, const std::string& message) {
    if (!(actual == expected)) {
        throw std::runtime_error(message);
    }
}

void require_same_mesh(const Mesh& actual, const Mesh& expected, const std::string& message) {
    require_equal(actual.vertices(), expected.vertices(), message + " (vertices)");
    require_equal(actual.faces(), expected.faces(), message + " (faces)");
    require_equal(actual.nextVertexId(), expected.nextVertexId(), message + " (next vertex ID)");
    require_equal(actual.nextFaceId(), expected.nextFaceId(), message + " (next face ID)");
    require_equal(actual.revision(), expected.revision(), message + " (revision)");
}

std::uint32_t crc32(const std::uint8_t* data, std::size_t size) {
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

void write_u32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    for (unsigned index = 0; index < 4; ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>((value >> (index * 8U)) & 0xffU);
    }
}

void write_u64(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint64_t value) {
    for (unsigned index = 0; index < 8; ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>((value >> (index * 8U)) & 0xffU);
    }
}

void write_f64(std::vector<std::uint8_t>& bytes, std::size_t offset, double value) {
    write_u64(bytes, offset, std::bit_cast<std::uint64_t>(value));
}

void refresh_payload_crc(std::vector<std::uint8_t>& bytes) {
    require(bytes.size() >= 32, "test fixture must contain a complete header");
    write_u32(bytes, 24, crc32(bytes.data() + 32, bytes.size() - 32));
}

void require_decode_error(const std::vector<std::uint8_t>& bytes,
                          octopoly::project::DecodeErrorCode expected,
                          const std::string& message,
                          octopoly::project::LoadLimits limits = {}) {
    const auto decoded = decodeProject(bytes, limits);
    require(!decoded.ok, message + " must fail");
    require_equal(decoded.error.code, expected, message + " error code");
    require(decoded.error.category != octopoly::project::DecodeErrorCategory::none,
            message + " must have an error category");
    require(!decoded.error.message.empty(), message + " must have an error message");
    require(decoded.error.offset <= bytes.size(), message + " offset must address the input");
}

void default_cube_encoding_is_deterministic_with_golden_header() {
    const Mesh cube = Mesh::makeDefaultCube();
    const auto first = encodeProject(cube);
    const auto second = encodeProject(cube);
    require(first.ok, "default cube must encode");
    require(second.ok, "repeat default cube encode must succeed");
    require_equal(first.bytes, second.bytes, "repeat encoding must be byte-for-byte equal");
    require_equal(first.bytes.size(), std::size_t{624}, "default cube encoded byte count is stable");

    const std::array<std::uint8_t, 32> goldenHeader{
        0x4f, 0x43, 0x54, 0x4f, 0x50, 0x4f, 0x4c, 0x59,
        0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
        0x50, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x70, 0xb9, 0xb1, 0x07, 0x00, 0x00, 0x00, 0x00,
    };
    require(first.bytes.size() >= goldenHeader.size(), "encoding must contain its header");
    require(std::equal(goldenHeader.begin(), goldenHeader.end(), first.bytes.begin()),
            "default cube header must match the v1.0 golden bytes exactly");
}

void round_trip_preserves_default_and_edited_state_and_future_ids() {
    std::vector<Mesh> examples;
    examples.push_back(Mesh::makeDefaultCube());

    Mesh edited = Mesh::makeDefaultCube();
    const auto loop = edited.loopCut(edited.faces().front().id);
    require(loop.ok, "round-trip fixture loop cut must succeed");
    const auto inset = edited.insetFace(edited.faces()[1].id, 0.2);
    require(inset.ok, "round-trip fixture inset must succeed");
    examples.push_back(edited);

    for (const Mesh& expected : examples) {
        const auto encoded = encodeProject(expected);
        require(encoded.ok, "round-trip fixture must encode");
        auto decoded = decodeProject(encoded.bytes);
        require(decoded.ok, "encoded project must decode");
        require_same_mesh(decoded.mesh, expected, "round-trip must preserve complete mesh state");

        Mesh expectedContinuation = expected;
        const FaceId continuationFace = expectedContinuation.faces().front().id;
        const auto expectedEdit = expectedContinuation.extrudeFace(continuationFace, {0.0, 0.0, -0.25});
        const auto decodedEdit = decoded.mesh.extrudeFace(continuationFace, {0.0, 0.0, -0.25});
        require(expectedEdit.ok && decodedEdit.ok, "continued edits must succeed after round-trip");
        require_equal(decodedEdit.createdVertices, expectedEdit.createdVertices,
                      "continued edit must use the same future vertex IDs");
        require_equal(decodedEdit.affectedFaces, expectedEdit.affectedFaces,
                      "continued edit must use the same future face IDs");
        require_same_mesh(decoded.mesh, expectedContinuation,
                          "continued edit after load must remain semantically identical");
    }
}

void every_truncation_and_single_bit_corruption_is_rejected() {
    const auto encoded = encodeProject(Mesh::makeDefaultCube());
    require(encoded.ok, "corruption fixture must encode");
    for (std::size_t length = 0; length < encoded.bytes.size(); ++length) {
        const std::vector<std::uint8_t> truncated(encoded.bytes.begin(),
                                                  encoded.bytes.begin() + static_cast<std::ptrdiff_t>(length));
        const auto result = decodeProject(truncated);
        require(!result.ok, "every strict prefix must be rejected as truncated");
        require(result.error.code != octopoly::project::DecodeErrorCode::none,
                "truncation must return a typed error");
    }
    for (std::size_t offset = 0; offset < encoded.bytes.size(); ++offset) {
        std::vector<std::uint8_t> corrupted = encoded.bytes;
        corrupted[offset] ^= 0x01U;
        const auto result = decodeProject(corrupted);
        require(!result.ok, "single-bit corruption at every byte must be rejected");
        require(result.error.code != octopoly::project::DecodeErrorCode::none,
                "bit corruption must return a typed error");
    }
}

void malformed_headers_and_checksum_have_precise_typed_errors() {
    const auto encoded = encodeProject(Mesh::makeDefaultCube());
    require(encoded.ok, "header fixture must encode");

    auto changed = encoded.bytes;
    changed[0] = 'X';
    require_decode_error(changed, octopoly::project::DecodeErrorCode::badMagic, "bad magic");

    changed = encoded.bytes;
    changed[8] = 2;
    require_decode_error(changed, octopoly::project::DecodeErrorCode::unsupportedVersion,
                         "unsupported major version");
    changed = encoded.bytes;
    changed[10] = 1;
    require_decode_error(changed, octopoly::project::DecodeErrorCode::unsupportedVersion,
                         "unsupported minor version");
    changed = encoded.bytes;
    changed[12] = 2;
    require_decode_error(changed, octopoly::project::DecodeErrorCode::unsupportedEndian,
                         "unsupported endian");
    for (const std::size_t offset : {std::size_t{13}, std::size_t{28}}) {
        changed = encoded.bytes;
        changed[offset] = 1;
        require_decode_error(changed, octopoly::project::DecodeErrorCode::nonzeroReserved,
                             "nonzero reserved byte");
    }
    changed = encoded.bytes;
    changed.push_back(0);
    require_decode_error(changed, octopoly::project::DecodeErrorCode::trailingBytes,
                         "trailing byte");
    changed = encoded.bytes;
    changed[100] ^= 0x80U;
    require_decode_error(changed, octopoly::project::DecodeErrorCode::checksumMismatch,
                         "payload checksum mismatch");
    changed = encoded.bytes;
    write_u64(changed, 16, std::numeric_limits<std::uint64_t>::max());
    require_decode_error(changed, octopoly::project::DecodeErrorCode::integerOverflow,
                         "overflowing payload length");
}

void crc_recomputed_structural_invalidity_is_rejected() {
    const auto encoded = encodeProject(Mesh::makeDefaultCube());
    require(encoded.ok, "structural fixture must encode");
    const auto check = [&](std::size_t offset, std::uint64_t value,
                           octopoly::project::DecodeErrorCode code,
                           const std::string& label) {
        auto changed = encoded.bytes;
        write_u64(changed, offset, value);
        refresh_payload_crc(changed);
        require_decode_error(changed, code, label);
    };

    check(80, 0, octopoly::project::DecodeErrorCode::zeroVertexId, "zero vertex ID");
    check(112, 1, octopoly::project::DecodeErrorCode::duplicateVertexId,
          "duplicate vertex ID");
    check(88, UINT64_C(0x7ff0000000000000),
          octopoly::project::DecodeErrorCode::nonFinitePosition,
          "nonfinite vertex position");
    check(336, 0, octopoly::project::DecodeErrorCode::zeroFaceId, "zero face ID");
    check(384, 1, octopoly::project::DecodeErrorCode::duplicateFaceId,
          "duplicate face ID");
    check(352, 999, octopoly::project::DecodeErrorCode::missingVertexReference,
          "missing face reference");
    check(360, 1, octopoly::project::DecodeErrorCode::invalidFaceLoop,
          "duplicate vertex in face loop");
    check(344, 2, octopoly::project::DecodeErrorCode::invalidFaceLoop,
          "short face loop");
    check(56, 8, octopoly::project::DecodeErrorCode::nextVertexIdInvalid,
          "next vertex ID not above maximum");
    check(64, 6, octopoly::project::DecodeErrorCode::nextFaceIdInvalid,
          "next face ID not above maximum");
    check(48, 23, octopoly::project::DecodeErrorCode::malformedLength,
          "declared total corners inconsistent with payload length");
    check(344, 25, octopoly::project::DecodeErrorCode::malformedCount,
          "per-face corner count exceeds declared total");
}

std::vector<std::uint8_t> unusual_high_id_fixture() {
    constexpr std::array<VertexId, 8> vertexIds{
        UINT64_C(0x8000000000000001), UINT64_C(0x00ff00ff00ff00ff),
        UINT64_C(0xaaaaaaaaaaaaaaaa), UINT64_C(0x0101010101010101),
        UINT64_C(0xffffffffffffff00), UINT64_C(0x5555555555555555),
        UINT64_C(0x7fffffffffffffff), UINT64_C(0xdeadbeef00000001),
    };
    constexpr std::array<FaceId, 6> faceIds{
        UINT64_C(0x9000000000000001), UINT64_C(0x1111111111111111),
        UINT64_C(0xeeeeeeeeeeeeeeee), UINT64_C(0x2222222222222222),
        UINT64_C(0xdddddddddddddddd), UINT64_C(0xabcdef0000000001),
    };
    constexpr std::array<std::array<std::size_t, 4>, 6> loops{{
        {{0, 3, 2, 1}}, {{4, 5, 6, 7}}, {{0, 1, 5, 4}},
        {{3, 7, 6, 2}}, {{0, 4, 7, 3}}, {{1, 2, 6, 5}},
    }};

    auto encoded = encodeProject(Mesh::makeDefaultCube());
    require(encoded.ok, "unusual-ID base fixture must encode");
    for (std::size_t index = 0; index < vertexIds.size(); ++index) {
        write_u64(encoded.bytes, 80 + 32 * index, vertexIds[index]);
    }
    for (std::size_t faceIndex = 0; faceIndex < faceIds.size(); ++faceIndex) {
        const std::size_t faceOffset = 336 + 48 * faceIndex;
        write_u64(encoded.bytes, faceOffset, faceIds[faceIndex]);
        for (std::size_t corner = 0; corner < loops[faceIndex].size(); ++corner) {
            write_u64(encoded.bytes, faceOffset + 16 + 8 * corner,
                      vertexIds[loops[faceIndex][corner]]);
        }
    }
    write_u64(encoded.bytes, 56, std::numeric_limits<std::uint64_t>::max());
    write_u64(encoded.bytes, 64, std::numeric_limits<std::uint64_t>::max());
    refresh_payload_crc(encoded.bytes);
    return encoded.bytes;
}

void sorted_id_validation_preserves_unusual_ids_and_precise_errors() {
    const auto fixture = unusual_high_id_fixture();
    const auto decoded = decodeProject(fixture);
    require(decoded.ok, "unique unusual high IDs must decode");
    require(decoded.mesh.validate().ok, "unique unusual high IDs must validate");
    const auto reencoded = encodeProject(decoded.mesh);
    require(reencoded.ok, "unique unusual high IDs must re-encode");
    require_equal(reencoded.bytes, fixture,
                  "unique unusual high IDs must round-trip byte-for-byte");

    const auto check = [&](std::size_t changedOffset, std::uint64_t value,
                           octopoly::project::DecodeErrorCode expectedCode,
                           std::size_t expectedOffset, const std::string& label) {
        auto changed = fixture;
        write_u64(changed, changedOffset, value);
        refresh_payload_crc(changed);
        const auto result = decodeProject(changed);
        require(!result.ok, label + " must fail");
        require_equal(result.error.code, expectedCode, label + " error code");
        require_equal(result.error.offset, expectedOffset, label + " error offset");
    };

    check(112, UINT64_C(0x8000000000000001),
          octopoly::project::DecodeErrorCode::duplicateVertexId, 112,
          "unusual duplicate vertex ID");
    check(384, UINT64_C(0x9000000000000001),
          octopoly::project::DecodeErrorCode::duplicateFaceId, 384,
          "unusual duplicate face ID");
    check(360, UINT64_C(0x8000000000000001),
          octopoly::project::DecodeErrorCode::invalidFaceLoop, 360,
          "unusual duplicate face-loop vertex ID");
    check(352, std::numeric_limits<std::uint64_t>::max() - 1,
          octopoly::project::DecodeErrorCode::missingVertexReference, 352,
          "unusual missing vertex reference");
}

void decoded_unsorted_high_ids_resolve_positions_and_round_trip() {
    const auto fixture = unusual_high_id_fixture();
    const auto decoded = decodeProject(fixture);
    require(decoded.ok, "unsorted high-ID lookup fixture must decode");

    for (std::size_t index = 0; index < decoded.mesh.vertices().size(); ++index) {
        std::uint64_t encodedId = 0;
        for (unsigned byte = 0; byte < 8; ++byte) {
            encodedId |= static_cast<std::uint64_t>(fixture[80 + 32 * index + byte])
                         << (byte * 8U);
        }
        const auto* resolved = decoded.mesh.vertex(encodedId);
        require(resolved != nullptr, "every decoded stable vertex ID must resolve");
        require_equal(resolved->position, decoded.mesh.vertices()[index].position,
                      "decoded stable ID must map to its stored vertex position");
    }

    const Mesh copied = decoded.mesh;
    require(copied.validate().ok, "copy must preserve the derived lookup index");
    Mesh copyAssigned;
    copyAssigned = copied;
    require(copyAssigned.validate().ok,
            "copy assignment must preserve the derived lookup index");
    Mesh moved = std::move(copyAssigned);
    require(moved.validate().ok, "move construction must preserve the derived lookup index");
    Mesh moveAssigned = Mesh::makeDefaultCube();
    moveAssigned = std::move(moved);
    require(moveAssigned.validate().ok, "move assignment must preserve the derived lookup index");
    for (const auto& vertex : moveAssigned.vertices()) {
        require(moveAssigned.vertex(vertex.id) != nullptr,
                "copied and moved lookup index must resolve every stable ID");
    }

    const auto reencoded = encodeProject(decoded.mesh);
    require(reencoded.ok, "unsorted high-ID lookup fixture must re-encode");
    require_equal(reencoded.bytes, fixture,
                  "derived lookup index must not alter vertex storage order or codec bytes");
}

std::vector<std::uint8_t> large_reverse_id_fixture(std::size_t vertexCount) {
    constexpr VertexId baseId = UINT64_C(0x4000000000000000);
    constexpr std::size_t headerSize = 32;
    constexpr std::size_t payloadPrefixSize = 48;
    const std::size_t payloadSize = payloadPrefixSize + 32 * vertexCount + 16 + 8 * vertexCount;
    std::vector<std::uint8_t> bytes(headerSize + payloadSize, 0);
    constexpr std::array<std::uint8_t, 8> magic{'O', 'C', 'T', 'O', 'P', 'O', 'L', 'Y'};
    std::copy(magic.begin(), magic.end(), bytes.begin());
    bytes[8] = 1;
    bytes[12] = 1;
    write_u64(bytes, 16, payloadSize);
    write_u64(bytes, 32, vertexCount);
    write_u64(bytes, 40, 1);
    write_u64(bytes, 48, vertexCount);
    write_u64(bytes, 56, baseId + vertexCount + 1);
    write_u64(bytes, 64, 2);
    write_u64(bytes, 72, 17);

    for (std::size_t index = 0; index < vertexCount; ++index) {
        const std::size_t offset = 80 + 32 * index;
        const VertexId id = baseId + vertexCount - index;
        write_u64(bytes, offset, id);
        write_f64(bytes, offset + 8, static_cast<double>(index));
        write_f64(bytes, offset + 16, static_cast<double>(id - baseId));
        write_f64(bytes, offset + 24, -static_cast<double>(index));
    }

    const std::size_t faceOffset = 80 + 32 * vertexCount;
    write_u64(bytes, faceOffset, 1);
    write_u64(bytes, faceOffset + 8, vertexCount);
    for (std::size_t corner = 0; corner < vertexCount; ++corner) {
        write_u64(bytes, faceOffset + 16 + 8 * corner, baseId + corner + 1);
    }
    refresh_payload_crc(bytes);
    return bytes;
}

void large_reverse_id_lookup_and_streaming_traversal_is_exact() {
    constexpr std::size_t vertexCount = 30'000;
    constexpr VertexId baseId = UINT64_C(0x4000000000000000);
    const auto fixture = large_reverse_id_fixture(vertexCount);
    const auto decoded = decodeProject(fixture);
    require(decoded.ok, "large adversarial lookup fixture must decode");
    require(decoded.mesh.validate().ok, "large adversarial lookup fixture must validate");

    std::size_t resolvedCount = 0;
    for (std::size_t offset = 1; offset <= vertexCount; ++offset) {
        const VertexId id = baseId + offset;
        const auto* vertex = decoded.mesh.vertex(id);
        require(vertex != nullptr, "worst-order direct lookup must resolve every stable ID");
        const std::size_t storageIndex = vertexCount - offset;
        require_equal(vertex->position,
                      octopoly::Vec3{static_cast<double>(storageIndex),
                                     static_cast<double>(offset),
                                     -static_cast<double>(storageIndex)},
                      "worst-order direct lookup must resolve the exact stored position");
        ++resolvedCount;
    }

    std::size_t triangleCount = 0;
    std::size_t referencedPositionCount = 0;
    decoded.mesh.visitTriangles([&](const octopoly::Triangle& triangle) {
        const std::array<VertexId, 3> expected{
            baseId + 1, baseId + triangleCount + 2, baseId + triangleCount + 3};
        require_equal(triangle.sourceFace, FaceId{1},
                      "streamed adversarial triangle keeps its source face");
        require_equal(triangle.vertices, expected,
                      "streamed adversarial triangle keeps deterministic fan order");
        for (const VertexId id : triangle.vertices) {
            const auto* vertex = decoded.mesh.vertex(id);
            require(vertex != nullptr, "streamed triangle stable ID must resolve");
            const std::size_t offset = static_cast<std::size_t>(id - baseId);
            const std::size_t storageIndex = vertexCount - offset;
            require_equal(vertex->position.x, static_cast<double>(storageIndex),
                          "streamed triangle lookup resolves exact position");
            ++referencedPositionCount;
        }
        ++triangleCount;
    });

    require_equal(resolvedCount, vertexCount, "large fixture direct lookup count");
    require_equal(triangleCount, vertexCount - 2, "large polygon exact triangle count");
    require_equal(referencedPositionCount, 3 * (vertexCount - 2),
                  "large polygon exact referenced-position visit count");
}

void injected_resource_limits_reject_before_allocation() {
    const auto encoded = encodeProject(Mesh::makeDefaultCube());
    require(encoded.ok, "resource fixture must encode");

    auto limits = octopoly::project::LoadLimits{};
    limits.maxBytes = encoded.bytes.size() - 1;
    require_decode_error(encoded.bytes, octopoly::project::DecodeErrorCode::inputTooLarge,
                         "byte limit", limits);
    limits = {};
    limits.maxVertices = 7;
    require_decode_error(encoded.bytes, octopoly::project::DecodeErrorCode::vertexLimitExceeded,
                         "vertex limit", limits);
    limits = {};
    limits.maxFaces = 5;
    require_decode_error(encoded.bytes, octopoly::project::DecodeErrorCode::faceLimitExceeded,
                         "face limit", limits);
    limits = {};
    limits.maxFaceCorners = 23;
    require_decode_error(encoded.bytes, octopoly::project::DecodeErrorCode::cornerLimitExceeded,
                         "corner limit", limits);

    auto oversized = encoded.bytes;
    write_u64(oversized, 32, std::numeric_limits<std::uint64_t>::max());
    refresh_payload_crc(oversized);
    limits = {};
    limits.maxVertices = std::numeric_limits<std::uint64_t>::max();
    require_decode_error(oversized, octopoly::project::DecodeErrorCode::integerOverflow,
                         "oversized declared vertex count", limits);
}

void fixed_seed_mutations_never_crash_and_always_return_a_typed_result() {
    const auto encoded = encodeProject(Mesh::makeDefaultCube());
    require(encoded.ok, "mutation fixture must encode");
    std::mt19937_64 random(0x4f43544f504f4c59ULL);
    for (std::size_t iteration = 0; iteration < 2'000; ++iteration) {
        std::vector<std::uint8_t> mutation = encoded.bytes;
        switch (iteration % 4) {
        case 0: {
            const std::size_t offset = static_cast<std::size_t>(random() % mutation.size());
            mutation[offset] ^= static_cast<std::uint8_t>(1U << (random() % 8U));
            break;
        }
        case 1:
            mutation.resize(static_cast<std::size_t>(random() % mutation.size()));
            break;
        case 2: {
            const std::size_t count = 1 + static_cast<std::size_t>(random() % 4U);
            for (std::size_t index = 0; index < count; ++index) {
                mutation.push_back(static_cast<std::uint8_t>(random()));
            }
            break;
        }
        default:
            for (std::size_t index = 0; index < 4; ++index) {
                const std::size_t offset = static_cast<std::size_t>(random() % mutation.size());
                mutation[offset] = static_cast<std::uint8_t>(random());
            }
            break;
        }
        const auto result = decodeProject(mutation);
        if (result.ok) {
            require(result.mesh.validate().ok, "successful mutation decode must return a valid mesh");
        } else {
            require(result.error.category != octopoly::project::DecodeErrorCategory::none,
                    "mutation failure must have a typed category");
            require(result.error.code != octopoly::project::DecodeErrorCode::none,
                    "mutation failure must have a typed code");
            require(!result.error.message.empty(), "mutation failure must have a message");
        }
    }
}

void failed_install_is_atomic_and_success_replaces_complete_state() {
    Mesh live = Mesh::makeDefaultCube();
    require(live.extrudeFace(live.faces().front().id, {0.0, 0.0, -0.5}).ok,
            "live fixture edit must succeed");
    const Mesh beforeFailure = live;
    const auto beforeBytes = encodeProject(live);
    require(beforeBytes.ok, "live fixture must encode before failed load");

    std::vector<std::uint8_t> damaged = beforeBytes.bytes;
    damaged[0] ^= 0x01U;
    const auto failed = installProject(live, damaged);
    require(!failed.ok, "damaged project install must fail");
    require(failed.error.code != octopoly::project::DecodeErrorCode::none,
            "failed install must return a typed decode error");
    require_same_mesh(live, beforeFailure, "failed install must not mutate the live mesh");
    const auto afterFailureBytes = encodeProject(live);
    require(afterFailureBytes.ok, "live mesh must remain encodable after failed load");
    require_equal(afterFailureBytes.bytes, beforeBytes.bytes,
                  "failed install must leave canonical live bytes unchanged");

    Mesh replacement = Mesh::makeDefaultCube();
    require(replacement.loopCut(replacement.faces().front().id).ok,
            "replacement fixture edit must succeed");
    require(replacement.insetFace(replacement.faces()[1].id, 0.3).ok,
            "replacement fixture second edit must succeed");
    const auto replacementBytes = encodeProject(replacement);
    require(replacementBytes.ok, "replacement fixture must encode");
    const auto installed = installProject(live, replacementBytes.bytes);
    require(installed.ok, "valid project install must succeed");
    require_same_mesh(live, replacement,
                      "successful install must replace the complete mesh state exactly once");
}

std::vector<std::uint8_t> cube_fixture_with_next_ids(VertexId nextVertexId,
                                                     FaceId nextFaceId) {
    auto encoded = encodeProject(Mesh::makeDefaultCube());
    require(encoded.ok, "ID-capacity fixture must encode");
    write_u64(encoded.bytes, 56, nextVertexId);
    write_u64(encoded.bytes, 64, nextFaceId);
    refresh_payload_crc(encoded.bytes);
    return encoded.bytes;
}

std::vector<std::uint8_t> cube_fixture_with_revision(std::uint64_t revision) {
    auto encoded = encodeProject(Mesh::makeDefaultCube());
    require(encoded.ok, "revision fixture must encode");
    write_u64(encoded.bytes, 72, revision);
    refresh_payload_crc(encoded.bytes);
    return encoded.bytes;
}

void terminal_revision_rejects_every_mutation_atomically_after_install() {
    const std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
    const auto fixture = cube_fixture_with_revision(maximum);

    const std::vector<std::pair<std::string,
        std::function<octopoly::OperationResult(Mesh&)>>> operations{
        {"loop cut", [](Mesh& mesh) { return mesh.loopCut(mesh.faces().front().id); }},
        {"knife cut", [](Mesh& mesh) {
             return mesh.knifeCut(mesh.faces().front().id, 0, 0.25, 2, 0.75);
         }},
        {"inset", [](Mesh& mesh) { return mesh.insetFace(mesh.faces().front().id, 0.25); }},
        {"extrude", [](Mesh& mesh) {
             return mesh.extrudeFace(mesh.faces().front().id, {0.0, 0.0, -1.0});
         }},
        {"merge", [](Mesh& mesh) {
             return mesh.mergeVertices(mesh.vertices()[0].id, mesh.vertices()[1].id);
         }},
    };

    for (const auto& [label, operation] : operations) {
        const auto decoded = decodeProject(fixture);
        require(decoded.ok, label + " terminal-revision fixture must decode");
        require_equal(decoded.mesh.revision(), maximum,
                      label + " decoder must preserve UINT64_MAX revision");

        Mesh live = Mesh::makeDefaultCube();
        require(live.extrudeFace(live.faces().front().id, {0.0, 0.0, -0.5}).ok,
                label + " live pre-install edit must succeed");
        const auto installed = installProject(live, fixture);
        require(installed.ok, label + " terminal-revision fixture must install");
        const Mesh before = live;
        const auto beforeBytes = encodeProject(live);
        require(beforeBytes.ok, label + " terminal revision must encode");
        require_equal(beforeBytes.bytes, fixture,
                      label + " installed fixture must preserve exact canonical bytes");

        const auto result = operation(live);
        require(!result.ok, label + " must reject terminal revision");
        require_equal(result.code, octopoly::OperationError::revisionExhausted,
                      label + " must return typed revision exhaustion");
        require_same_mesh(live, before,
                          label + " revision exhaustion must preserve vertices, faces, IDs, and revision");
        const auto afterBytes = encodeProject(live);
        require(afterBytes.ok, label + " mesh must remain encodable after revision exhaustion");
        require_equal(afterBytes.bytes, beforeBytes.bytes,
                      label + " revision exhaustion must preserve exact encoded bytes");
    }
}

void penultimate_revision_advances_to_valid_deterministic_terminal_state() {
    const std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
    const auto fixture = cube_fixture_with_revision(maximum - 1);
    const std::vector<std::pair<std::string,
        std::function<octopoly::OperationResult(Mesh&)>>> operations{
        {"loop cut", [](Mesh& mesh) { return mesh.loopCut(mesh.faces().front().id); }},
        {"knife cut", [](Mesh& mesh) {
             return mesh.knifeCut(mesh.faces().front().id, 0, 0.25, 2, 0.75);
         }},
        {"inset", [](Mesh& mesh) { return mesh.insetFace(mesh.faces().front().id, 0.25); }},
        {"extrude", [](Mesh& mesh) {
             return mesh.extrudeFace(mesh.faces().front().id, {0.0, 0.0, -1.0});
         }},
        {"merge", [](Mesh& mesh) {
             return mesh.mergeVertices(mesh.vertices()[0].id, mesh.vertices()[1].id);
         }},
    };

    for (const auto& [label, operation] : operations) {
        const auto decoded = decodeProject(fixture);
        require(decoded.ok, label + " penultimate revision fixture must decode");

        Mesh live = Mesh::makeDefaultCube();
        const auto installed = installProject(live, fixture);
        require(installed.ok, label + " penultimate revision fixture must install");
        const auto result = operation(live);
        require(result.ok, label + " must allow a final mutation at UINT64_MAX-1: " + result.error);
        require_equal(live.revision(), maximum,
                      label + " must advance UINT64_MAX-1 exactly to UINT64_MAX");
        require(live.validate().ok, label + " terminal revision mesh must remain valid");

        const auto first = encodeProject(live);
        const auto second = encodeProject(live);
        require(first.ok && second.ok, label + " terminal revision mesh must encode repeatedly");
        require_equal(first.bytes, second.bytes,
                      label + " terminal revision encoding must be byte-for-byte deterministic");
        const auto roundTrip = decodeProject(first.bytes);
        require(roundTrip.ok, label + " terminal revision encoding must decode");
        require_same_mesh(roundTrip.mesh, live,
                          label + " terminal revision must round-trip vertices, faces, IDs, and revision");
        const auto roundTripBytes = encodeProject(roundTrip.mesh);
        require(roundTripBytes.ok, label + " round-tripped terminal revision must re-encode");
        require_equal(roundTripBytes.bytes, first.bytes,
                      label + " terminal revision must round-trip exact encoded bytes");

        const Mesh beforeFailure = live;
        const auto failed = operation(live);
        require(!failed.ok, label + " UINT64_MAX revision must be terminal after final mutation");
        require_equal(failed.code, octopoly::OperationError::revisionExhausted,
                      label + " terminal follow-up must return typed revision exhaustion");
        require_same_mesh(live, beforeFailure,
                          label + " terminal follow-up must preserve vertices, faces, IDs, and revision");
        const auto afterFailureBytes = encodeProject(live);
        require(afterFailureBytes.ok,
                label + " terminal mesh must remain encodable after rejected follow-up");
        require_equal(afterFailureBytes.bytes, first.bytes,
                      label + " terminal follow-up must preserve exact encoded bytes");
    }
}

void require_id_exhausted_atomic(
    const std::string& label, VertexId nextVertexId, FaceId nextFaceId,
    const std::function<octopoly::OperationResult(Mesh&)>& operation) {
    const auto fixture = cube_fixture_with_next_ids(nextVertexId, nextFaceId);
    const auto decoded = decodeProject(fixture);
    require(decoded.ok, label + " fixture must decode");
    require_equal(decoded.mesh.nextVertexId(), nextVertexId,
                  label + " decode preserves next vertex ID");
    require_equal(decoded.mesh.nextFaceId(), nextFaceId,
                  label + " decode preserves next face ID");

    Mesh live = Mesh::makeDefaultCube();
    require(live.extrudeFace(live.faces().front().id, {0.0, 0.0, -0.5}).ok,
            label + " live pre-install edit must succeed");
    const auto installed = installProject(live, fixture);
    require(installed.ok, label + " fixture must install");
    const Mesh before = live;
    const auto beforeBytes = encodeProject(live);
    require(beforeBytes.ok, label + " installed fixture must encode before edit");

    const auto result = operation(live);
    require(!result.ok, label + " must reject exhausted IDs");
    require_equal(result.code, octopoly::OperationError::idExhausted,
                  label + " must return typed ID exhaustion");
    require_same_mesh(live, before, label + " failure must preserve complete mesh state");
    const auto afterBytes = encodeProject(live);
    require(afterBytes.ok, label + " mesh must remain encodable after failure");
    require_equal(afterBytes.bytes, beforeBytes.bytes,
                  label + " failure must preserve exact encoded bytes");
}

void allocation_operations_reject_id_exhaustion_atomically_after_install() {
    constexpr std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();

    require_id_exhausted_atomic(
        "loop cut vertex capacity", maximum - 1, 7,
        [](Mesh& mesh) { return mesh.loopCut(mesh.faces().front().id); });
    require_id_exhausted_atomic(
        "knife cut face capacity", maximum - 2, maximum,
        [](Mesh& mesh) {
            return mesh.knifeCut(mesh.faces().front().id, 0, 0.25, 2, 0.75);
        });
    require_id_exhausted_atomic(
        "inset vertex capacity", maximum - 3, maximum - 4,
        [](Mesh& mesh) { return mesh.insetFace(mesh.faces().front().id, 0.25); });
    require_id_exhausted_atomic(
        "extrude face capacity", maximum - 4, maximum - 3,
        [](Mesh& mesh) {
            return mesh.extrudeFace(mesh.faces().front().id, {0.0, 0.0, -1.0});
        });
}

void boundary_allocation_leaves_valid_maximum_next_ids() {
    constexpr std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
    const auto fixture = cube_fixture_with_next_ids(maximum - 2, maximum - 1);
    Mesh live = Mesh::makeDefaultCube();
    const auto installed = installProject(live, fixture);
    require(installed.ok, "boundary fixture must install");

    const auto result = live.knifeCut(live.faces().front().id, 0, 0.25, 2, 0.75);
    require(result.ok, "boundary knife allocation must succeed: " + result.error);
    require_equal(result.createdVertices,
                  std::vector<VertexId>{maximum - 2, maximum - 1},
                  "boundary knife allocates only IDs below UINT64_MAX");
    require_equal(live.nextVertexId(), maximum,
                  "boundary knife leaves maximum as valid next vertex ID");
    require_equal(live.nextFaceId(), maximum,
                  "boundary knife leaves maximum as valid next face ID");
    require(live.validate().ok, "boundary allocation result must validate");
    const auto encoded = encodeProject(live);
    require(encoded.ok, "boundary allocation result must remain encodable");
    const auto decoded = decodeProject(encoded.bytes);
    require(decoded.ok, "boundary allocation result must remain decodable");
    require_same_mesh(decoded.mesh, live,
                      "boundary allocation must round-trip exactly");

    const Mesh beforeFailure = live;
    const auto failed = live.loopCut(live.faces().front().id);
    require(!failed.ok, "maximum next IDs must reject another allocation");
    require_equal(failed.code, octopoly::OperationError::idExhausted,
                  "maximum next IDs return typed ID exhaustion");
    require_same_mesh(live, beforeFailure,
                      "post-boundary exhaustion must remain atomic");
}

void merge_succeeds_with_terminal_next_ids_without_allocating() {
    constexpr std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
    const auto fixture = cube_fixture_with_next_ids(maximum, maximum);
    Mesh live = Mesh::makeDefaultCube();
    const auto installed = installProject(live, fixture);
    require(installed.ok, "terminal-ID merge fixture must install");

    const auto result = live.mergeVertices(live.vertices()[0].id,
                                           live.vertices()[1].id);
    require(result.ok, "merge must not require stable-ID capacity: " + result.error);
    require_equal(live.nextVertexId(), maximum,
                  "merge preserves terminal next vertex ID");
    require_equal(live.nextFaceId(), maximum,
                  "merge preserves terminal next face ID");
    require(live.validate().ok, "terminal-ID merge result must validate");
    const auto encoded = encodeProject(live);
    require(encoded.ok, "terminal-ID merge result must remain encodable");
}

struct TestCase {
    std::string name;
    std::function<void()> run;
};

}  // namespace

int main(int argc, char** argv) {
    const std::vector<TestCase> tests{
        {"default_cube_encoding_is_deterministic_with_golden_header",
         default_cube_encoding_is_deterministic_with_golden_header},
        {"round_trip_preserves_default_and_edited_state_and_future_ids",
         round_trip_preserves_default_and_edited_state_and_future_ids},
        {"every_truncation_and_single_bit_corruption_is_rejected",
         every_truncation_and_single_bit_corruption_is_rejected},
        {"malformed_headers_and_checksum_have_precise_typed_errors",
         malformed_headers_and_checksum_have_precise_typed_errors},
        {"crc_recomputed_structural_invalidity_is_rejected",
         crc_recomputed_structural_invalidity_is_rejected},
        {"sorted_id_validation_preserves_unusual_ids_and_precise_errors",
         sorted_id_validation_preserves_unusual_ids_and_precise_errors},
        {"decoded_unsorted_high_ids_resolve_positions_and_round_trip",
         decoded_unsorted_high_ids_resolve_positions_and_round_trip},
        {"large_reverse_id_lookup_and_streaming_traversal_is_exact",
         large_reverse_id_lookup_and_streaming_traversal_is_exact},
        {"injected_resource_limits_reject_before_allocation",
         injected_resource_limits_reject_before_allocation},
        {"fixed_seed_mutations_never_crash_and_always_return_a_typed_result",
         fixed_seed_mutations_never_crash_and_always_return_a_typed_result},
        {"failed_install_is_atomic_and_success_replaces_complete_state",
         failed_install_is_atomic_and_success_replaces_complete_state},
        {"allocation_operations_reject_id_exhaustion_atomically_after_install",
         allocation_operations_reject_id_exhaustion_atomically_after_install},
        {"boundary_allocation_leaves_valid_maximum_next_ids",
         boundary_allocation_leaves_valid_maximum_next_ids},
        {"merge_succeeds_with_terminal_next_ids_without_allocating",
         merge_succeeds_with_terminal_next_ids_without_allocating},
        {"terminal_revision_rejects_every_mutation_atomically_after_install",
         terminal_revision_rejects_every_mutation_atomically_after_install},
        {"penultimate_revision_advances_to_valid_deterministic_terminal_state",
         penultimate_revision_advances_to_valid_deterministic_terminal_state},
    };
    const std::string filter = argc > 1 ? argv[1] : "";
    int failures = 0;
    int executed = 0;
    for (const auto& test : tests) {
        if (!filter.empty() && test.name != filter) {
            continue;
        }
        ++executed;
        try {
            test.run();
            std::cout << "PASS " << test.name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL " << test.name << ": " << error.what() << '\n';
        }
    }
    if (executed == 0) {
        std::cerr << "No test matched filter: " << filter << '\n';
        return 2;
    }
    std::cout << executed << " test(s), " << failures << " failure(s)\n";
    return failures == 0 ? 0 : 1;
}
