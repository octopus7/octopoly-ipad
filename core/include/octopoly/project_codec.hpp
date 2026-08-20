#pragma once

#include "octopoly/mesh.hpp"
#include "octopoly/scene.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace octopoly::project {

enum class EncodeErrorCode {
    none = 0,
    invalidMesh = 1,
    integerOverflow = 2,
    allocationFailed = 3,
    internalError = 4,
    invalidScene = 5,
};

struct EncodeError {
    EncodeErrorCode code{EncodeErrorCode::none};
    std::string_view message{};
};

struct EncodeResult {
    bool ok{};
    std::vector<std::uint8_t> bytes;
    EncodeError error;
};

enum class DecodeErrorCategory {
    none,
    input,
    format,
    compatibility,
    integrity,
    resource,
    topology,
    allocation,
    internal,
};

enum class DecodeErrorCode {
    none,
    inputTooLarge,
    badMagic,
    unsupportedVersion,
    unsupportedEndian,
    nonzeroReserved,
    truncated,
    trailingBytes,
    checksumMismatch,
    integerOverflow,
    malformedLength,
    malformedCount,
    vertexLimitExceeded,
    faceLimitExceeded,
    cornerLimitExceeded,
    zeroVertexId,
    duplicateVertexId,
    nonFinitePosition,
    zeroFaceId,
    duplicateFaceId,
    invalidFaceLoop,
    missingVertexReference,
    nextVertexIdInvalid,
    nextFaceIdInvalid,
    meshValidationFailed,
    allocationFailed,
    internalError,
};

struct DecodeError {
    DecodeErrorCategory category{DecodeErrorCategory::none};
    DecodeErrorCode code{DecodeErrorCode::none};
    std::size_t offset{};
    std::string_view message{};
};

struct LoadLimits {
    std::size_t maxBytes{64U * 1024U * 1024U};
    std::uint64_t maxVertices{1'000'000};
    std::uint64_t maxFaces{1'000'000};
    std::uint64_t maxFaceCorners{4'000'000};
};

struct DecodeResult {
    bool ok{};
    Mesh mesh;
    DecodeError error;
};

struct InstallResult {
    bool ok{};
    DecodeError error;
};

[[nodiscard]] EncodeResult encodeProject(const Mesh& mesh) noexcept;
[[nodiscard]] DecodeResult decodeProject(std::span<const std::uint8_t> bytes,
                                         LoadLimits limits = {}) noexcept;
[[nodiscard]] InstallResult installProject(Mesh& liveMesh,
                                           std::span<const std::uint8_t> bytes,
                                           LoadLimits limits = {}) noexcept;

// Canonical scene container v1.0. The legacy Mesh v1.0 API and bytes above are
// independent and unchanged.
enum class SceneDecodeErrorCode {
    none,
    inputTooLarge,
    badMagic,
    unsupportedVersion,
    unsupportedEndian,
    nonzeroReserved,
    truncated,
    trailingBytes,
    checksumMismatch,
    integerOverflow,
    malformedLength,
    malformedCount,
    objectLimitExceeded,
    nameLimitExceeded,
    aggregateVertexLimitExceeded,
    aggregateFaceLimitExceeded,
    aggregateCornerLimitExceeded,
    zeroObjectId,
    duplicateObjectId,
    invalidObjectName,
    invalidTransform,
    nestedMeshInvalid,
    aggregateCountMismatch,
    selectedObjectInvalid,
    nextObjectIdInvalid,
    sceneValidationFailed,
    allocationFailed,
    internalError,
};

struct SceneDecodeError {
    DecodeErrorCategory category{DecodeErrorCategory::none};
    SceneDecodeErrorCode code{SceneDecodeErrorCode::none};
    std::size_t offset{};
    std::string_view message{};
};

struct SceneLoadLimits {
    std::size_t maxBytes{128U * 1024U * 1024U};
    std::uint64_t maxObjects{10'000};
    std::uint64_t maxNameBytes{kMaxObjectNameBytes};
    std::uint64_t maxAggregateVertices{1'000'000};
    std::uint64_t maxAggregateFaces{1'000'000};
    std::uint64_t maxAggregateFaceCorners{4'000'000};
};

struct SceneDecodeResult {
    bool ok{};
    Scene scene;
    SceneDecodeError error;
};

struct SceneInstallResult {
    bool ok{};
    SceneDecodeError error;
};

[[nodiscard]] EncodeResult encodeSceneProject(const Scene& scene) noexcept;
[[nodiscard]] SceneDecodeResult decodeSceneProject(
    std::span<const std::uint8_t> bytes, SceneLoadLimits limits = {}) noexcept;
[[nodiscard]] SceneInstallResult installSceneProject(
    Scene& liveScene, std::span<const std::uint8_t> bytes,
    SceneLoadLimits limits = {}) noexcept;

}  // namespace octopoly::project
