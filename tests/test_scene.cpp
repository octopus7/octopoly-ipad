#include "octopoly/scene.hpp"
#include "octopoly/project_codec.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using octopoly::Mat4;
using octopoly::Mesh;
using octopoly::ObjectId;
using octopoly::OperationError;
using octopoly::Primitive;
using octopoly::PrimitiveParameters;
using octopoly::Quaternion;
using octopoly::Scene;
using octopoly::SceneError;
using octopoly::SceneResult;
using octopoly::Transform;
using octopoly::Vec3;

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

void require_near(double actual, double expected, double tolerance, const std::string& message) {
    if (std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(message);
    }
}

void require_mesh_contract(const Mesh& mesh, std::size_t vertices, std::size_t faces,
                           const std::string& label) {
    require(mesh.validate().ok, label + " validates");
    require_equal(mesh.vertices().size(), vertices, label + " vertex count");
    require_equal(mesh.faces().size(), faces, label + " face count");
    require_equal(mesh.nextVertexId(), static_cast<std::uint64_t>(vertices + 1),
                  label + " next vertex ID");
    require_equal(mesh.nextFaceId(), static_cast<std::uint64_t>(faces + 1),
                  label + " next face ID");
    require_equal(mesh.revision(), std::uint64_t{0}, label + " revision");
    for (std::size_t index = 0; index < mesh.vertices().size(); ++index) {
        require_equal(mesh.vertices()[index].id, static_cast<std::uint64_t>(index + 1),
                      label + " sequential vertex IDs");
    }
    for (std::size_t index = 0; index < mesh.faces().size(); ++index) {
        require_equal(mesh.faces()[index].id, static_cast<std::uint64_t>(index + 1),
                      label + " sequential face IDs");
    }
}

void primitive_factories_are_deterministic_polygons() {
    const auto cube = octopoly::makePrimitiveMesh(Primitive::cube);
    const auto plane = octopoly::makePrimitiveMesh(Primitive::plane);
    const auto tetrahedron = octopoly::makePrimitiveMesh(Primitive::tetrahedron);
    const auto cylinder = octopoly::makePrimitiveMesh(Primitive::cylinder, {8, 4});
    const auto cone = octopoly::makePrimitiveMesh(Primitive::cone, {8, 4});
    const auto sphere = octopoly::makePrimitiveMesh(Primitive::uvSphere, {8, 4});
    for (const auto* result : {&cube, &plane, &tetrahedron, &cylinder, &cone, &sphere}) {
        require(result->ok, "every supported primitive factory succeeds");
    }
    require_mesh_contract(cube.mesh, 8, 6, "cube");
    require_mesh_contract(plane.mesh, 4, 1, "plane");
    require_mesh_contract(tetrahedron.mesh, 4, 4, "tetrahedron");
    require_mesh_contract(cylinder.mesh, 16, 10, "8-segment cylinder");
    require_mesh_contract(cone.mesh, 9, 9, "8-segment cone");
    require_mesh_contract(sphere.mesh, 26, 32, "8x4 UV sphere");

    require_equal(plane.mesh.vertices()[0].position, Vec3{-1.0, 0.0, -1.0},
                  "plane first position is stable");
    require_equal(plane.mesh.faces()[0].vertices,
                  std::vector<std::uint64_t>{1, 4, 3, 2},
                  "plane winding points toward +Y");
    require_equal(cylinder.mesh.faces()[0].vertices,
                  std::vector<std::uint64_t>{1, 2, 3, 4, 5, 6, 7, 8},
                  "cylinder bottom is one deterministic polygon cap");
    require_equal(cylinder.mesh.faces()[1].vertices,
                  std::vector<std::uint64_t>{16, 15, 14, 13, 12, 11, 10, 9},
                  "cylinder top is one deterministic polygon cap");
    require_equal(cylinder.mesh.faces()[2].vertices,
                  std::vector<std::uint64_t>{1, 9, 10, 2},
                  "cylinder first side is an outward quad");
    require_equal(cone.mesh.faces()[1].vertices,
                  std::vector<std::uint64_t>{1, 9, 2},
                  "cone first side is an outward triangle");
    require_equal(sphere.mesh.faces()[0].vertices,
                  std::vector<std::uint64_t>{1, 3, 2},
                  "sphere first north-pole triangle winding is stable");

    const auto cylinderAgain = octopoly::makePrimitiveMesh(Primitive::cylinder, {8, 4});
    require(cylinderAgain.ok, "repeat cylinder succeeds");
    require_equal(cylinderAgain.mesh.vertices(), cylinder.mesh.vertices(),
                  "primitive vertex bytes/order are deterministic");
    require_equal(cylinderAgain.mesh.faces(), cylinder.mesh.faces(),
                  "primitive face loops/order are deterministic");
}

