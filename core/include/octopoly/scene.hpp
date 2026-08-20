#pragma once

#include "octopoly/mesh.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace octopoly {

namespace project {
struct SceneProjectAccess;
}

using ObjectId = std::uint64_t;

inline constexpr std::size_t kMaxObjectNameBytes = 255;
inline constexpr std::uint32_t kMinPrimitiveRadialSegments = 3;
inline constexpr std::uint32_t kMaxPrimitiveRadialSegments = 256;
inline constexpr std::uint32_t kMinPrimitiveRings = 2;
inline constexpr std::uint32_t kMaxPrimitiveRings = 256;

struct Quaternion {
    double x{};
    double y{};
    double z{};
    double w{1.0};

    friend bool operator==(const Quaternion&, const Quaternion&) = default;
};

struct Transform {
    Vec3 translation{};
    Quaternion rotation{};
    Vec3 scale{1.0, 1.0, 1.0};

    friend bool operator==(const Transform&, const Transform&) = default;
};

// Column-major 4x4 matrix for column vectors. Element (row, column) is
// values[column * 4 + row], and points are transformed as M * [x y z 1]^T.
struct Mat4 {
    std::array<double, 16> values{};

    [[nodiscard]] double at(std::size_t row, std::size_t column) const;
    [[nodiscard]] Vec3 transformPoint(Vec3 point) const noexcept;

    friend bool operator==(const Mat4&, const Mat4&) = default;
};

[[nodiscard]] bool isValidTransform(const Transform& transform) noexcept;
[[nodiscard]] bool isValidObjectName(std::string_view name) noexcept;
[[nodiscard]] Mat4 localTransformMatrix(const Transform& transform) noexcept;

enum class Primitive {
    cube,
    plane,
    tetrahedron,
    cylinder,
    cone,
    uvSphere,
};

struct PrimitiveParameters {
    std::uint32_t radialSegments{16};
    std::uint32_t rings{8};
};

enum class SceneError {
    none,
    notFound,
    invalidName,
    invalidTransform,
    unsupportedPrimitive,
    resourceLimit,
    idExhausted,
    revisionExhausted,
    invalidMesh,
    invalidScene,
};

struct PrimitiveMeshResult {
    bool ok{};
    SceneError code{SceneError::none};
    std::string error;
    Mesh mesh;
};

[[nodiscard]] PrimitiveMeshResult makePrimitiveMesh(
    Primitive primitive, PrimitiveParameters parameters = {});

struct SceneResult {
    bool ok{};
    SceneError code{SceneError::none};
    std::string error;
    ObjectId objectId{};
};

class SceneObject {
public:
    [[nodiscard]] ObjectId id() const noexcept;
    [[nodiscard]] const std::string& name() const noexcept;
    [[nodiscard]] const Mesh& mesh() const noexcept;
    [[nodiscard]] const Transform& localTransform() const noexcept;
    // Phase 4 intentionally has no parenting. World is exactly local T*R*S.
    [[nodiscard]] Mat4 worldTransform() const noexcept;

private:
    friend class Scene;
    friend struct project::SceneProjectAccess;

    ObjectId id_{};
    std::string name_;
    Mesh mesh_;
    Transform localTransform_{};
};

class Scene {
public:
    Scene() = default;
    Scene(const Scene&) = default;
    Scene& operator=(const Scene& other);
    Scene(Scene&&) noexcept = default;
    Scene& operator=(Scene&&) noexcept = default;

    [[nodiscard]] const std::vector<SceneObject>& objects() const noexcept;
    [[nodiscard]] const SceneObject* object(ObjectId id) const noexcept;
    [[nodiscard]] const SceneObject* selectedObject() const noexcept;
    [[nodiscard]] ObjectId selectedObjectId() const noexcept;
    [[nodiscard]] ObjectId nextObjectId() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;
    [[nodiscard]] ValidationResult validate() const;

    SceneResult createPrimitive(Primitive primitive, std::string name,
                                PrimitiveParameters parameters = {});
    // Adds a detached imported/legacy mesh without rewriting its stable IDs,
    // future-ID counters, or mesh revision. Success appends and selects it.
    SceneResult createMeshObject(Mesh mesh, std::string name);
    SceneResult deleteObject(ObjectId id);
    SceneResult renameObject(ObjectId id, std::string name);
    // ObjectId 0 clears selection. Re-selecting the current ID is a successful
    // no-op and does not advance the revision.
    SceneResult selectObject(ObjectId id);
    SceneResult setLocalTransform(ObjectId id, Transform transform);

    OperationResult selectedLoopCut(FaceId faceId);
    OperationResult selectedKnifeCut(FaceId faceId, std::size_t firstEdge,
                                     double firstT, std::size_t secondEdge,
                                     double secondT);
    OperationResult selectedInsetFace(FaceId faceId, double factor);
    OperationResult selectedMergeVertices(VertexId targetId, VertexId sourceId);
    OperationResult selectedExtrudeFace(FaceId faceId, Vec3 offset);

private:
    friend struct project::SceneProjectAccess;

    void rebuildObjectLookup();

    enum class MeshEditKind { loopCut, knifeCut, insetFace, mergeVertices, extrudeFace };
    struct MeshEditRequest {
        MeshEditKind kind{};
        FaceId faceId{};
        std::size_t firstEdge{};
        double firstT{};
        std::size_t secondEdge{};
        double secondT{};
        VertexId targetId{};
        VertexId sourceId{};
        Vec3 offset{};
    };
    OperationResult editSelectedMesh(const MeshEditRequest& request);

    std::vector<SceneObject> objects_;
    std::vector<std::pair<ObjectId, std::size_t>> objectLookup_;
    ObjectId selectedObjectId_{};
    ObjectId nextObjectId_{1};
    std::uint64_t revision_{};
};

}  // namespace octopoly
