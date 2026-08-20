#include "octopoly/scene.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace octopoly {

struct SceneMeshAccess {
    static Mesh make(std::vector<Vertex> vertices, std::vector<Face> faces) {
        Mesh mesh;
        mesh.vertices_ = std::move(vertices);
        mesh.faces_ = std::move(faces);
        mesh.nextVertexId_ = static_cast<VertexId>(mesh.vertices_.size()) + 1;
        mesh.nextFaceId_ = static_cast<FaceId>(mesh.faces_.size()) + 1;
        mesh.revision_ = 0;
        mesh.rebuildVertexLookup();
        return mesh;
    }

    static OperationResult preflightLoopCut(const Mesh& mesh, FaceId faceId) {
        return mesh.preflightLoopCut(faceId);
    }

    static OperationResult preflightKnifeCut(
        const Mesh& mesh, FaceId faceId, std::size_t firstEdge, double firstT,
        std::size_t secondEdge, double secondT) {
        return mesh.preflightKnifeCut(faceId, firstEdge, firstT, secondEdge, secondT);
    }

    static OperationResult preflightInsetFace(
        const Mesh& mesh, FaceId faceId, double factor) {
        return mesh.preflightInsetFace(faceId, factor);
    }

    static OperationResult preflightMergeVertices(
        const Mesh& mesh, VertexId targetId, VertexId sourceId) {
        return mesh.preflightMergeVertices(targetId, sourceId);
    }

    static OperationResult preflightExtrudeFace(
        const Mesh& mesh, FaceId faceId, Vec3 offset) {
        return mesh.preflightExtrudeFace(faceId, offset);
    }
};

namespace {

constexpr double kQuaternionNormTolerance = 1.0e-12;

bool finite(double value) noexcept { return std::isfinite(value); }

bool finite(Vec3 value) noexcept {
    return finite(value.x) && finite(value.y) && finite(value.z);
}

double canonicalTrig(double value) noexcept {
    return std::abs(value) < 1.0e-15 ? 0.0 : value;
}

PrimitiveMeshResult primitiveFailure(SceneError code, const char* message) {
    return {false, code, message, {}};
}

PrimitiveMeshResult preflightPrimitive(Primitive primitive,
                                       PrimitiveParameters parameters) {
    switch (primitive) {
    case Primitive::cube:
    case Primitive::plane:
    case Primitive::tetrahedron:
        return {true, SceneError::none, {}, {}};
    case Primitive::cylinder:
    case Primitive::cone:
        if (parameters.radialSegments < kMinPrimitiveRadialSegments ||
            parameters.radialSegments > kMaxPrimitiveRadialSegments) {
            return primitiveFailure(SceneError::resourceLimit,
                                    "radial segments are outside the supported bounds");
        }
        return {true, SceneError::none, {}, {}};
    case Primitive::uvSphere:
        if (parameters.radialSegments < kMinPrimitiveRadialSegments ||
            parameters.radialSegments > kMaxPrimitiveRadialSegments ||
            parameters.rings < kMinPrimitiveRings ||
            parameters.rings > kMaxPrimitiveRings) {
            return primitiveFailure(SceneError::resourceLimit,
                                    "sphere tessellation is outside the supported bounds");
        }
        return {true, SceneError::none, {}, {}};
    }
    return primitiveFailure(SceneError::unsupportedPrimitive,
                            "primitive kind is not supported");
}

SceneResult sceneFailure(SceneError code, const char* message) {
    return {false, code, message, 0};
}

OperationResult meshEditFailure(OperationError code, const char* message) {
    return {false, code, message, {}, {}};
}

static_assert(std::is_nothrow_move_assignable_v<Scene>);
static_assert(std::is_nothrow_move_constructible_v<SceneResult>);
static_assert(std::is_nothrow_move_constructible_v<OperationResult>);

SceneResult commitCandidate(Scene& live, Scene&& candidate, SceneResult&& success) noexcept {
    live = std::move(candidate);
    return std::move(success);
}

std::vector<VertexId> sequentialLoop(std::uint32_t first, std::uint32_t count,
                                     bool reverse) {
    std::vector<VertexId> loop;
    loop.reserve(count);
    if (!reverse) {
        for (std::uint32_t index = 0; index < count; ++index) {
            loop.push_back(static_cast<VertexId>(first + index));
        }
    } else {
        for (std::uint32_t index = 0; index < count; ++index) {
            loop.push_back(static_cast<VertexId>(first + count - 1U - index));
        }
    }
    return loop;
}

}  // namespace

