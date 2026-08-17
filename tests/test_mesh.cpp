#include "octopoly/mesh.hpp"

#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using octopoly::FaceId;
using octopoly::Mesh;
using octopoly::VertexId;

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
    require(actual.vertices() == expected.vertices(), message + " (vertices)");
    require(actual.faces() == expected.faces(), message + " (faces)");
    require_equal(actual.revision(), expected.revision(), message + " (revision)");
}

std::size_t edge_incidence(const Mesh& mesh, VertexId first, VertexId second) {
    std::size_t incidence = 0;
    for (const auto& face : mesh.faces()) {
        for (std::size_t index = 0; index < face.vertices.size(); ++index) {
            const VertexId a = face.vertices[index];
            const VertexId b = face.vertices[(index + 1) % face.vertices.size()];
            if ((a == first && b == second) || (a == second && b == first)) {
                ++incidence;
            }
        }
    }
    return incidence;
}

void require_closed_two_face_edge_incidence(const Mesh& mesh, const std::string& message) {
    for (const auto& face : mesh.faces()) {
        for (std::size_t index = 0; index < face.vertices.size(); ++index) {
            const VertexId first = face.vertices[index];
            const VertexId second = face.vertices[(index + 1) % face.vertices.size()];
            require_equal(edge_incidence(mesh, first, second), std::size_t{2}, message);
        }
    }
}

void default_cube_is_valid_and_stable() {
    const Mesh cube = Mesh::makeDefaultCube();
    require(cube.validate().ok, "default cube must validate");
    require_equal(cube.vertices().size(), std::size_t{8}, "cube vertex count");
    require_equal(cube.faces().size(), std::size_t{6}, "cube face count");
    for (const auto& face : cube.faces()) {
        require_equal(face.vertices.size(), std::size_t{4}, "cube faces are quads");
    }
    const VertexId firstVertex = cube.vertices().front().id;
    const FaceId firstFace = cube.faces().front().id;
    require(cube.vertex(firstVertex) != nullptr, "stable vertex ID lookup");
    require(cube.face(firstFace) != nullptr, "stable face ID lookup");
}

void triangulation_is_deterministic_and_references_mesh_vertices() {
    const Mesh cube = Mesh::makeDefaultCube();
    const auto triangles = cube.triangulate();
    require_equal(triangles.size(), std::size_t{12}, "six quads triangulate to twelve triangles");
    require_equal(triangles.front().vertices[0], cube.faces().front().vertices[0], "fan anchor is deterministic");
    for (const auto& triangle : triangles) {
        require(cube.face(triangle.sourceFace) != nullptr, "triangle source face exists");
        for (const VertexId id : triangle.vertices) {
            require(cube.vertex(id) != nullptr, "triangle vertex exists");
        }
    }
}

void loop_cut_splits_a_quad_at_opposite_edge_midpoints() {
    Mesh mesh = Mesh::makeDefaultCube();
    const FaceId target = mesh.faces().front().id;
    const auto result = mesh.loopCut(target);
    require(result.ok, result.error);
    require_equal(result.createdVertices.size(), std::size_t{2}, "loop cut creates two vertices");
    require_equal(result.affectedFaces.size(), std::size_t{4}, "loop cut changes both regions and adjacent faces");
    require_equal(mesh.vertices().size(), std::size_t{10}, "loop cut vertex count");
    require_equal(mesh.faces().size(), std::size_t{7}, "loop cut face count");
    require(mesh.face(target) != nullptr, "original face ID remains stable for one region");
    require(mesh.validate().ok, "loop-cut mesh validates");
}

void loop_cut_propagates_edge_points_to_adjacent_cube_faces() {
    Mesh mesh = Mesh::makeDefaultCube();
    const auto result = mesh.loopCut(1);
    require(result.ok, result.error);
    require_equal(result.createdVertices, std::vector<VertexId>{9, 10}, "loop cut creates stable midpoint IDs");
    require_equal(result.affectedFaces, std::vector<FaceId>{1, 5, 6, 7},
                  "loop cut reports every changed face once in deterministic order");
    require_equal(mesh.face(1)->vertices, std::vector<VertexId>{9, 4, 3, 10},
                  "selected face keeps its winding in the first region");
    require_equal(mesh.face(5)->vertices, std::vector<VertexId>{1, 5, 8, 4, 9},
                  "first adjacent face receives the shared midpoint in its winding");
    require_equal(mesh.face(6)->vertices, std::vector<VertexId>{2, 10, 3, 7, 6},
                  "second adjacent face receives the shared midpoint in its winding");
    require_equal(mesh.face(7)->vertices, std::vector<VertexId>{10, 2, 1, 9},
                  "new region has the intended loop");
    require_closed_two_face_edge_incidence(mesh, "loop-cut cube edges must retain two-face incidence");
}

