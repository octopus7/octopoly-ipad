#include "octopoly/mesh.hpp"
#include "octopoly/glb_codec.hpp"
#include "octopoly/project_codec.hpp"
#include "octopoly/scene.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

namespace allocation_fault {

bool enabled = false;
std::size_t failOrdinal = 0;
std::size_t allocationOrdinal = 0;
std::size_t failedAllocationSize = 0;

bool shouldFail() noexcept {
    if (!enabled) {
        return false;
    }
    const std::size_t current = allocationOrdinal++;
    return current == failOrdinal;
}

class Scope {
public:
    explicit Scope(std::size_t ordinal) noexcept {
        failOrdinal = ordinal;
        allocationOrdinal = 0;
        failedAllocationSize = 0;
        enabled = true;
    }

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

    ~Scope() { enabled = false; }
};

void* allocate(std::size_t size) {
    if (shouldFail()) {
        failedAllocationSize = size;
        throw std::bad_alloc{};
    }
    if (void* memory = std::malloc(std::max(size, std::size_t{1}))) {
        return memory;
    }
    throw std::bad_alloc{};
}

void* allocateAligned(std::size_t size, std::size_t alignment) {
    if (shouldFail()) {
        failedAllocationSize = size;
        throw std::bad_alloc{};
    }
#if defined(_MSC_VER)
    if (void* memory = _aligned_malloc(std::max(size, std::size_t{1}), alignment)) {
        return memory;
    }
#else
    const std::size_t requested = std::max(size, std::size_t{1});
    if (requested > std::numeric_limits<std::size_t>::max() - (alignment - 1)) {
        throw std::bad_alloc{};
    }
    const std::size_t rounded = (requested + alignment - 1) / alignment * alignment;
    if (void* memory = std::aligned_alloc(alignment, rounded)) {
        return memory;
    }
#endif
    throw std::bad_alloc{};
}

void deallocateAligned(void* memory) noexcept {
#if defined(_MSC_VER)
    _aligned_free(memory);
#else
    std::free(memory);
#endif
}

}  // namespace allocation_fault

void* operator new(std::size_t size) {
    return allocation_fault::allocate(size);
}

void* operator new[](std::size_t size) {
    return allocation_fault::allocate(size);
}

void operator delete(void* memory) noexcept {
    std::free(memory);
}