double Mat4::at(std::size_t row, std::size_t column) const {
    if (row >= 4 || column >= 4) {
        throw std::out_of_range("matrix row and column must be within [0, 4)");
    }
    return values[column * 4U + row];
}

Vec3 Mat4::transformPoint(Vec3 point) const noexcept {
    return {
        values[0] * point.x + values[4] * point.y + values[8] * point.z + values[12],
        values[1] * point.x + values[5] * point.y + values[9] * point.z + values[13],
        values[2] * point.x + values[6] * point.y + values[10] * point.z + values[14],
    };
}

bool isValidTransform(const Transform& transform) noexcept {
    if (!finite(transform.translation) || !finite(transform.scale) ||
        transform.scale.x == 0.0 || transform.scale.y == 0.0 ||
        transform.scale.z == 0.0) {
        return false;
    }
    const Quaternion& quaternion = transform.rotation;
    if (!finite(quaternion.x) || !finite(quaternion.y) || !finite(quaternion.z) ||
        !finite(quaternion.w)) {
        return false;
    }
    const double normSquared = quaternion.x * quaternion.x + quaternion.y * quaternion.y +
                               quaternion.z * quaternion.z + quaternion.w * quaternion.w;
    return finite(normSquared) &&
           std::abs(normSquared - 1.0) <= kQuaternionNormTolerance;
}

bool isValidObjectName(std::string_view name) noexcept {
    if (name.empty() || name.size() > kMaxObjectNameBytes) {
        return false;
    }
    std::size_t index = 0;
    while (index < name.size()) {
        const auto first = static_cast<unsigned char>(name[index]);
        if (first == 0) {
            return false;
        }
        if (first <= 0x7fU) {
            ++index;
            continue;
        }
        std::size_t length = 0;
        std::uint32_t codePoint = 0;
        std::uint32_t minimum = 0;
        if (first >= 0xc2U && first <= 0xdfU) {
            length = 2;
            codePoint = first & 0x1fU;
            minimum = 0x80U;
        } else if (first >= 0xe0U && first <= 0xefU) {
            length = 3;
            codePoint = first & 0x0fU;
            minimum = 0x800U;
        } else if (first >= 0xf0U && first <= 0xf4U) {
            length = 4;
            codePoint = first & 0x07U;
            minimum = 0x10000U;
        } else {
            return false;
        }
        if (length > name.size() - index) {
            return false;
        }
        for (std::size_t continuation = 1; continuation < length; ++continuation) {
            const auto byte = static_cast<unsigned char>(name[index + continuation]);
            if ((byte & 0xc0U) != 0x80U) {
                return false;
            }
            codePoint = (codePoint << 6U) | (byte & 0x3fU);
        }
        if (codePoint < minimum || codePoint > 0x10ffffU ||
            (codePoint >= 0xd800U && codePoint <= 0xdfffU)) {
            return false;
        }
        index += length;
    }
    return true;
}

