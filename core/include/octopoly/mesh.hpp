#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace octopoly {

using VertexId = std::uint64_t;
using FaceId = std::uint64_t;

struct Vec3 {
    double x{};
    double y{};
    double z{};

    friend bool operator==(const Vec3&, const Vec3&) = default;
};

struct Vertex {
    VertexId id{};
    Vec3 position{};

    friend bool operator==(const Vertex&, const Vertex&) = default;
};

struct Face {
    FaceId id{};
    std::vector<VertexId> vertices;

    friend bool operator==(const Face&, const Face&) = default;
};

struct Triangle {
    FaceId sourceFace{};
    std::array<VertexId, 3> vertices{};

    friend bool operator==(const Triangle&, const Triangle&) = default;
};

enum class OperationError {
    none,
    notFound,
    unsupported,
    invalidArgument,
    topologyInvalid,
};

struct OperationResult {
    bool ok{};
    OperationError code{OperationError::none};
    std::string error;
    std::vector<VertexId> createdVertices;
    std::vector<FaceId> affectedFaces;
};

struct ValidationResult {
    bool ok{};
    std::string error;
};

class Mesh {
public:
    static Mesh makeDefaultCube();

    [[nodiscard]] const std::vector<Vertex>& vertices() const noexcept;
    [[nodiscard]] const std::vector<Face>& faces() const noexcept;
    [[nodiscard]] const Vertex* vertex(VertexId id) const noexcept;
    [[nodiscard]] const Face* face(FaceId id) const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;
    [[nodiscard]] std::vector<Triangle> triangulate() const;
    [[nodiscard]] ValidationResult validate() const;

    OperationResult loopCut(FaceId faceId);
    OperationResult knifeCut(FaceId faceId, std::size_t firstEdge, double firstT,
                             std::size_t secondEdge, double secondT);
    OperationResult insetFace(FaceId faceId, double factor);
    // Phase 1 contract: extrudes one existing polygon by a finite, nonzero
    // translation. The source face ID becomes the translated cap, one vertex
    // is created per source corner, and one quad is created per source edge.
    // Adjacent faces are not propagated. Failure leaves the mesh unchanged.
    OperationResult extrudeFace(FaceId faceId, Vec3 offset);
    OperationResult mergeVertices(VertexId targetId, VertexId sourceId);

private:
    std::vector<Vertex> vertices_;
    std::vector<Face> faces_;
    VertexId nextVertexId_{1};
    FaceId nextFaceId_{1};
    std::uint64_t revision_{0};
};

}  // namespace octopoly