void invalid_loop_cut_is_atomic_and_reports_not_found() {
    Mesh mesh = Mesh::makeDefaultCube();
    const Mesh before = mesh;
    const auto result = mesh.loopCut(9999);
    require(!result.ok, "missing face must fail");
    require_equal(result.code, octopoly::OperationError::notFound, "typed not-found error");
    require_same_mesh(mesh, before, "failed loop cut must not mutate");
}

void knife_cut_splits_between_two_nonadjacent_edges() {
    Mesh mesh = Mesh::makeDefaultCube();
    const FaceId target = mesh.faces().front().id;
    const auto result = mesh.knifeCut(target, 0, 0.25, 2, 0.75);
    require(result.ok, result.error);
    require_equal(result.createdVertices.size(), std::size_t{2}, "knife creates edge points");
    require_equal(mesh.vertices().size(), std::size_t{10}, "knife vertex count");
    require_equal(mesh.faces().size(), std::size_t{7}, "knife face count");
    require_equal(mesh.face(target)->vertices.size(), std::size_t{4}, "first region remains a quad");
    require(mesh.validate().ok, "knife-cut mesh validates");
}

void knife_cut_propagates_edge_points_to_adjacent_cube_faces() {
    Mesh mesh = Mesh::makeDefaultCube();
    const auto result = mesh.knifeCut(1, 0, 0.25, 2, 0.75);
    require(result.ok, result.error);
    require_equal(result.createdVertices, std::vector<VertexId>{9, 10}, "knife cut creates stable edge-point IDs");
    require_equal(result.affectedFaces, std::vector<FaceId>{1, 5, 6, 7},
                  "knife cut reports every changed face once in deterministic order");
    require_equal(mesh.face(1)->vertices, std::vector<VertexId>{9, 4, 3, 10},
                  "selected face keeps its winding in the first region");
    require_equal(mesh.face(5)->vertices, std::vector<VertexId>{1, 5, 8, 4, 9},
                  "first adjacent face receives the shared edge point in its winding");
    require_equal(mesh.face(6)->vertices, std::vector<VertexId>{2, 10, 3, 7, 6},
                  "second adjacent face receives the shared edge point in its winding");
    require_equal(mesh.face(7)->vertices, std::vector<VertexId>{10, 2, 1, 9},
                  "new region has the intended loop");
    require_closed_two_face_edge_incidence(mesh, "knife-cut cube edges must retain two-face incidence");
}

void invalid_knife_cut_is_atomic_including_revision() {
    Mesh mesh = Mesh::makeDefaultCube();
    const Mesh before = mesh;
    const auto result = mesh.knifeCut(mesh.faces().front().id, 0, 0.5, 1, 0.5);
    require(!result.ok, "adjacent knife edges must fail");
    require_equal(result.code, octopoly::OperationError::invalidArgument, "typed invalid-argument error");
    require_same_mesh(mesh, before, "failed knife cut must not mutate topology");
}

void inset_face_creates_an_inner_face_and_side_ring() {
    Mesh mesh = Mesh::makeDefaultCube();
    const FaceId target = mesh.faces().front().id;
    const auto result = mesh.insetFace(target, 0.25);
    require(result.ok, result.error);
    require_equal(result.createdVertices.size(), std::size_t{4}, "quad inset creates four inner vertices");
    require_equal(result.affectedFaces.size(), std::size_t{5}, "inset affects inner face and four sides");
    require_equal(mesh.vertices().size(), std::size_t{12}, "inset vertex count");
    require_equal(mesh.faces().size(), std::size_t{10}, "inset face count");
    require_equal(mesh.face(target)->vertices.size(), std::size_t{4}, "stable target ID identifies inner face");
    require(mesh.validate().ok, "inset mesh validates");
}