Mat4 localTransformMatrix(const Transform& transform) noexcept {
    const Quaternion& q = transform.rotation;
    const double xx = q.x * q.x;
    const double yy = q.y * q.y;
    const double zz = q.z * q.z;
    const double xy = q.x * q.y;
    const double xz = q.x * q.z;
    const double yz = q.y * q.z;
    const double wx = q.w * q.x;
    const double wy = q.w * q.y;
    const double wz = q.w * q.z;
    const Vec3& scale = transform.scale;

    Mat4 matrix;
    matrix.values = {
        (1.0 - 2.0 * (yy + zz)) * scale.x,
        (2.0 * (xy + wz)) * scale.x,
        (2.0 * (xz - wy)) * scale.x,
        0.0,
        (2.0 * (xy - wz)) * scale.y,
        (1.0 - 2.0 * (xx + zz)) * scale.y,
        (2.0 * (yz + wx)) * scale.y,
        0.0,
        (2.0 * (xz + wy)) * scale.z,
        (2.0 * (yz - wx)) * scale.z,
        (1.0 - 2.0 * (xx + yy)) * scale.z,
        0.0,
        transform.translation.x,
        transform.translation.y,
        transform.translation.z,
        1.0,
    };
    return matrix;
}

PrimitiveMeshResult makePrimitiveMesh(Primitive primitive, PrimitiveParameters parameters) {
    PrimitiveMeshResult preflight = preflightPrimitive(primitive, parameters);
    if (!preflight.ok) {
        return preflight;
    }
    std::vector<Vertex> vertices;
    std::vector<Face> faces;
    const auto finish = [&vertices, &faces]() {
        Mesh mesh = SceneMeshAccess::make(std::move(vertices), std::move(faces));
        const ValidationResult validation = mesh.validate();
        if (!validation.ok) {
            return primitiveFailure(SceneError::invalidScene,
                                    "generated primitive failed mesh validation");
        }
        return PrimitiveMeshResult{true, SceneError::none, {}, std::move(mesh)};
    };

    switch (primitive) {
    case Primitive::cube:
        return {true, SceneError::none, {}, Mesh::makeDefaultCube()};
    case Primitive::plane:
        vertices = {{1, {-1.0, 0.0, -1.0}}, {2, {1.0, 0.0, -1.0}},
                    {3, {1.0, 0.0, 1.0}}, {4, {-1.0, 0.0, 1.0}}};
        faces = {{1, {1, 4, 3, 2}}};
        return finish();
    case Primitive::tetrahedron:
        vertices = {{1, {1.0, 1.0, 1.0}}, {2, {-1.0, -1.0, 1.0}},
                    {3, {-1.0, 1.0, -1.0}}, {4, {1.0, -1.0, -1.0}}};
        faces = {{1, {1, 3, 2}}, {2, {1, 2, 4}},
                 {3, {1, 4, 3}}, {4, {2, 3, 4}}};
        return finish();
    case Primitive::cylinder:
    case Primitive::cone:
        if (parameters.radialSegments < kMinPrimitiveRadialSegments ||
            parameters.radialSegments > kMaxPrimitiveRadialSegments) {
            return primitiveFailure(SceneError::resourceLimit,
                                    "radial segments are outside the supported bounds");
        }
        break;
    case Primitive::uvSphere:
        if (parameters.radialSegments < kMinPrimitiveRadialSegments ||
            parameters.radialSegments > kMaxPrimitiveRadialSegments ||
            parameters.rings < kMinPrimitiveRings ||
            parameters.rings > kMaxPrimitiveRings) {
            return primitiveFailure(SceneError::resourceLimit,
                                    "sphere tessellation is outside the supported bounds");
        }
        break;
    }

    const std::uint32_t radial = parameters.radialSegments;
    if (primitive == Primitive::cylinder || primitive == Primitive::cone) {
        const std::size_t vertexCount = primitive == Primitive::cylinder
                                            ? static_cast<std::size_t>(radial) * 2U
                                            : static_cast<std::size_t>(radial) + 1U;
        const std::size_t faceCount = static_cast<std::size_t>(radial) +
                                      (primitive == Primitive::cylinder ? 2U : 1U);
        vertices.reserve(vertexCount);
        faces.reserve(faceCount);
        for (std::uint32_t index = 0; index < radial; ++index) {
            const double angle = 2.0 * std::numbers::pi * static_cast<double>(index) /
                                 static_cast<double>(radial);
            vertices.push_back({static_cast<VertexId>(index + 1U),
                                {canonicalTrig(std::cos(angle)), -1.0,
                                 canonicalTrig(std::sin(angle))}});
        }
        faces.push_back({1, sequentialLoop(1, radial, false)});
        if (primitive == Primitive::cylinder) {
            for (std::uint32_t index = 0; index < radial; ++index) {
                const Vec3 bottom = vertices[index].position;
                vertices.push_back({static_cast<VertexId>(radial + index + 1U),
                                    {bottom.x, 1.0, bottom.z}});
            }
            faces.push_back({2, sequentialLoop(radial + 1U, radial, true)});
            for (std::uint32_t index = 0; index < radial; ++index) {
                const std::uint32_t next = (index + 1U) % radial;
                faces.push_back({static_cast<FaceId>(faces.size() + 1U),
                                 {static_cast<VertexId>(index + 1U),
                                  static_cast<VertexId>(radial + index + 1U),
                                  static_cast<VertexId>(radial + next + 1U),
                                  static_cast<VertexId>(next + 1U)}});
            }
        } else {
            const VertexId apex = static_cast<VertexId>(radial + 1U);
            vertices.push_back({apex, {0.0, 1.0, 0.0}});
            for (std::uint32_t index = 0; index < radial; ++index) {
                const std::uint32_t next = (index + 1U) % radial;
                faces.push_back({static_cast<FaceId>(faces.size() + 1U),
                                 {static_cast<VertexId>(index + 1U), apex,
                                  static_cast<VertexId>(next + 1U)}});
            }
        }
        return finish();
    }

    const std::uint32_t rings = parameters.rings;
    const std::size_t sphereVertexCount = 2U + static_cast<std::size_t>(radial) *
                                                   static_cast<std::size_t>(rings - 1U);
    const std::size_t sphereFaceCount = static_cast<std::size_t>(radial) * rings;
    vertices.reserve(sphereVertexCount);
    faces.reserve(sphereFaceCount);
    vertices.push_back({1, {0.0, 1.0, 0.0}});
    for (std::uint32_t ring = 1; ring < rings; ++ring) {
        const double latitude = std::numbers::pi * static_cast<double>(ring) /
                                static_cast<double>(rings);
        const double radius = canonicalTrig(std::sin(latitude));
        const double y = canonicalTrig(std::cos(latitude));
        for (std::uint32_t segment = 0; segment < radial; ++segment) {
            const double longitude = 2.0 * std::numbers::pi *
                                     static_cast<double>(segment) /
                                     static_cast<double>(radial);
            vertices.push_back({static_cast<VertexId>(vertices.size() + 1U),
                                {canonicalTrig(radius * std::cos(longitude)), y,
                                 canonicalTrig(radius * std::sin(longitude))}});
        }
    }
    const auto ringId = [radial](std::uint32_t ring, std::uint32_t segment) {
        return static_cast<VertexId>(2U + (ring - 1U) * radial + segment);
    };
    for (std::uint32_t segment = 0; segment < radial; ++segment) {
        const std::uint32_t next = (segment + 1U) % radial;
        faces.push_back({static_cast<FaceId>(faces.size() + 1U),
                         {1, ringId(1, next), ringId(1, segment)}});
    }
    for (std::uint32_t ring = 1; ring + 1U < rings; ++ring) {
        for (std::uint32_t segment = 0; segment < radial; ++segment) {
            const std::uint32_t next = (segment + 1U) % radial;
            faces.push_back({static_cast<FaceId>(faces.size() + 1U),
                             {ringId(ring, segment), ringId(ring, next),
                              ringId(ring + 1U, next), ringId(ring + 1U, segment)}});
        }
    }
    const VertexId bottom = static_cast<VertexId>(sphereVertexCount);
    vertices.push_back({bottom, {0.0, -1.0, 0.0}});
    for (std::uint32_t segment = 0; segment < radial; ++segment) {
        const std::uint32_t next = (segment + 1U) % radial;
        faces.push_back({static_cast<FaceId>(faces.size() + 1U),
                         {bottom, ringId(rings - 1U, segment),
                          ringId(rings - 1U, next)}});
    }
    return finish();
}