void operator delete[](void* memory) noexcept {
    std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept {
    std::free(memory);
}

void operator delete[](void* memory, std::size_t) noexcept {
    std::free(memory);
}

void* operator new(std::size_t size, std::align_val_t alignment) {
    return allocation_fault::allocateAligned(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment) {
    return allocation_fault::allocateAligned(size, static_cast<std::size_t>(alignment));
}

void operator delete(void* memory, std::align_val_t) noexcept {
    allocation_fault::deallocateAligned(memory);
}

void operator delete[](void* memory, std::align_val_t) noexcept {
    allocation_fault::deallocateAligned(memory);
}

void operator delete(void* memory, std::size_t, std::align_val_t) noexcept {
    allocation_fault::deallocateAligned(memory);
}

void operator delete[](void* memory, std::size_t, std::align_val_t) noexcept {
    allocation_fault::deallocateAligned(memory);
}

namespace {

using octopoly::Mesh;
using octopoly::OperationResult;
using octopoly::Primitive;
using octopoly::Scene;
using octopoly::SceneResult;
using octopoly::project::encodeProject;
using octopoly::project::encodeSceneProject;
using octopoly::project::installProject;
using octopoly::project::installSceneProject;

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_same_mesh(const Mesh& actual, const Mesh& expected, const std::string& label) {
    require(actual.vertices() == expected.vertices(), label + " (vertices)");
    require(actual.faces() == expected.faces(), label + " (faces)");
    require(actual.nextVertexId() == expected.nextVertexId(), label + " (next vertex ID)");
    require(actual.nextFaceId() == expected.nextFaceId(), label + " (next face ID)");
    require(actual.revision() == expected.revision(), label + " (revision)");
}

OperationResult loopCut(Mesh& mesh) {
    return mesh.loopCut(mesh.faces().front().id);
}

OperationResult knifeCut(Mesh& mesh) {
    return mesh.knifeCut(mesh.faces().front().id, 0, 0.25, 2, 0.75);
}

OperationResult insetFace(Mesh& mesh) {
    return mesh.insetFace(mesh.faces().front().id, 0.25);
}

OperationResult extrudeFace(Mesh& mesh) {
    return mesh.extrudeFace(mesh.faces().front().id, {0.0, 0.0, -1.0});
}

OperationResult mergeVertices(Mesh& mesh) {
    return mesh.mergeVertices(mesh.vertices()[0].id, mesh.vertices()[1].id);
}

using Operation = OperationResult (*)(Mesh&);

void require_allocation_failures_are_atomic(std::string_view label, Operation operation) {
    constexpr std::size_t maximumOrdinal = 1'024;
    std::size_t observedFailures = 0;

    for (std::size_t ordinal = 0; ordinal < maximumOrdinal; ++ordinal) {
        Mesh live = Mesh::makeDefaultCube();
        const Mesh before = live;
        const auto beforeEncoding = encodeProject(live);
        require(beforeEncoding.ok, std::string(label) + " baseline must encode");

        bool badAllocationEscaped = false;
        bool operationSucceeded = false;
        {
            allocation_fault::Scope fault(ordinal);
            try {
                const OperationResult result = operation(live);
                operationSucceeded = result.ok;
            } catch (const std::bad_alloc&) {
                badAllocationEscaped = true;
            }
        }

        if (badAllocationEscaped) {
            ++observedFailures;
            require_same_mesh(live, before,
                              std::string(label) + " escaped bad_alloc must preserve complete state");
            const auto afterEncoding = encodeProject(live);
            require(afterEncoding.ok,
                    std::string(label) + " must remain encodable after escaped bad_alloc");
            require(afterEncoding.bytes == beforeEncoding.bytes,
                    std::string(label) + " escaped bad_alloc must preserve exact encoded bytes");
            continue;
        }

        require(operationSucceeded,
                std::string(label) + " non-throwing ordinal must complete successfully");
        require(observedFailures > 0,
                std::string(label) + " must observe at least one injected allocation failure");
        require(live.validate().ok,
                std::string(label) + " eventual successful operation must validate");
        return;
    }

    throw std::runtime_error(std::string(label) +
                             " did not reach a successful allocation ordinal");
}

void decoded_index_allocation_failures_are_typed_and_install_is_atomic() {
    constexpr std::size_t maximumOrdinal = 1'024;
    const Mesh replacement = Mesh::makeDefaultCube();
    const auto replacementEncoding = encodeProject(replacement);
    require(replacementEncoding.ok, "replacement project must encode");
    std::size_t observedAllocationFailures = 0;
    std::size_t observedIndexSizedFailures = 0;

    for (std::size_t ordinal = 0; ordinal < maximumOrdinal; ++ordinal) {
        Mesh live = Mesh::makeDefaultCube();
        require(live.extrudeFace(live.faces().front().id, {0.0, 0.0, -0.5}).ok,
                "live install fixture edit must succeed");
        const Mesh before = live;
        const auto beforeEncoding = encodeProject(live);
        require(beforeEncoding.ok, "live install fixture must encode");

        octopoly::project::InstallResult result;
        std::size_t failedSize = 0;
        {
            allocation_fault::Scope fault(ordinal);
            result = installProject(live, replacementEncoding.bytes);
            failedSize = allocation_fault::failedAllocationSize;
        }

        if (!result.ok) {
            require(result.error.code == octopoly::project::DecodeErrorCode::allocationFailed,
                    "every injected decode allocation must return the typed allocation error");
            ++observedAllocationFailures;
            if (failedSize == replacement.vertices().size() *
                                  sizeof(std::pair<octopoly::VertexId, std::size_t>)) {
                ++observedIndexSizedFailures;
            }
            require_same_mesh(live, before,
                              "typed decode allocation failure must preserve complete live state");
            const auto afterEncoding = encodeProject(live);
            require(afterEncoding.ok,
                    "live mesh must remain encodable after typed decode allocation failure");
            require(afterEncoding.bytes == beforeEncoding.bytes,
                    "typed decode allocation failure must preserve canonical live bytes");
            continue;
        }

        require(observedAllocationFailures > 0,
                "install must observe at least one typed allocation failure");
        require(observedIndexSizedFailures >= 2,
                "fault sweep must reach parsed-ID and derived-index-sized allocations");
        require_same_mesh(live, replacement,
                          "first non-failing ordinal must atomically install complete replacement");
        require(live.validate().ok, "successfully installed derived index must validate");
        return;
    }

    throw std::runtime_error("project install did not reach a successful allocation ordinal");
}

void copy_assignment_allocation_failures_are_atomic() {
    constexpr std::size_t maximumOrdinal = 1'024;
    Mesh source = Mesh::makeDefaultCube();
    require(source.extrudeFace(source.faces().front().id, {0.0, 0.0, -0.75}).ok,
            "copy-assignment source edit must succeed");
    std::size_t observedFailures = 0;

    for (std::size_t ordinal = 0; ordinal < maximumOrdinal; ++ordinal) {
        Mesh destination = Mesh::makeDefaultCube();
        require(destination.loopCut(destination.faces().front().id).ok,
                "copy-assignment destination edit must succeed");
        const Mesh before = destination;
        const auto beforeEncoding = encodeProject(destination);
        require(beforeEncoding.ok, "copy-assignment destination baseline must encode");

        bool badAllocationEscaped = false;
        {
            allocation_fault::Scope fault(ordinal);
            try {
                destination = source;
            } catch (const std::bad_alloc&) {
                badAllocationEscaped = true;
            }
        }

        if (badAllocationEscaped) {
            ++observedFailures;
            require_same_mesh(destination, before,
                              "copy-assignment bad_alloc must preserve destination state");
            require(destination.validate().ok,
                    "copy-assignment bad_alloc must preserve a valid lookup index");
            const auto afterEncoding = encodeProject(destination);
            require(afterEncoding.ok,
                    "copy-assignment destination must remain encodable after bad_alloc");
            require(afterEncoding.bytes == beforeEncoding.bytes,
                    "copy-assignment bad_alloc must preserve exact destination bytes");
            continue;
        }

        require(observedFailures > 0,
                "copy assignment must observe at least one injected allocation failure");
        require_same_mesh(destination, source,
                          "first non-failing copy assignment must replace complete state");
        require(destination.validate().ok,
                "successful copy assignment must preserve the derived lookup index");
        for (const auto& vertex : source.vertices()) {
            require(destination.vertex(vertex.id) != nullptr,
                    "successful copy assignment must resolve every stable vertex ID");
        }
        return;
    }

    throw std::runtime_error("copy assignment did not reach a successful allocation ordinal");
}

void glb_decode_allocation_failures_are_typed_and_install_is_atomic() {
    constexpr std::size_t maximumOrdinal = 4'096;
    const Mesh replacement = Mesh::makeDefaultCube();
    const auto encoded = octopoly::glb::encodeGlb(replacement);
    require(encoded.ok, "GLB replacement fixture must encode");
    const auto decodedReplacement = octopoly::glb::decodeGlb(encoded.bytes);
    require(decodedReplacement.ok, "GLB replacement fixture must decode");
    const Mesh expected = decodedReplacement.mesh;
    std::size_t observedFailures = 0;

    for (std::size_t ordinal = 0; ordinal < maximumOrdinal; ++ordinal) {
        Mesh live = Mesh::makeDefaultCube();
        require(live.extrudeFace(live.faces().front().id, {0.0, 0.0, -0.25}).ok,
                "GLB live fixture edit must succeed");
        const Mesh before = live;
        const auto beforeEncoding = encodeProject(live);
        require(beforeEncoding.ok, "GLB live fixture must encode canonically");

        octopoly::glb::InstallResult result;
        {
            allocation_fault::Scope fault(ordinal);
            result = octopoly::glb::installGlb(live, encoded.bytes);
        }

        if (!result.ok) {
            require(result.error.code == octopoly::glb::DecodeErrorCode::allocationFailed,
                    "every injected GLB allocation failure must be typed");
            ++observedFailures;
            require_same_mesh(live, before,
                              "typed GLB allocation failure must preserve complete live state");
            const auto afterEncoding = encodeProject(live);
            require(afterEncoding.ok,
                    "GLB live mesh must remain encodable after allocation failure");
            require(afterEncoding.bytes == beforeEncoding.bytes,
                    "typed GLB allocation failure must preserve canonical live bytes");
            continue;
        }

        require(observedFailures > 0,
                "GLB install must observe at least one typed allocation failure");
        require_same_mesh(live, expected,
                          "first non-failing GLB install must atomically install triangulated mesh");
        require(live.validate().ok, "successful GLB install must validate");
        return;
    }

    throw std::runtime_error("GLB install did not reach a successful allocation ordinal");
}

Scene make_scene_fixture() {
    Scene scene;
    require(scene.createPrimitive(Primitive::cube, "Cube").ok,
            "scene fixture cube creation");
    require(scene.createPrimitive(Primitive::uvSphere, "Sphere", {8, 4}).ok,
            "scene fixture sphere creation");
    require(scene.selectObject(1).ok, "scene fixture selection");
    return scene;
}

SceneResult sceneCreate(Scene& scene) {
    return scene.createPrimitive(Primitive::cylinder, "Cylinder", {12, 4});
}

SceneResult sceneCreateMesh(Scene& scene) {
    Mesh imported = Mesh::makeDefaultCube();
    require(imported.extrudeFace(imported.faces().front().id, {0.0, 0.0, -0.25}).ok,
            "scene imported-mesh allocation fixture edit");
    return scene.createMeshObject(std::move(imported), "Imported GLB");
}

SceneResult sceneDelete(Scene& scene) { return scene.deleteObject(1); }
SceneResult sceneRename(Scene& scene) { return scene.renameObject(1, "Renamed"); }
SceneResult sceneTransform(Scene& scene) {
    return scene.setLocalTransform(1, {{2.0, 3.0, 4.0}, {}, {-1.0, 2.0, 3.0}});
}

using SceneOperation = SceneResult (*)(Scene&);

template <typename Operation>
void require_scene_allocation_failures_are_atomic(std::string_view label,
                                                  Operation operation) {
    constexpr std::size_t maximumOrdinal = 4'096;
    std::size_t observedFailures = 0;
    for (std::size_t ordinal = 0; ordinal < maximumOrdinal; ++ordinal) {
        Scene live = make_scene_fixture();
        const auto before = encodeSceneProject(live);
        require(before.ok, std::string(label) + " baseline scene encodes");
        bool escaped = false;
        bool succeeded = false;
        {
            allocation_fault::Scope fault(ordinal);
            try {
                succeeded = operation(live).ok;
            } catch (const std::bad_alloc&) {
                escaped = true;
            }
        }
        if (escaped) {
            ++observedFailures;
            const auto after = encodeSceneProject(live);
            require(after.ok && after.bytes == before.bytes,
                    std::string(label) +
                        " escaped bad_alloc preserves exact scene bytes");
            require(live.validate().ok,
                    std::string(label) + " escaped bad_alloc preserves validation");
            continue;
        }
        require(succeeded, std::string(label) + " eventual ordinal succeeds");
        require(observedFailures > 0,
                std::string(label) + " observes injected allocation failures");
        require(live.validate().ok, std::string(label) + " successful scene validates");
        return;
    }
    throw std::runtime_error(std::string(label) +
                             " did not reach a successful allocation ordinal");
}

OperationResult sceneSelectedLoopCut(Scene& scene) {
    return scene.selectedLoopCut(scene.selectedObject()->mesh().faces().front().id);
}

void scene_copy_assignment_allocation_failures_are_atomic() {
    constexpr std::size_t maximumOrdinal = 4'096;
    const Scene source = make_scene_fixture();
    std::size_t observedFailures = 0;
    for (std::size_t ordinal = 0; ordinal < maximumOrdinal; ++ordinal) {
        Scene destination;
        require(destination.createPrimitive(Primitive::cone, "Destination", {7, 4}).ok,
                "scene copy destination fixture");
        const auto before = encodeSceneProject(destination);
        require(before.ok, "scene copy destination encodes");
        bool escaped = false;
        {
            allocation_fault::Scope fault(ordinal);
            try {
                destination = source;
            } catch (const std::bad_alloc&) {
                escaped = true;
            }
        }
        if (escaped) {
            ++observedFailures;
            const auto after = encodeSceneProject(destination);
            require(after.ok && after.bytes == before.bytes,
                    "scene copy bad_alloc preserves exact destination bytes");
            continue;
        }
        require(observedFailures > 0, "scene copy observes allocation failures");
        const auto sourceBytes = encodeSceneProject(source);
        const auto destinationBytes = encodeSceneProject(destination);
        require(sourceBytes.ok && destinationBytes.ok &&
                    sourceBytes.bytes == destinationBytes.bytes,
                "successful scene copy replaces complete destination");
        return;
    }
    throw std::runtime_error("scene copy did not reach a successful ordinal");
}

void scene_install_allocation_failures_are_typed_and_atomic() {
    constexpr std::size_t maximumOrdinal = 8'192;
    const Scene replacement = make_scene_fixture();
    const auto replacementBytes = encodeSceneProject(replacement);
    require(replacementBytes.ok, "scene install replacement encodes");
    std::size_t observedFailures = 0;
    for (std::size_t ordinal = 0; ordinal < maximumOrdinal; ++ordinal) {
        Scene live;
        require(live.createPrimitive(Primitive::plane, "Live").ok,
                "scene install live fixture");
        const auto before = encodeSceneProject(live);
        require(before.ok, "scene install live fixture encodes");
        octopoly::project::SceneInstallResult result;
        {
            allocation_fault::Scope fault(ordinal);
            result = installSceneProject(live, replacementBytes.bytes);
        }
        if (!result.ok) {
            ++observedFailures;
            require(result.error.code ==
                        octopoly::project::SceneDecodeErrorCode::allocationFailed,
                    "injected scene install failure is typed allocation failure");
            const auto after = encodeSceneProject(live);
            require(after.ok && after.bytes == before.bytes,
                    "typed scene install failure preserves exact live bytes");
            continue;
        }
        require(observedFailures > 0, "scene install observes allocation failures");
        const auto after = encodeSceneProject(live);
        require(after.ok && after.bytes == replacementBytes.bytes,
                "successful scene install commits complete candidate");
        return;
    }
    throw std::runtime_error("scene install did not reach a successful ordinal");
}

}  // namespace

int main() {
    const std::array<std::pair<std::string_view, Operation>, 5> operations{{
        {"loop cut", loopCut},
        {"knife cut", knifeCut},
        {"inset", insetFace},
        {"extrude", extrudeFace},
        {"merge", mergeVertices},
    }};

    int failures = 0;
    for (const auto& [label, operation] : operations) {
        try {
            require_allocation_failures_are_atomic(label, operation);
            std::cout << "PASS " << label << " allocation failures are atomic\n";
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL " << label << " allocation failures are atomic: "
                      << error.what() << '\n';
        }
    }
    try {
        decoded_index_allocation_failures_are_typed_and_install_is_atomic();
        std::cout << "PASS decoded index allocation failures are typed and install is atomic\n";
    } catch (const std::exception& error) {
        ++failures;
        std::cerr << "FAIL decoded index allocation failures are typed and install is atomic: "
                  << error.what() << '\n';
    }
    try {
        copy_assignment_allocation_failures_are_atomic();
        std::cout << "PASS copy assignment allocation failures are atomic\n";
    } catch (const std::exception& error) {
        ++failures;
        std::cerr << "FAIL copy assignment allocation failures are atomic: "
                  << error.what() << '\n';
    }
    try {
        glb_decode_allocation_failures_are_typed_and_install_is_atomic();
        std::cout << "PASS GLB allocation failures are typed and install is atomic\n";
    } catch (const std::exception& error) {
        ++failures;
        std::cerr << "FAIL GLB allocation failures are typed and install is atomic: "
                  << error.what() << '\n';
    }
    const std::array<std::pair<std::string_view, SceneOperation>, 5> sceneOperations{{
        {"scene create", sceneCreate},
        {"scene create imported mesh", sceneCreateMesh},
        {"scene delete", sceneDelete},
        {"scene rename", sceneRename},
        {"scene transform", sceneTransform},
    }};
    for (const auto& [label, operation] : sceneOperations) {
        try {
            require_scene_allocation_failures_are_atomic(label, operation);
            std::cout << "PASS " << label << " allocation failures are atomic\n";
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL " << label << " allocation failures are atomic: "
                      << error.what() << '\n';
        }
    }
    try {
        require_scene_allocation_failures_are_atomic("scene selected loop cut",
                                                     sceneSelectedLoopCut);
        std::cout << "PASS scene selected loop cut allocation failures are atomic\n";
    } catch (const std::exception& error) {
        ++failures;
        std::cerr << "FAIL scene selected loop cut allocation failures are atomic: "
                  << error.what() << '\n';
    }
    try {
        scene_copy_assignment_allocation_failures_are_atomic();
        std::cout << "PASS scene copy assignment allocation failures are atomic\n";
    } catch (const std::exception& error) {
        ++failures;
        std::cerr << "FAIL scene copy assignment allocation failures are atomic: "
                  << error.what() << '\n';
    }
    try {
        scene_install_allocation_failures_are_typed_and_atomic();
        std::cout << "PASS scene allocation failures are typed and install is atomic\n";
    } catch (const std::exception& error) {
        ++failures;
        std::cerr << "FAIL scene allocation failures are typed and install is atomic: "
                  << error.what() << '\n';
    }
    std::cout << operations.size() + sceneOperations.size() + 6 << " test(s), "
              << failures << " failure(s)\n";
    return failures == 0 ? 0 : 1;
}
