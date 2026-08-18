#pragma once

#include "octopoly/mesh.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace octopoly::project {

enum class EncodeErrorCode {
    none,
    invalidMesh,
    integerOverflow,
    allocationFailed,
    internalError,
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

}  // namespace octopoly::project