ObjectId SceneObject::id() const noexcept { return id_; }
const std::string& SceneObject::name() const noexcept { return name_; }
const Mesh& SceneObject::mesh() const noexcept { return mesh_; }
const Transform& SceneObject::localTransform() const noexcept { return localTransform_; }
Mat4 SceneObject::worldTransform() const noexcept {
    return localTransformMatrix(localTransform_);
}

Scene& Scene::operator=(const Scene& other) {
    if (this == &other) {
        return *this;
    }
    Scene candidate(other);
    *this = std::move(candidate);
    return *this;
}

const std::vector<SceneObject>& Scene::objects() const noexcept { return objects_; }

const SceneObject* Scene::object(ObjectId id) const noexcept {
    if (objectLookup_.size() != objects_.size()) {
        return nullptr;
    }
    const auto found = std::lower_bound(
        objectLookup_.begin(), objectLookup_.end(), id,
        [](const auto& entry, ObjectId sought) { return entry.first < sought; });
    if (found == objectLookup_.end() || found->first != id || found->second >= objects_.size()) {
        return nullptr;
    }
    const SceneObject& resolved = objects_[found->second];
    return resolved.id_ == id ? &resolved : nullptr;
}

const SceneObject* Scene::selectedObject() const noexcept {
    return selectedObjectId_ == 0 ? nullptr : object(selectedObjectId_);
}