void primitive_parameter_boundaries_are_typed_and_preflighted() {
    for (const Primitive unknown : {static_cast<Primitive>(6),
                                    static_cast<Primitive>(255)}) {
        for (const PrimitiveParameters parameters : {
                 PrimitiveParameters{}, PrimitiveParameters{8, 0},
                 PrimitiveParameters{std::numeric_limits<std::uint32_t>::max(),
                                     std::numeric_limits<std::uint32_t>::max()}}) {
            const auto result = octopoly::makePrimitiveMesh(unknown, parameters);
            require(!result.ok, "unknown primitive kind fails closed");
            require_equal(result.code, SceneError::unsupportedPrimitive,
                          "unknown primitive kind has a typed error");
        }
    }
    for (const std::uint32_t segments : {0U, 2U, octopoly::kMaxPrimitiveRadialSegments + 1U}) {
        const auto result = octopoly::makePrimitiveMesh(Primitive::cylinder, {segments, 4});
        require(!result.ok, "invalid radial segment count fails");
        require_equal(result.code, SceneError::resourceLimit,
                      "invalid radial segment count is typed");
    }
    for (const std::uint32_t rings : {0U, 1U, octopoly::kMaxPrimitiveRings + 1U}) {
        const auto result = octopoly::makePrimitiveMesh(Primitive::uvSphere, {8, rings});
        require(!result.ok, "invalid sphere ring count fails");
        require_equal(result.code, SceneError::resourceLimit,
                      "invalid sphere ring count is typed");
    }
    const auto minimum = octopoly::makePrimitiveMesh(Primitive::uvSphere, {3, 2});
    require(minimum.ok, "minimum sphere tessellation succeeds");
    require_mesh_contract(minimum.mesh, 5, 6, "minimum sphere");
    const auto maximum = octopoly::makePrimitiveMesh(
        Primitive::uvSphere,
        {octopoly::kMaxPrimitiveRadialSegments, octopoly::kMaxPrimitiveRings});
    require(maximum.ok, "maximum bounded sphere tessellation succeeds");
    const std::size_t expectedVertices = 2U +
        static_cast<std::size_t>(octopoly::kMaxPrimitiveRadialSegments) *
        static_cast<std::size_t>(octopoly::kMaxPrimitiveRings - 1U);
    const std::size_t expectedFaces =
        static_cast<std::size_t>(octopoly::kMaxPrimitiveRadialSegments) *
        static_cast<std::size_t>(octopoly::kMaxPrimitiveRings);
    require_mesh_contract(maximum.mesh, expectedVertices, expectedFaces, "maximum sphere");
}

void scene_storage_lookup_selection_and_delete_are_deterministic() {
    static_assert(std::is_nothrow_move_constructible_v<Scene>);
    static_assert(std::is_nothrow_move_assignable_v<Scene>);

    Scene scene;
    require(scene.validate().ok, "empty scene validates");
    require_equal(scene.nextObjectId(), ObjectId{1}, "empty scene starts at object ID 1");
    require_equal(scene.revision(), std::uint64_t{0}, "empty scene revision is zero");
    require(scene.selectedObject() == nullptr, "empty scene has no selection");

    const auto first = scene.createPrimitive(Primitive::cube, "Cube");
    const auto second = scene.createPrimitive(Primitive::plane, "Plane");
    const auto third = scene.createPrimitive(Primitive::tetrahedron, "Tetrahedron");
    require(first.ok && second.ok && third.ok, "three scene objects are created");
    require_equal(std::array<ObjectId, 3>{first.objectId, second.objectId, third.objectId},
                  std::array<ObjectId, 3>{1, 2, 3}, "object IDs are stable and sequential");
    require_equal(scene.objects().size(), std::size_t{3}, "storage order is insertion order");
    require_equal(scene.revision(), std::uint64_t{3}, "each creation advances one revision");
    require_equal(scene.selectedObjectId(), ObjectId{3}, "creation selects the new object");
    require(scene.object(2) == &scene.objects()[1], "binary lookup resolves storage object");
    require(scene.object(99) == nullptr, "missing object lookup fails closed");

    require(scene.selectObject(2).ok, "selection change succeeds");
    require_equal(scene.revision(), std::uint64_t{4}, "selection change advances revision");
    require(scene.deleteObject(2).ok, "selected middle object deletion succeeds");
    require_equal(scene.selectedObjectId(), ObjectId{3},
                  "deleting selected object selects next storage object");
    require_equal(scene.objects()[0].id(), ObjectId{1}, "first object stays first");
    require_equal(scene.objects()[1].id(), ObjectId{3}, "third object shifts into second slot");
    require(scene.deleteObject(3).ok, "selected last object deletion succeeds");
    require_equal(scene.selectedObjectId(), ObjectId{1},
                  "deleting selected last object selects previous object");
    require(scene.deleteObject(1).ok, "last object deletion succeeds");
    require_equal(scene.selectedObjectId(), ObjectId{0}, "deleting final object clears selection");
    require(scene.validate().ok, "edited scene validates");
}