void extrude_face_offsets_a_new_cap_and_creates_a_side_ring() {
    Mesh mesh = Mesh::makeDefaultCube();
    const FaceId target = mesh.faces().front().id;
    const auto original = mesh.face(target)->vertices;
    const auto result = mesh.extrudeFace(target, {0.0, 0.0, -2.0});
    require(result.ok, result.error);
    require_equal(result.createdVertices.size(), std::size_t{4}, "quad extrusion creates four cap vertices");
    require_equal(result.affectedFaces.size(), std::size_t{5}, "extrusion affects cap and four sides");
    require_equal(mesh.vertices().size(), std::size_t{12}, "extrusion vertex count");
    require_equal(mesh.faces().size(), std::size_t{10}, "extrusion face count");
    const auto* cap = mesh.face(target);
    require(cap != nullptr, "stable target ID identifies extruded cap");
    require_equal(cap->vertices, result.createdVertices, "cap uses created vertices in source winding order");
    for (std::size_t index = 0; index < original.size(); ++index) {
        const auto* before = mesh.vertex(original[index]);
        const auto* after = mesh.vertex(result.createdVertices[index]);
        require(before != nullptr && after != nullptr, "extrusion vertices remain addressable");
        require_equal(after->position,
                      octopoly::Vec3{before->position.x, before->position.y, before->position.z - 2.0},
                      "cap vertex is translated by the requested offset");
    }
    require(mesh.validate().ok, "extruded mesh validates");
}

void invalid_extrude_is_atomic_including_revision() {
    struct InvalidExtrude {
        FaceId faceId;
        octopoly::Vec3 offset;
        octopoly::OperationError expected;
    };
    const Mesh reference = Mesh::makeDefaultCube();
    const FaceId existing = reference.faces().front().id;
    const std::vector<InvalidExtrude> invalidInputs{
        {9999, {0.0, 0.0, 1.0}, octopoly::OperationError::notFound},
        {existing, {0.0, 0.0, 0.0}, octopoly::OperationError::invalidArgument},
        {existing, {std::numeric_limits<double>::quiet_NaN(), 0.0, 1.0},
         octopoly::OperationError::invalidArgument},
    };
    for (const auto& input : invalidInputs) {
        Mesh mesh = Mesh::makeDefaultCube();
        const Mesh before = mesh;
        const auto result = mesh.extrudeFace(input.faceId, input.offset);
        require(!result.ok, "invalid extrusion must fail");
        require_equal(result.code, input.expected, "invalid extrusion reports a typed error");
        require_same_mesh(mesh, before, "failed extrusion must be atomic");
    }
}

void invalid_inset_is_atomic_including_revision() {
    const Mesh reference = Mesh::makeDefaultCube();
    const FaceId existing = reference.faces().front().id;
    const std::vector<std::pair<FaceId, double>> invalidInputs{
        {9999, 0.25},
        {existing, 0.0},
        {existing, 1.0},
        {existing, std::numeric_limits<double>::quiet_NaN()},
    };
    for (const auto& [faceId, factor] : invalidInputs) {
        Mesh mesh = Mesh::makeDefaultCube();
        const Mesh before = mesh;
        const auto result = mesh.insetFace(faceId, factor);
        require(!result.ok, "invalid inset must fail");
        require_same_mesh(mesh, before, "failed inset must be atomic");
    }
}

void merge_vertices_keeps_target_id_and_repairs_faces() {
    Mesh mesh = Mesh::makeDefaultCube();
    const VertexId target = mesh.vertices()[0].id;
    const VertexId source = mesh.vertices()[1].id;
    const auto result = mesh.mergeVertices(target, source);
    require(result.ok, result.error);
    require_equal(result.affectedFaces, std::vector<FaceId>{1, 3, 5, 6},
                  "merge reports all faces moved or repaired in deterministic order");
    require_equal(mesh.vertices().size(), std::size_t{7}, "merge removes source vertex");
    require(mesh.vertex(target) != nullptr, "target stable ID survives");
    require(mesh.vertex(source) == nullptr, "source ID is retired");
    require_equal(mesh.vertex(target)->position, octopoly::Vec3{0.0, -1.0, -1.0}, "target moves to midpoint");
    require(mesh.validate().ok, "merged mesh validates");
}