ObjectId Scene::selectedObjectId() const noexcept { return selectedObjectId_; }
ObjectId Scene::nextObjectId() const noexcept { return nextObjectId_; }
std::uint64_t Scene::revision() const noexcept { return revision_; }

void Scene::rebuildObjectLookup() {
    std::vector<std::pair<ObjectId, std::size_t>> rebuilt;
    rebuilt.reserve(objects_.size());
    for (std::size_t index = 0; index < objects_.size(); ++index) {
        rebuilt.emplace_back(objects_[index].id_, index);
    }
    std::sort(rebuilt.begin(), rebuilt.end());
    objectLookup_ = std::move(rebuilt);
}

ValidationResult Scene::validate() const {
    if (objectLookup_.size() != objects_.size()) {
        return {false, "object lookup index must be complete"};
    }
    for (std::size_t index = 0; index < objectLookup_.size(); ++index) {
        const auto [id, storageIndex] = objectLookup_[index];
        if (id == 0 || storageIndex >= objects_.size() || objects_[storageIndex].id_ != id) {
            return {false, "object lookup index must map nonzero IDs to stored objects"};
        }
        if (index != 0 && objectLookup_[index - 1].first >= id) {
            return {false, "object lookup index must be sorted with unique IDs"};
        }
    }
    for (std::size_t storageIndex = 0; storageIndex < objects_.size(); ++storageIndex) {
        const SceneObject& item = objects_[storageIndex];
        if (!isValidObjectName(item.name_)) {
            return {false, "object names must be nonempty bounded strict UTF-8 without NUL"};
        }
        if (!isValidTransform(item.localTransform_)) {
            return {false, "object local transforms must be finite normalized nonzero-scale TRS"};
        }
        if (!item.mesh_.validate().ok) {
            return {false, "every object mesh must validate"};
        }
        const SceneObject* resolved = object(item.id_);
        if (resolved != &item) {
            return {false, "object lookup index must be complete"};
        }
    }
    if (nextObjectId_ == 0 ||
        (!objectLookup_.empty() && nextObjectId_ <= objectLookup_.back().first)) {
        return {false, "next object ID must be nonzero and greater than existing IDs"};
    }
    if (selectedObjectId_ != 0 && object(selectedObjectId_) == nullptr) {
        return {false, "selected object ID must be zero or reference a stored object"};
    }
    return {true, {}};
}