void transforms_use_column_major_trs_and_reject_invalid_values_atomically() {
    Scene scene;
    const auto created = scene.createPrimitive(Primitive::cube, "Transform Cube");
    require(created.ok, "transform fixture creation succeeds");
    const double rootHalf = std::sqrt(0.5);
    const Transform transform{{10.0, 20.0, 30.0}, {0.0, 0.0, rootHalf, rootHalf},
                              {2.0, 3.0, 4.0}};
    require(scene.setLocalTransform(created.objectId, transform).ok,
            "finite normalized nonzero TRS succeeds");
    const auto* object = scene.object(created.objectId);
    require(object != nullptr, "transformed object resolves");
    const Mat4 matrix = object->worldTransform();
    const std::array<double, 16> expected{
        0.0, 2.0, 0.0, 0.0,
        -3.0, 0.0, 0.0, 0.0,
        0.0, 0.0, 4.0, 0.0,
        10.0, 20.0, 30.0, 1.0,
    };
    for (std::size_t index = 0; index < expected.size(); ++index) {
        require_near(matrix.values[index], expected[index], 1.0e-12,
                     "column-major T*R*S matrix golden value");
    }
    const Vec3 point = matrix.transformPoint({1.0, 2.0, 3.0});
    require_near(point.x, 4.0, 1.0e-12, "transformed point x");
    require_near(point.y, 22.0, 1.0e-12, "transformed point y");
    require_near(point.z, 42.0, 1.0e-12, "transformed point z");

    const auto beforeRevision = scene.revision();
    const Transform before = object->localTransform();
    const std::vector<Transform> invalid{
        {{std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0}, {}, {1.0, 1.0, 1.0}},
        {{}, {0.0, 0.0, 0.0, 0.0}, {1.0, 1.0, 1.0}},
        {{}, {0.0, 0.0, 0.0, 2.0}, {1.0, 1.0, 1.0}},
        {{}, {}, {0.0, 1.0, 1.0}},
        {{}, {}, {1.0, std::numeric_limits<double>::infinity(), 1.0}},
    };
    for (const Transform& rejected : invalid) {
        const auto result = scene.setLocalTransform(created.objectId, rejected);
        require(!result.ok, "invalid transform fails");
        require_equal(result.code, SceneError::invalidTransform,
                      "invalid transform has typed failure");
        require_equal(scene.object(created.objectId)->localTransform(), before,
                      "invalid transform preserves local TRS");
        require_equal(scene.revision(), beforeRevision,
                      "invalid transform preserves revision");
    }
}

void matrix_access_rejects_out_of_range_indices() {
    const Mat4 matrix = octopoly::localTransformMatrix({});
    for (const auto [row, column] :
         {std::pair<std::size_t, std::size_t>{4, 0}, {0, 4}, {4, 3}, {3, 4}}) {
        bool rejected = false;
        try {
            static_cast<void>(matrix.at(row, column));
        } catch (const std::out_of_range&) {
            rejected = true;
        }
        require(rejected, "matrix access outside 4x4 bounds throws out_of_range");
    }
}

void names_and_missing_ids_fail_atomically_and_successes_advance_once() {
    Scene scene;
    const auto created = scene.createPrimitive(Primitive::cube, "Cube");
    require(created.ok, "rename fixture creation succeeds");
    const auto baseline = scene.revision();
    require(scene.renameObject(created.objectId, "Renamed Cube \xe2\x9c\x93").ok,
            "valid UTF-8 rename succeeds");
    require_equal(scene.object(created.objectId)->name(),
                  std::string{"Renamed Cube \xe2\x9c\x93"},
                  "renamed object stores exact UTF-8");
    require_equal(scene.revision(), baseline + 1, "rename advances exactly once");

    const std::string oldName = scene.object(created.objectId)->name();
    const auto oldRevision = scene.revision();
    const std::vector<std::string> invalidNames{
        {}, std::string(octopoly::kMaxObjectNameBytes + 1U, 'a'), std::string{"\xc0\xaf", 2},
        std::string{"a\0b", 3},
    };
    for (const std::string& name : invalidNames) {
        const auto result = scene.renameObject(created.objectId, name);
        require(!result.ok, "invalid name fails");
        require_equal(result.code, SceneError::invalidName, "invalid name is typed");
        require_equal(scene.object(created.objectId)->name(), oldName,
                      "invalid name preserves object");
        require_equal(scene.revision(), oldRevision, "invalid name preserves revision");
    }
    require_equal(scene.deleteObject(999).code, SceneError::notFound,
                  "delete missing object is typed");
    require_equal(scene.selectObject(999).code, SceneError::notFound,
                  "select missing object is typed");
    require_equal(scene.setLocalTransform(999, {}).code, SceneError::notFound,
                  "transform missing object is typed");
    require_equal(scene.revision(), oldRevision, "missing-ID operations are atomic");
}

void imported_mesh_creation_is_transactional_and_preserves_mesh_state() {
    Mesh imported = Mesh::makeDefaultCube();
    require(imported.extrudeFace(imported.faces().front().id, {0.0, 0.0, -0.5}).ok,
            "import fixture edit succeeds");
    const Mesh expected = imported;

    Scene scene;
    require(scene.createPrimitive(Primitive::plane, "Existing").ok,
            "existing scene object creation succeeds");
    const auto beforeRevision = scene.revision();
    const auto beforeNextObjectId = scene.nextObjectId();
    const auto created = scene.createMeshObject(std::move(imported), "Imported GLB");
    require(created.ok, "valid detached imported mesh is accepted");
    require_equal(created.objectId, beforeNextObjectId,
                  "import returns the allocated stable object ID");
    require_equal(scene.revision(), beforeRevision + 1,
                  "import advances scene revision exactly once");
    require_equal(scene.nextObjectId(), beforeNextObjectId + 1,
                  "import advances object cursor exactly once");
    require_equal(scene.selectedObjectId(), created.objectId,
                  "import selects the new object");
    const auto* object = scene.object(created.objectId);
    require(object != nullptr, "imported object resolves by stable ID");
    require_equal(object->name(), std::string{"Imported GLB"},
                  "import stores the exact valid UTF-8 name");
    require_equal(object->mesh().vertices(), expected.vertices(),
                  "import preserves mesh vertex IDs and positions");
    require_equal(object->mesh().faces(), expected.faces(),
                  "import preserves mesh face IDs and loops");
    require_equal(object->mesh().nextVertexId(), expected.nextVertexId(),
                  "import preserves mesh vertex counter");
    require_equal(object->mesh().nextFaceId(), expected.nextFaceId(),
                  "import preserves mesh face counter");
    require_equal(object->mesh().revision(), expected.revision(),
                  "import preserves owned mesh revision");

    const auto canonicalBefore = octopoly::project::encodeSceneProject(scene);
    require(canonicalBefore.ok, "imported scene baseline encodes");
    const auto failed = scene.createMeshObject(Mesh::makeDefaultCube(), "");
    require(!failed.ok && failed.code == SceneError::invalidName,
            "invalid imported object name fails with typed error");
    const auto canonicalAfter = octopoly::project::encodeSceneProject(scene);
    require(canonicalAfter.ok && canonicalAfter.bytes == canonicalBefore.bytes,
            "failed imported mesh creation preserves exact canonical scene bytes");
}