void invalid_merge_ids_are_atomic() {
    {
        Mesh mesh = Mesh::makeDefaultCube();
        const Mesh before = mesh;
        const VertexId same = mesh.vertices().front().id;
        const auto result = mesh.mergeVertices(same, same);
        require(!result.ok, "merging a vertex with itself must fail");
        require_equal(result.code, octopoly::OperationError::invalidArgument,
                      "same-vertex merge reports invalid argument");
        require_same_mesh(mesh, before, "same-vertex merge must be atomic");
    }
    {
        Mesh mesh = Mesh::makeDefaultCube();
        const Mesh before = mesh;
        const auto result = mesh.mergeVertices(mesh.vertices().front().id, 9999);
        require(!result.ok, "merging a missing vertex must fail");
        require_equal(result.code, octopoly::OperationError::notFound,
                      "missing-vertex merge reports not found");
        require_same_mesh(mesh, before, "missing-vertex merge must be atomic");
    }
}

void successful_operations_preserve_validation_and_advance_one_revision() {
    const auto require_success_invariants = [](Mesh mesh, const auto& operation, const std::string& name) {
        const auto beforeRevision = mesh.revision();
        require(mesh.validate().ok, name + " input must validate");
        require_equal(beforeRevision, std::uint64_t{0}, name + " starts at revision zero");
        const auto result = operation(mesh);
        require(result.ok, name + " must succeed: " + result.error);
        require_equal(result.code, octopoly::OperationError::none, name + " reports no error code");
        require(mesh.validate().ok, name + " result must validate");
        require_equal(mesh.revision(), beforeRevision + 1, name + " advances exactly one revision");
    };

    require_success_invariants(Mesh::makeDefaultCube(), [](Mesh& mesh) {
        return mesh.loopCut(mesh.faces().front().id);
    }, "loop cut");
    require_success_invariants(Mesh::makeDefaultCube(), [](Mesh& mesh) {
        return mesh.knifeCut(mesh.faces().front().id, 0, 0.25, 2, 0.75);
    }, "knife cut");
    require_success_invariants(Mesh::makeDefaultCube(), [](Mesh& mesh) {
        return mesh.insetFace(mesh.faces().front().id, 0.25);
    }, "inset");
    require_success_invariants(Mesh::makeDefaultCube(), [](Mesh& mesh) {
        return mesh.mergeVertices(mesh.vertices()[0].id, mesh.vertices()[1].id);
    }, "merge");
    require_success_invariants(Mesh::makeDefaultCube(), [](Mesh& mesh) {
        return mesh.extrudeFace(mesh.faces().front().id, {0.0, 0.0, -1.0});
    }, "extrude");
}

struct TestCase {
    std::string name;
    std::function<void()> run;
};

}  // namespace

int main(int argc, char** argv) {
    const std::vector<TestCase> tests{
        {"default_cube_is_valid_and_stable", default_cube_is_valid_and_stable},
        {"triangulation_is_deterministic_and_references_mesh_vertices", triangulation_is_deterministic_and_references_mesh_vertices},
        {"loop_cut_splits_a_quad_at_opposite_edge_midpoints", loop_cut_splits_a_quad_at_opposite_edge_midpoints},
        {"loop_cut_propagates_edge_points_to_adjacent_cube_faces", loop_cut_propagates_edge_points_to_adjacent_cube_faces},
        {"invalid_loop_cut_is_atomic_and_reports_not_found", invalid_loop_cut_is_atomic_and_reports_not_found},
        {"knife_cut_splits_between_two_nonadjacent_edges", knife_cut_splits_between_two_nonadjacent_edges},
        {"knife_cut_propagates_edge_points_to_adjacent_cube_faces", knife_cut_propagates_edge_points_to_adjacent_cube_faces},
        {"invalid_knife_cut_is_atomic_including_revision", invalid_knife_cut_is_atomic_including_revision},
        {"inset_face_creates_an_inner_face_and_side_ring", inset_face_creates_an_inner_face_and_side_ring},
        {"extrude_face_offsets_a_new_cap_and_creates_a_side_ring", extrude_face_offsets_a_new_cap_and_creates_a_side_ring},
        {"invalid_extrude_is_atomic_including_revision", invalid_extrude_is_atomic_including_revision},
        {"invalid_inset_is_atomic_including_revision", invalid_inset_is_atomic_including_revision},
        {"merge_vertices_keeps_target_id_and_repairs_faces", merge_vertices_keeps_target_id_and_repairs_faces},
        {"invalid_merge_ids_are_atomic", invalid_merge_ids_are_atomic},
        {"successful_operations_preserve_validation_and_advance_one_revision", successful_operations_preserve_validation_and_advance_one_revision},
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