SceneResult Scene::createPrimitive(Primitive primitive, std::string name,
                                   PrimitiveParameters parameters) {
    if (!isValidObjectName(name)) {
        return sceneFailure(SceneError::invalidName, "object name is not valid bounded UTF-8");
    }
    PrimitiveMeshResult preflight = preflightPrimitive(primitive, parameters);
    if (!preflight.ok) {
        return {false, preflight.code, std::move(preflight.error), 0};
    }
    if (revision_ == std::numeric_limits<std::uint64_t>::max()) {
        return sceneFailure(SceneError::revisionExhausted,
                            "scene revision is terminal");
    }
    if (nextObjectId_ == std::numeric_limits<ObjectId>::max()) {
        return sceneFailure(SceneError::idExhausted,
                            "object ID cursor is terminal");
    }
    PrimitiveMeshResult generated = makePrimitiveMesh(primitive, parameters);
    if (!generated.ok) {
        return {false, generated.code, std::move(generated.error), 0};
    }

    Scene candidate = *this;
    const ObjectId created = candidate.nextObjectId_++;
    SceneObject createdObject;
    createdObject.id_ = created;
    createdObject.name_ = std::move(name);
    createdObject.mesh_ = std::move(generated.mesh);
    candidate.objects_.push_back(std::move(createdObject));
    candidate.selectedObjectId_ = created;
    candidate.rebuildObjectLookup();
    const ValidationResult validation = candidate.validate();
    if (!validation.ok) {
        return sceneFailure(SceneError::invalidScene,
                            "created scene candidate failed validation");
    }
    ++candidate.revision_;
    SceneResult success{true, SceneError::none, {}, created};
    return commitCandidate(*this, std::move(candidate), std::move(success));
}

SceneResult Scene::createMeshObject(Mesh mesh, std::string name) {
    if (!isValidObjectName(name)) {
        return sceneFailure(SceneError::invalidName, "object name is not valid bounded UTF-8");
    }
    const ValidationResult meshValidation = mesh.validate();
    if (!meshValidation.ok) {
        return sceneFailure(SceneError::invalidMesh, "imported object mesh is invalid");
    }
    if (revision_ == std::numeric_limits<std::uint64_t>::max()) {
        return sceneFailure(SceneError::revisionExhausted,
                            "scene revision is terminal");
    }
    if (nextObjectId_ == std::numeric_limits<ObjectId>::max()) {
        return sceneFailure(SceneError::idExhausted,
                            "object ID cursor is terminal");
    }

    Scene candidate = *this;
    const ObjectId created = candidate.nextObjectId_++;
    SceneObject createdObject;
    createdObject.id_ = created;
    createdObject.name_ = std::move(name);
    createdObject.mesh_ = std::move(mesh);
    candidate.objects_.push_back(std::move(createdObject));
    candidate.selectedObjectId_ = created;
    candidate.rebuildObjectLookup();
    const ValidationResult validation = candidate.validate();
    if (!validation.ok) {
        return sceneFailure(SceneError::invalidScene,
                            "imported scene candidate failed validation");
    }
    ++candidate.revision_;
    SceneResult success{true, SceneError::none, {}, created};
    return commitCandidate(*this, std::move(candidate), std::move(success));
}

SceneResult Scene::deleteObject(ObjectId id) {
    const SceneObject* existing = object(id);
    if (existing == nullptr) {
        return sceneFailure(SceneError::notFound, "object does not exist");
    }
    if (revision_ == std::numeric_limits<std::uint64_t>::max()) {
        return sceneFailure(SceneError::revisionExhausted,
                            "scene revision is terminal");
    }
    const std::size_t storageIndex = static_cast<std::size_t>(existing - objects_.data());
    Scene candidate = *this;
    candidate.objects_.erase(candidate.objects_.begin() +
                             static_cast<std::ptrdiff_t>(storageIndex));
    if (candidate.selectedObjectId_ == id) {
        if (storageIndex < candidate.objects_.size()) {
            candidate.selectedObjectId_ = candidate.objects_[storageIndex].id_;
        } else if (!candidate.objects_.empty()) {
            candidate.selectedObjectId_ = candidate.objects_.back().id_;
        } else {
            candidate.selectedObjectId_ = 0;
        }
    }
    candidate.rebuildObjectLookup();
    if (!candidate.validate().ok) {
        return sceneFailure(SceneError::invalidScene,
                            "deleted scene candidate failed validation");
    }
    ++candidate.revision_;
    SceneResult success{true, SceneError::none, {}, id};
    return commitCandidate(*this, std::move(candidate), std::move(success));
}