void selected_mesh_edits_are_atomic_and_advance_both_revisions_once() {
    Scene empty;
    const std::array<octopoly::OperationResult, 5> noSelection{
        empty.selectedLoopCut(1),
        empty.selectedKnifeCut(1, 0, 0.25, 2, 0.75),
        empty.selectedInsetFace(1, 0.25),
        empty.selectedMergeVertices(1, 2),
        empty.selectedExtrudeFace(1, {0.0, 0.0, -0.5}),
    };
    for (const auto& result : noSelection) {
        require(!result.ok && result.code == octopoly::OperationError::notFound,
                "every selected edit rejects no selection with a typed error");
        require(result.error.find("selected") != std::string::npos,
                "no-selection error text is bridge-usable");
    }
    require_equal(empty.revision(), std::uint64_t{0},
                  "no-selection failures preserve scene revision");

    Scene failedScene;
    require(failedScene.createPrimitive(Primitive::cube, "Cube").ok,
            "failed-edit fixture creation succeeds");
    const auto failedBefore = octopoly::project::encodeSceneProject(failedScene);
    require(failedBefore.ok, "failed-edit baseline encodes");
    const auto failedMeshRevision = failedScene.selectedObject()->mesh().revision();
    const auto failed = failedScene.selectedLoopCut(999);
    require(!failed.ok && failed.code == octopoly::OperationError::notFound,
            "selected mesh operation failure remains typed");
    const auto failedAfter = octopoly::project::encodeSceneProject(failedScene);
    require(failedAfter.ok && failedAfter.bytes == failedBefore.bytes,
            "selected mesh operation failure preserves exact canonical scene bytes");
    require_equal(failedScene.selectedObject()->mesh().revision(), failedMeshRevision,
                  "selected mesh operation failure preserves mesh revision");

    const auto require_success = [](Scene scene, const auto& operation,
                                    const std::string& label) {
        const auto sceneRevision = scene.revision();
        const auto meshRevision = scene.selectedObject()->mesh().revision();
        const auto result = operation(scene);
        require(result.ok, label + " succeeds: " + result.error);
        require_equal(scene.revision(), sceneRevision + 1,
                      label + " advances scene revision exactly once");
        require_equal(scene.selectedObject()->mesh().revision(), meshRevision + 1,
                      label + " advances owned mesh revision exactly once");
        require(scene.validate().ok, label + " leaves complete scene valid");
        return result;
    };

    Scene source;
    require(source.createPrimitive(Primitive::cube, "Cube").ok,
            "successful-edit fixture creation succeeds");
    const auto loop = require_success(source,
        [](Scene& scene) { return scene.selectedLoopCut(1); }, "selected loop cut");
    require(!loop.createdVertices.empty() && !loop.affectedFaces.empty(),
            "loop cut preserves created and affected IDs");
    const auto knife = require_success(source,
        [](Scene& scene) { return scene.selectedKnifeCut(1, 0, 0.25, 2, 0.75); },
        "selected knife cut");
    require(!knife.createdVertices.empty() && !knife.affectedFaces.empty(),
            "knife preserves created and affected IDs");
    const auto inset = require_success(source,
        [](Scene& scene) { return scene.selectedInsetFace(1, 0.25); }, "selected inset");
    require(!inset.createdVertices.empty() && !inset.affectedFaces.empty(),
            "inset preserves created and affected IDs");
    const auto merge = require_success(source,
        [](Scene& scene) { return scene.selectedMergeVertices(1, 2); }, "selected merge");
    require(!merge.affectedFaces.empty(), "merge preserves affected face IDs");
    const auto extrude = require_success(source,
        [](Scene& scene) { return scene.selectedExtrudeFace(1, {0.0, 0.0, -0.5}); },
        "selected extrude");
    require(!extrude.createdVertices.empty() && !extrude.affectedFaces.empty(),
            "extrude preserves created and affected IDs");
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

std::uint64_t read_u64(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    std::uint64_t value = 0;
    for (unsigned index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(bytes[offset + index]) << (index * 8U);
    }
    return value;
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

void refresh_scene_crc(std::vector<std::uint8_t>& bytes) {
    write_u32(bytes, 24, crc32(bytes.data() + 32, bytes.size() - 32));
}

void require_same_scene(const Scene& actual, const Scene& expected,
                        const std::string& message) {
    require_equal(actual.selectedObjectId(), expected.selectedObjectId(),
                  message + " selected ID");
    require_equal(actual.nextObjectId(), expected.nextObjectId(),
                  message + " next ID");
    require_equal(actual.revision(), expected.revision(), message + " revision");
    require_equal(actual.objects().size(), expected.objects().size(), message + " count");
    for (std::size_t index = 0; index < expected.objects().size(); ++index) {
        const auto& left = actual.objects()[index];
        const auto& right = expected.objects()[index];
        require_equal(left.id(), right.id(), message + " object ID");
        require_equal(left.name(), right.name(), message + " object name");
        require_equal(left.localTransform(), right.localTransform(), message + " local TRS");
        require_equal(left.mesh().vertices(), right.mesh().vertices(), message + " mesh vertices");
        require_equal(left.mesh().faces(), right.mesh().faces(), message + " mesh faces");
        require_equal(left.mesh().nextVertexId(), right.mesh().nextVertexId(),
                      message + " mesh next vertex ID");
        require_equal(left.mesh().nextFaceId(), right.mesh().nextFaceId(),
                      message + " mesh next face ID");
        require_equal(left.mesh().revision(), right.mesh().revision(),
                      message + " mesh revision");
    }
}

Scene make_codec_fixture() {
    Scene scene;
    require(scene.createPrimitive(Primitive::cube, "Cube").ok,
            "codec cube fixture creation");
    require(scene.createPrimitive(Primitive::uvSphere, "Sphere", {8, 4}).ok,
            "codec sphere fixture creation");
    require(scene.setLocalTransform(1, {{1.0, 2.0, 3.0}, {}, {-2.0, 3.0, 4.0}}).ok,
            "codec transform fixture");
    require(scene.selectObject(1).ok, "codec selection fixture");
    return scene;
}

void scene_codec_is_canonical_and_round_trips_complete_state() {
    Scene one;
    require(one.createPrimitive(Primitive::cube, "Cube").ok,
            "single-object scene creation succeeds");
    const auto first = octopoly::project::encodeSceneProject(one);
    const auto second = octopoly::project::encodeSceneProject(one);
    require(first.ok && second.ok, "single-object scene encoding succeeds");
    require_equal(first.bytes, second.bytes, "scene encoding is byte deterministic");
    require_equal(first.bytes.size(), std::size_t{820}, "scene golden byte size");
    const std::array<std::uint8_t, 16> goldenPrefix{
        'O', 'C', 'T', 'O', 'S', 'C', 'N', 'E',
        1, 0, 0, 0, 1, 0, 0, 0,
    };
    require(std::equal(goldenPrefix.begin(), goldenPrefix.end(), first.bytes.begin()),
            "scene envelope magic/version/endian are canonical");
    require_equal(read_u64(first.bytes, 16), std::uint64_t{788},
                  "scene payload length is canonical little endian");
    require_equal(read_u64(first.bytes, 32), std::uint64_t{1},
                  "scene object count field");
    require_equal(read_u64(first.bytes, 40), ObjectId{1}, "scene selected ID field");
    require_equal(read_u64(first.bytes, 48), ObjectId{2}, "scene next object ID field");
    require_equal(read_u64(first.bytes, 56), std::uint64_t{1}, "scene revision field");
    require_equal(read_u64(first.bytes, 64), std::uint64_t{8},
                  "scene aggregate vertex count field");
    require_equal(read_u64(first.bytes, 72), std::uint64_t{6},
                  "scene aggregate face count field");
    require_equal(read_u64(first.bytes, 80), std::uint64_t{24},
                  "scene aggregate corner count field");

    const Scene expected = make_codec_fixture();
    const auto encoded = octopoly::project::encodeSceneProject(expected);
    require(encoded.ok, "complete scene fixture encodes");
    auto decoded = octopoly::project::decodeSceneProject(encoded.bytes);
    require(decoded.ok, "complete scene fixture decodes");
    require(decoded.scene.validate().ok, "decoded scene validates including indexes");
    require_same_scene(decoded.scene, expected, "scene round trip");
    const auto reencoded = octopoly::project::encodeSceneProject(decoded.scene);
    require(reencoded.ok, "decoded scene re-encodes");
    require_equal(reencoded.bytes, encoded.bytes, "scene round trip preserves canonical bytes");
}

void scene_codec_rejects_every_truncation_and_corruption() {
    const auto encoded = octopoly::project::encodeSceneProject(make_codec_fixture());
    require(encoded.ok, "corruption scene fixture encodes");
    for (std::size_t length = 0; length < encoded.bytes.size(); ++length) {
        const std::vector<std::uint8_t> prefix(
            encoded.bytes.begin(), encoded.bytes.begin() + static_cast<std::ptrdiff_t>(length));
        const auto decoded = octopoly::project::decodeSceneProject(prefix);
        require(!decoded.ok, "every strict scene prefix is rejected");
        require(decoded.error.code != octopoly::project::SceneDecodeErrorCode::none,
                "every scene truncation has a typed error");
    }
    for (std::size_t offset = 0; offset < encoded.bytes.size(); ++offset) {
        auto changed = encoded.bytes;
        changed[offset] ^= 1U;
        const auto decoded = octopoly::project::decodeSceneProject(changed);
        require(!decoded.ok, "single-bit scene corruption is rejected at every byte");
        require(decoded.error.code != octopoly::project::SceneDecodeErrorCode::none,
                "scene corruption has a typed error");
    }
}

void scene_codec_rejects_semantic_errors_limits_and_bad_nested_meshes() {
    Scene two;
    require(two.createPrimitive(Primitive::cube, "A").ok, "first semantic fixture object");
    require(two.createPrimitive(Primitive::cube, "B").ok, "second semantic fixture object");
    const auto encoded = octopoly::project::encodeSceneProject(two);
    require(encoded.ok, "semantic scene fixture encodes");
    const std::size_t secondObjectOffset = 88U + 104U + 1U + 624U;

    const auto check = [&](std::size_t offset, std::uint64_t value,
                           octopoly::project::SceneDecodeErrorCode expected,
                           const std::string& label) {
        auto changed = encoded.bytes;
        write_u64(changed, offset, value);
        refresh_scene_crc(changed);
        const auto decoded = octopoly::project::decodeSceneProject(changed);
        require(!decoded.ok, label + " fails");
        require_equal(decoded.error.code, expected, label + " typed code");
        require(decoded.error.offset <= changed.size(), label + " typed offset");
    };
    check(88, 0, octopoly::project::SceneDecodeErrorCode::zeroObjectId,
          "zero object ID");
    check(secondObjectOffset, 1, octopoly::project::SceneDecodeErrorCode::duplicateObjectId,
          "duplicate object ID");
    check(40, 999, octopoly::project::SceneDecodeErrorCode::selectedObjectInvalid,
          "missing selected object");
    check(48, 2, octopoly::project::SceneDecodeErrorCode::nextObjectIdInvalid,
          "next object ID not above maximum");
    check(64, 15, octopoly::project::SceneDecodeErrorCode::aggregateCountMismatch,
          "aggregate vertex mismatch");

    auto badName = encoded.bytes;
    badName[192] = 0xffU;
    refresh_scene_crc(badName);
    require_equal(octopoly::project::decodeSceneProject(badName).error.code,
                  octopoly::project::SceneDecodeErrorCode::invalidObjectName,
                  "invalid UTF-8 name is typed");

    auto badTransform = encoded.bytes;
    write_f64(badTransform, 160, 0.0);
    refresh_scene_crc(badTransform);
    require_equal(octopoly::project::decodeSceneProject(badTransform).error.code,
                  octopoly::project::SceneDecodeErrorCode::invalidTransform,
                  "zero scale is typed");

    auto badNested = encoded.bytes;
    const std::size_t nestedMeshOffset = 193;
    badNested[nestedMeshOffset + 100] ^= 0x80U;
    refresh_scene_crc(badNested);
    require_equal(octopoly::project::decodeSceneProject(badNested).error.code,
                  octopoly::project::SceneDecodeErrorCode::nestedMeshInvalid,
                  "nested mesh checksum failure is typed");

    auto limits = octopoly::project::SceneLoadLimits{};
    limits.maxObjects = 1;
    require_equal(octopoly::project::decodeSceneProject(encoded.bytes, limits).error.code,
                  octopoly::project::SceneDecodeErrorCode::objectLimitExceeded,
                  "object count limit is checked before object allocation");
    limits = {};
    limits.maxAggregateVertices = 15;
    require_equal(octopoly::project::decodeSceneProject(encoded.bytes, limits).error.code,
                  octopoly::project::SceneDecodeErrorCode::aggregateVertexLimitExceeded,
                  "aggregate vertex limit is checked before mesh allocation");
}

void decoded_unusual_object_ids_use_sorted_lookup_and_round_trip_exactly() {
    Scene scene;
    require(scene.createPrimitive(Primitive::cube, "A").ok, "high-ID first object");
    require(scene.createPrimitive(Primitive::cube, "B").ok, "high-ID second object");
    auto encoded = octopoly::project::encodeSceneProject(scene);
    require(encoded.ok, "high-ID base scene encodes");
    constexpr ObjectId firstId = UINT64_C(0xf000000000000001);
    constexpr ObjectId secondId = UINT64_C(0x8000000000000001);
    const std::size_t secondObjectOffset = 88U + 104U + 1U + 624U;
    write_u64(encoded.bytes, 88, firstId);
    write_u64(encoded.bytes, secondObjectOffset, secondId);
    write_u64(encoded.bytes, 40, secondId);
    write_u64(encoded.bytes, 48, std::numeric_limits<ObjectId>::max());
    refresh_scene_crc(encoded.bytes);

    const auto decoded = octopoly::project::decodeSceneProject(encoded.bytes);
    require(decoded.ok, "unsorted unusual object IDs decode");
    require(decoded.scene.object(firstId) == &decoded.scene.objects()[0],
            "first unusual ID resolves to storage position zero");
    require(decoded.scene.object(secondId) == &decoded.scene.objects()[1],
            "second unusual ID resolves to storage position one");
    require_equal(decoded.scene.selectedObjectId(), secondId,
                  "unusual selected ID is preserved");
    const auto reencoded = octopoly::project::encodeSceneProject(decoded.scene);
    require(reencoded.ok, "unusual-ID scene re-encodes");
    require_equal(reencoded.bytes, encoded.bytes,
                  "derived sorted index does not alter storage-order wire bytes");
}

void large_reverse_object_id_lookup_is_exact_without_timing_assumptions() {
    constexpr std::size_t objectCount = 512;
    constexpr ObjectId base = UINT64_C(0x4000000000000000);
    Scene scene;
    for (std::size_t index = 0; index < objectCount; ++index) {
        require(scene.createPrimitive(Primitive::plane, "A").ok,
                "large lookup source object creation");
    }
    auto encoded = octopoly::project::encodeSceneProject(scene);
    require(encoded.ok, "large lookup source scene encodes");
    constexpr std::size_t recordSize = 104U + 1U + 256U;
    for (std::size_t index = 0; index < objectCount; ++index) {
        const ObjectId id = base + objectCount - index;
        write_u64(encoded.bytes, 88U + recordSize * index, id);
    }
    write_u64(encoded.bytes, 40, base + 1U);
    write_u64(encoded.bytes, 48, base + objectCount + 1U);
    refresh_scene_crc(encoded.bytes);
    const auto decoded = octopoly::project::decodeSceneProject(encoded.bytes);
    require(decoded.ok, "large reverse-ID scene decodes");
    require_equal(decoded.scene.objects().size(), objectCount, "large scene object count");
    for (std::size_t offset = 1; offset <= objectCount; ++offset) {
        const ObjectId id = base + offset;
        const auto* object = decoded.scene.object(id);
        require(object != nullptr, "every large-scene stable ID resolves");
        require(object == &decoded.scene.objects()[objectCount - offset],
                "every large-scene ID maps to exact reverse storage index");
    }
    const auto reencoded = octopoly::project::encodeSceneProject(decoded.scene);
    require(reencoded.ok && reencoded.bytes == encoded.bytes,
            "large derived lookup leaves storage-order bytes unchanged");
}

void fixed_seed_scene_mutations_are_typed_and_safe() {
    const auto encoded = octopoly::project::encodeSceneProject(make_codec_fixture());
    require(encoded.ok, "fixed-seed scene mutation fixture encodes");
    std::mt19937_64 random(UINT64_C(0x4f43544f53434e45));
    for (std::size_t iteration = 0; iteration < 1'000; ++iteration) {
        auto mutation = encoded.bytes;
        switch (iteration % 3U) {
        case 0:
            mutation[static_cast<std::size_t>(random() % mutation.size())] ^=
                static_cast<std::uint8_t>(1U << (random() % 8U));
            break;
        case 1:
            mutation.resize(static_cast<std::size_t>(random() % mutation.size()));
            break;
        default:
            mutation.push_back(static_cast<std::uint8_t>(random()));
            break;
        }
        const auto decoded = octopoly::project::decodeSceneProject(mutation);
        if (decoded.ok) {
            require(decoded.scene.validate().ok,
                    "successful fixed-seed mutation decode returns valid scene");
        } else {
            require(decoded.error.code != octopoly::project::SceneDecodeErrorCode::none,
                    "failed fixed-seed mutation decode is typed");
            require(decoded.error.offset <= mutation.size(),
                    "fixed-seed mutation error offset is within input");
        }
    }
}

void terminal_object_cursor_allows_last_allocation_then_fails_atomically() {
    Scene scene;
    require(scene.createPrimitive(Primitive::cube, "Cube").ok,
            "terminal object cursor base scene");
    auto encoded = octopoly::project::encodeSceneProject(scene);
    require(encoded.ok, "terminal object cursor base encodes");
    write_u64(encoded.bytes, 48, std::numeric_limits<ObjectId>::max() - 1U);
    refresh_scene_crc(encoded.bytes);
    auto decoded = octopoly::project::decodeSceneProject(encoded.bytes);
    require(decoded.ok, "last-allocatable object cursor decodes");
    const auto last = decoded.scene.createPrimitive(Primitive::plane, "Last");
    require(last.ok, "last allocatable object ID succeeds");
    require_equal(last.objectId, std::numeric_limits<ObjectId>::max() - 1U,
                  "last allocation returns exact stable object ID");
    require_equal(decoded.scene.nextObjectId(), std::numeric_limits<ObjectId>::max(),
                  "successful last allocation reaches terminal cursor");
    const auto before = octopoly::project::encodeSceneProject(decoded.scene);
    const auto failed = decoded.scene.createPrimitive(Primitive::plane, "Never");
    require(!failed.ok && failed.code == SceneError::idExhausted,
            "terminal object cursor rejects another creation");
    const auto after = octopoly::project::encodeSceneProject(decoded.scene);
    require(before.ok && after.ok && before.bytes == after.bytes,
            "terminal object-ID failure preserves canonical scene bytes");
}

void scene_install_is_atomic_and_terminal_revision_fails_mutations() {
    Scene replacement = make_codec_fixture();
    const auto encoded = octopoly::project::encodeSceneProject(replacement);
    require(encoded.ok, "replacement scene encodes");
    Scene live;
    require(live.createPrimitive(Primitive::cone, "Live", {6, 4}).ok,
            "live install fixture creation");
    const auto installed = octopoly::project::installSceneProject(live, encoded.bytes);
    require(installed.ok, "valid scene installs");
    require_same_scene(live, replacement, "successful scene install");

    const auto before = octopoly::project::encodeSceneProject(live);
    auto corrupted = encoded.bytes;
    corrupted.back() ^= 1U;
    const auto failed = octopoly::project::installSceneProject(live, corrupted);
    require(!failed.ok, "corrupted scene install fails");
    const auto after = octopoly::project::encodeSceneProject(live);
    require(before.ok && after.ok && before.bytes == after.bytes,
            "failed scene install preserves exact canonical live bytes");

    auto terminalBytes = encoded.bytes;
    write_u64(terminalBytes, 56, std::numeric_limits<std::uint64_t>::max());
    refresh_scene_crc(terminalBytes);
    auto terminal = octopoly::project::decodeSceneProject(terminalBytes);
    require(terminal.ok, "terminal scene revision decodes as valid state");
    const auto terminalBefore = octopoly::project::encodeSceneProject(terminal.scene);
    const auto invalidParameters = terminal.scene.createPrimitive(
        Primitive::uvSphere, "Invalid", {8, 0});
    require(!invalidParameters.ok && invalidParameters.code == SceneError::resourceLimit,
            "primitive resource validation precedes terminal revision rejection");
    const auto unknownPrimitive = terminal.scene.createPrimitive(
        static_cast<Primitive>(255), "Invalid");
    require(!unknownPrimitive.ok &&
                unknownPrimitive.code == SceneError::unsupportedPrimitive,
            "primitive kind validation precedes terminal revision rejection");
    require_equal(terminal.scene.selectedLoopCut(999).code, OperationError::notFound,
                  "Loop Cut face validation precedes terminal Scene revision");
    require_equal(terminal.scene.selectedKnifeCut(1, 0, 0.5, 1, 0.5).code,
                  OperationError::invalidArgument,
                  "Knife edge validation precedes terminal Scene revision");
    require_equal(terminal.scene.selectedInsetFace(1, 0.0).code,
                  OperationError::invalidArgument,
                  "Inset factor validation precedes terminal Scene revision");
    require_equal(terminal.scene.selectedMergeVertices(1, 1).code,
                  OperationError::invalidArgument,
                  "Merge ID validation precedes terminal Scene revision");
    require_equal(terminal.scene.selectedExtrudeFace(1, {}).code,
                  OperationError::invalidArgument,
                  "Extrude offset validation precedes terminal Scene revision");
    const Transform identity{};
    const std::array<SceneResult, 5> failures{
        terminal.scene.createPrimitive(Primitive::plane, "Nope"),
        terminal.scene.deleteObject(1),
        terminal.scene.renameObject(1, "Nope"),
        terminal.scene.selectObject(2),
        terminal.scene.setLocalTransform(1, identity),
    };
    for (const auto& result : failures) {
        require(!result.ok, "terminal revision rejects every mutation path");
        require_equal(result.code, SceneError::revisionExhausted,
                      "terminal mutation has typed revision error");
    }
    const auto terminalAfter = octopoly::project::encodeSceneProject(terminal.scene);
    require(terminalBefore.ok && terminalAfter.ok &&
                terminalBefore.bytes == terminalAfter.bytes,
            "terminal failures preserve canonical scene bytes");
}

struct TestCase {
    const char* name;
    std::function<void()> run;
};

}  // namespace

int main(int argc, char** argv) {
    const std::vector<TestCase> tests{
        {"primitive_factories_are_deterministic_polygons", primitive_factories_are_deterministic_polygons},
        {"primitive_parameter_boundaries_are_typed_and_preflighted", primitive_parameter_boundaries_are_typed_and_preflighted},
        {"scene_storage_lookup_selection_and_delete_are_deterministic", scene_storage_lookup_selection_and_delete_are_deterministic},
        {"transforms_use_column_major_trs_and_reject_invalid_values_atomically", transforms_use_column_major_trs_and_reject_invalid_values_atomically},
        {"matrix_access_rejects_out_of_range_indices", matrix_access_rejects_out_of_range_indices},
        {"names_and_missing_ids_fail_atomically_and_successes_advance_once", names_and_missing_ids_fail_atomically_and_successes_advance_once},
        {"imported_mesh_creation_is_transactional_and_preserves_mesh_state", imported_mesh_creation_is_transactional_and_preserves_mesh_state},
        {"selected_mesh_edits_are_atomic_and_advance_both_revisions_once", selected_mesh_edits_are_atomic_and_advance_both_revisions_once},
        {"scene_codec_is_canonical_and_round_trips_complete_state", scene_codec_is_canonical_and_round_trips_complete_state},
        {"scene_codec_rejects_every_truncation_and_corruption", scene_codec_rejects_every_truncation_and_corruption},
        {"scene_codec_rejects_semantic_errors_limits_and_bad_nested_meshes", scene_codec_rejects_semantic_errors_limits_and_bad_nested_meshes},
        {"decoded_unusual_object_ids_use_sorted_lookup_and_round_trip_exactly", decoded_unusual_object_ids_use_sorted_lookup_and_round_trip_exactly},
        {"large_reverse_object_id_lookup_is_exact_without_timing_assumptions", large_reverse_object_id_lookup_is_exact_without_timing_assumptions},
        {"fixed_seed_scene_mutations_are_typed_and_safe", fixed_seed_scene_mutations_are_typed_and_safe},
        {"terminal_object_cursor_allows_last_allocation_then_fails_atomically", terminal_object_cursor_allows_last_allocation_then_fails_atomically},
        {"scene_install_is_atomic_and_terminal_revision_fails_mutations", scene_install_is_atomic_and_terminal_revision_fails_mutations},
    };
    const std::string filter = argc > 1 ? argv[1] : "";
    int failures = 0;
    int executed = 0;
    for (const auto& test : tests) {
        if (!filter.empty() && filter != test.name) {
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
