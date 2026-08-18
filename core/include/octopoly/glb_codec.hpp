#pragma once

#include "octopoly/mesh.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace octopoly::glb {

enum class ErrorCategory {
    none,
    input,
    format,
    compatibility,
    unsupported,
    resource,
    topology,
    allocation,
    internal,
};

enum class EncodeErrorCode {
    none,
    invalidMesh,
    missingVertexReference,
    nonFinitePosition,
    positionOutOfFloatRange,
    tooManyVertices,
    tooManyTriangles,
    integerOverflow,
    allocationFailed,
    internalError,
};

struct EncodeError {
    ErrorCategory category{ErrorCategory::none};
    EncodeErrorCode code{EncodeErrorCode::none};
    std::size_t offset{};
    std::string_view message{};
};

enum class DecodeErrorCode {
    none,
    inputTooLarge,
    truncated,
    badMagic,
    unsupportedVersion,
    lengthMismatch,
    trailingBytes,
    integerOverflow,
    badChunkOrder,
    badChunkType,
    missingJsonChunk,
    missingBinChunk,
    invalidChunkPadding,
    jsonTooLarge,
    jsonSyntax,
    jsonTrailingGarbage,
    jsonDuplicateKey,
    jsonDepthLimit,
    jsonNodeLimit,
    jsonObjectMemberLimit,
    missingRequiredProperty,
    wrongPropertyType,
    unsupportedFeature,
    unsupportedExternalBuffer,
    unsupportedPrimitiveMode,
    unsupportedAccessor,
    primitiveLimitExceeded,
    vertexLimitExceeded,
    triangleLimitExceeded,
    bufferViewOutOfBounds,
    accessorOutOfBounds,
    invalidStride,
    invalidIndex,
    nonFinitePosition,
    meshValidationFailed,
    allocationFailed,
    internalError,
};

struct DecodeError {
    ErrorCategory category{ErrorCategory::none};
    DecodeErrorCode code{DecodeErrorCode::none};
    std::size_t offset{};
    std::string_view message{};
};

enum class DiagnosticCode {
    ignoredAttribute,
    ignoredMaterial,
};

struct Diagnostic {
    DiagnosticCode code{DiagnosticCode::ignoredAttribute};
    std::size_t offset{};
    std::string_view message{};

    friend bool operator==(const Diagnostic&, const Diagnostic&) = default;
};

struct LoadLimits {
    std::size_t maxBytes{64U * 1024U * 1024U};
    std::size_t maxJsonBytes{4U * 1024U * 1024U};
    std::size_t maxJsonDepth{128};
    std::size_t maxJsonNodes{1'000'000};
    std::size_t maxJsonObjectMembers{100'000};
    std::uint64_t maxVertices{1'000'000};
    std::uint64_t maxTriangles{1'000'000};
    std::uint64_t maxPrimitives{10'000};
};

struct EncodeResult {
    bool ok{};
    std::vector<std::uint8_t> bytes;
    EncodeError error;
};

struct DecodeResult {
    bool ok{};
    Mesh mesh;
    std::vector<Diagnostic> diagnostics;
    DecodeError error;
};

struct InstallResult {
    bool ok{};
    std::vector<Diagnostic> diagnostics;
    DecodeError error;
};

[[nodiscard]] EncodeResult encodeGlb(const Mesh& mesh) noexcept;
[[nodiscard]] DecodeResult decodeGlb(std::span<const std::uint8_t> bytes,
                                     LoadLimits limits = {}) noexcept;
[[nodiscard]] InstallResult installGlb(Mesh& liveMesh,
                                       std::span<const std::uint8_t> bytes,
                                       LoadLimits limits = {}) noexcept;

struct MeshGlbAccess;

}  // namespace octopoly::glb