SceneResult Scene::renameObject(ObjectId id, std::string name) {
    if (object(id) == nullptr) {
        return sceneFailure(SceneError::notFound, "object does not exist");
    }
    if (!isValidObjectName(name)) {
        return sceneFailure(SceneError::invalidName, "object name is not valid bounded UTF-8");
    }
    if (revision_ == std::numeric_limits<std::uint64_t>::max()) {
        return sceneFailure(SceneError::revisionExhausted,
                            "scene revision is terminal");
    }
    Scene candidate = *this;
    const auto found = std::lower_bound(
        candidate.objectLookup_.begin(), candidate.objectLookup_.end(), id,
        [](const auto& entry, ObjectId sought) { return entry.first < sought; });
    candidate.objects_[found->second].name_ = std::move(name);
    if (!candidate.validate().ok) {
        return sceneFailure(SceneError::invalidScene,
                            "renamed scene candidate failed validation");
    }
    ++candidate.revision_;
    SceneResult success{true, SceneError::none, {}, id};
    return commitCandidate(*this, std::move(candidate), std::move(success));
}

SceneResult Scene::selectObject(ObjectId id) {
    if (id != 0 && object(id) == nullptr) {
        return sceneFailure(SceneError::notFound, "object does not exist");
    }
    if (id == selectedObjectId_) {
        return {true, SceneError::none, {}, id};
    }
    if (revision_ == std::numeric_limits<std::uint64_t>::max()) {
        return sceneFailure(SceneError::revisionExhausted,
                            "scene revision is terminal");
    }
    Scene candidate = *this;
    candidate.selectedObjectId_ = id;
    if (!candidate.validate().ok) {
        return sceneFailure(SceneError::invalidScene,
                            "selection scene candidate failed validation");
    }
    ++candidate.revision_;
    SceneResult success{true, SceneError::none, {}, id};
    return commitCandidate(*this, std::move(candidate), std::move(success));
}

SceneResult Scene::setLocalTransform(ObjectId id, Transform transform) {
    if (object(id) == nullptr) {
        return sceneFailure(SceneError::notFound, "object does not exist");
    }
    if (!isValidTransform(transform)) {
        return sceneFailure(SceneError::invalidTransform,
                            "local transform must be finite normalized nonzero-scale TRS");
    }
    if (revision_ == std::numeric_limits<std::uint64_t>::max()) {
        return sceneFailure(SceneError::revisionExhausted,
                            "scene revision is terminal");
    }
    Scene candidate = *this;
    const auto found = std::lower_bound(
        candidate.objectLookup_.begin(), candidate.objectLookup_.end(), id,
        [](const auto& entry, ObjectId sought) { return entry.first < sought; });
    candidate.objects_[found->second].localTransform_ = transform;
    if (!candidate.validate().ok) {
        return sceneFailure(SceneError::invalidScene,
                            "transformed scene candidate failed validation");
    }
    ++candidate.revision_;
    SceneResult success{true, SceneError::none, {}, id};
    return commitCandidate(*this, std::move(candidate), std::move(success));
}

OperationResult Scene::selectedLoopCut(FaceId faceId) {
    return editSelectedMesh({MeshEditKind::loopCut, faceId});
}

OperationResult Scene::selectedKnifeCut(FaceId faceId, std::size_t firstEdge,
                                        double firstT, std::size_t secondEdge,
                                        double secondT) {
    MeshEditRequest request;
    request.kind = MeshEditKind::knifeCut;
    request.faceId = faceId;
    request.firstEdge = firstEdge;
    request.firstT = firstT;
    request.secondEdge = secondEdge;
    request.secondT = secondT;
    return editSelectedMesh(request);
}

OperationResult Scene::selectedInsetFace(FaceId faceId, double factor) {
    MeshEditRequest request;
    request.kind = MeshEditKind::insetFace;
    request.faceId = faceId;
    request.firstT = factor;
    return editSelectedMesh(request);
}

OperationResult Scene::selectedMergeVertices(VertexId targetId, VertexId sourceId) {
    MeshEditRequest request;
    request.kind = MeshEditKind::mergeVertices;
    request.targetId = targetId;
    request.sourceId = sourceId;
    return editSelectedMesh(request);
}

OperationResult Scene::selectedExtrudeFace(FaceId faceId, Vec3 offset) {
    MeshEditRequest request;
    request.kind = MeshEditKind::extrudeFace;
    request.faceId = faceId;
    request.offset = offset;
    return editSelectedMesh(request);
}

OperationResult Scene::editSelectedMesh(const MeshEditRequest& request) {
    const SceneObject* selected = selectedObject();
    if (selected == nullptr) {
        return meshEditFailure(OperationError::notFound,
                               "no selected object is available for mesh editing");
    }
    OperationResult preflight;
    switch (request.kind) {
    case MeshEditKind::loopCut:
        preflight = SceneMeshAccess::preflightLoopCut(selected->mesh_, request.faceId);
        break;
    case MeshEditKind::knifeCut:
        preflight = SceneMeshAccess::preflightKnifeCut(
            selected->mesh_, request.faceId, request.firstEdge, request.firstT,
            request.secondEdge, request.secondT);
        break;
    case MeshEditKind::insetFace:
        preflight = SceneMeshAccess::preflightInsetFace(
            selected->mesh_, request.faceId, request.firstT);
        break;
    case MeshEditKind::mergeVertices:
        preflight = SceneMeshAccess::preflightMergeVertices(
            selected->mesh_, request.targetId, request.sourceId);
        break;
    case MeshEditKind::extrudeFace:
        preflight = SceneMeshAccess::preflightExtrudeFace(
            selected->mesh_, request.faceId, request.offset);
        break;
    }
    if (!preflight.ok) {
        return preflight;
    }
    if (revision_ == std::numeric_limits<std::uint64_t>::max()) {
        return meshEditFailure(OperationError::revisionExhausted,
                               "scene revision is terminal");
    }

    Scene candidate = *this;
    const auto found = std::lower_bound(
        candidate.objectLookup_.begin(), candidate.objectLookup_.end(),
        candidate.selectedObjectId_,
        [](const auto& entry, ObjectId sought) { return entry.first < sought; });
    Mesh& mesh = candidate.objects_[found->second].mesh_;
    OperationResult result;
    switch (request.kind) {
    case MeshEditKind::loopCut:
        result = mesh.loopCut(request.faceId);
        break;
    case MeshEditKind::knifeCut:
        result = mesh.knifeCut(request.faceId, request.firstEdge, request.firstT,
                               request.secondEdge, request.secondT);
        break;
    case MeshEditKind::insetFace:
        result = mesh.insetFace(request.faceId, request.firstT);
        break;
    case MeshEditKind::mergeVertices:
        result = mesh.mergeVertices(request.targetId, request.sourceId);
        break;
    case MeshEditKind::extrudeFace:
        result = mesh.extrudeFace(request.faceId, request.offset);
        break;
    }
    if (!result.ok) {
        return result;
    }
    if (!candidate.validate().ok) {
        return meshEditFailure(OperationError::topologyInvalid,
                               "edited scene candidate failed validation");
    }
    ++candidate.revision_;
    *this = std::move(candidate);
    return result;
}

}  // namespace octopoly
