#!/usr/bin/env python3
"""Deterministic Phase 1-4 portable/static Apple integration validator.

This is deliberately not an Xcode parser or Apple compiler. It fails closed on
missing PBX/source/action-chain invariants while keeping the verification tier
explicitly limited to portable execution plus static project inspection.
"""

from __future__ import annotations

import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys
from typing import NoReturn

ROOT = Path(__file__).resolve().parents[1]


def fail(message: str) -> NoReturn:
    print(f"[static] FAIL {message}", file=sys.stderr)
    raise SystemExit(1)


def read(relative: str) -> str:
    path = ROOT / relative
    if not path.is_file():
        fail(f"missing required file: {relative}")
    return path.read_text(encoding="utf-8")


def require_markers(source: str, markers: list[str], label: str) -> None:
    for marker in markers:
        if marker not in source:
            fail(f"{label} missing: {marker}")


def git(*arguments: str, check: bool = True) -> str:
    completed = subprocess.run(
        ["git", *arguments], cwd=ROOT, check=False,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
    )
    if check and completed.returncode != 0:
        fail(f"git {' '.join(arguments)} failed: {completed.stderr.strip()}")
    return completed.stdout.strip()


def normalized_phase4_candidate_data(relative: str, data: bytes) -> bytes:
    if relative == "docs/verification/phase-4.md":
        return re.sub(
            rb"sha256:[0-9a-f]{64}", b"sha256:<normalized-candidate>", data
        )
    return data


def update_candidate_digest(
    digest: object, relative: str, mode: bytes, data: bytes
) -> None:
    data = normalized_phase4_candidate_data(relative, data)
    encoded_path = relative.encode("utf-8")
    digest.update(len(encoded_path).to_bytes(8, "little"))  # type: ignore[attr-defined]
    digest.update(encoded_path)  # type: ignore[attr-defined]
    digest.update(mode)  # type: ignore[attr-defined]
    digest.update(len(data).to_bytes(8, "little"))  # type: ignore[attr-defined]
    digest.update(data)  # type: ignore[attr-defined]


def phase4_candidate_fingerprint(paths: list[str]) -> str:
    digest = hashlib.sha256(b"octopoly-phase4-candidate-v1\0")
    for relative in paths:
        path = ROOT / relative
        if not path.is_file():
            fail(f"Phase 4 candidate manifest path is missing: {relative}")
        mode = b"0755" if os.stat(path).st_mode & 0o111 else b"0644"
        update_candidate_digest(digest, relative, mode, path.read_bytes())
    return digest.hexdigest()


def phase4_candidate_fingerprint_at_commit(commit: str, paths: list[str]) -> str:
    digest = hashlib.sha256(b"octopoly-phase4-candidate-v1\0")
    for relative in paths:
        content = subprocess.run(
            ["git", "show", f"{commit}:{relative}"], cwd=ROOT, check=False,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
        if content.returncode != 0:
            fail(f"Phase 4 candidate path is absent from {commit}: {relative}")
        tree_entry = git("ls-tree", commit, "--", relative)
        if not tree_entry:
            fail(f"Phase 4 candidate mode is absent from {commit}: {relative}")
        mode_token = tree_entry.split(maxsplit=1)[0]
        mode = format(int(mode_token, 8) & 0o777, "04o").encode("ascii")
        update_candidate_digest(digest, relative, mode, content.stdout)
    return digest.hexdigest()


def block_from(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        fail(f"missing method/function signature: {signature}")
    opening = source.find("{", start)
    if opening < 0:
        fail(f"missing method/function body: {signature}")
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    fail(f"unterminated method/function body: {signature}")
    raise AssertionError("unreachable")


required_files = [
    "LICENSE", "README.md", "ROADMAP.md", "CMakeLists.txt",
    "docs/FORMAT.md", "docs/GLB.md", "docs/SCENE.md",
    "docs/verification/phase-1.md", "docs/verification/phase-2.md",
    "docs/verification/phase-3.md", "docs/verification/phase-4-core.md",
    "docs/verification/phase-4.md",
    "core/include/octopoly/mesh.hpp", "core/include/octopoly/project_codec.hpp",
    "core/include/octopoly/glb_codec.hpp", "core/include/octopoly/scene.hpp",
    "core/src/mesh.cpp", "core/src/project_codec.cpp", "core/src/glb_codec.cpp",
    "core/src/scene.cpp", "tests/test_mesh.cpp", "tests/test_project_codec.cpp",
    "tests/test_mesh_allocation_faults.cpp", "tests/test_glb_codec.cpp",
    "tests/test_scene.cpp", "scripts/check.sh", "scripts/mac/remote-build.sh",
    "scripts/mac/install-device.sh", "ci/github-actions/linux.yml",
    "ci/github-actions/macos.yml",
    "app/OctoPolyIPad/Sources/OctoPolyIPadApp.swift",
    "app/OctoPolyIPad/Sources/ContentView.swift",
    "app/OctoPolyIPad/Sources/MeshViewModel.swift",
    "app/OctoPolyIPad/Sources/MetalViewport.swift",
    "app/OctoPolyIPad/Sources/MeshRenderer.swift",
    "app/OctoPolyIPad/Sources/Shaders.metal",
    "app/OctoPolyIPad/Sources/MeshBridge.h",
    "app/OctoPolyIPad/Sources/MeshBridge.mm",
    "app/OctoPolyIPad/Sources/OctoPolyIPad-Bridging-Header.h",
    "app/OctoPolyIPad/OctoPolyIPad.xcodeproj/project.pbxproj",
    "app/OctoPolyIPad/OctoPolyIPad.xcodeproj/xcshareddata/xcschemes/OctoPolyIPad.xcscheme",
]
for relative in required_files:
    if not (ROOT / relative).is_file():
        fail(f"missing required file: {relative}")
print(f"[static] OK required Phase 1-4 files ({len(required_files)})")

license_text = read("LICENSE")
if not license_text.startswith("MIT License\n") or "Copyright (c) 2026 octopus7" not in license_text:
    fail("LICENSE must remain MIT with Copyright (c) 2026 octopus7")
roadmap = read("ROADMAP.md")
for phase, title in {
    1: "Portable mesh core and iPad shell",
    2: "Native project save and load",
    3: "GLB import and export",
    4: "Primitives, scene outliner, and world transforms",
}.items():
    if f"## Phase {phase} — {title} (complete)" not in roadmap:
        fail(f"ROADMAP must mark Phase {phase} complete")
for phase in range(5, 11):
    if not re.search(rf"^## Phase {phase} .* \(planned\)$", roadmap, re.MULTILINE):
        fail(f"ROADMAP must keep Phase {phase} planned")
print("[static] OK roadmap marks only Phases 1-4 complete")

cmake = read("CMakeLists.txt")
require_markers(cmake, [
    "core/src/mesh.cpp", "core/src/scene.cpp", "core/src/project_codec.cpp",
    "core/src/glb_codec.cpp", "add_executable(octopoly_core_tests",
    "add_executable(project_codec_tests", "add_executable(mesh_allocation_fault_tests",
    "add_executable(glb_codec_tests", "add_executable(scene_tests",
    "add_test(NAME octopoly_core_tests", "add_test(NAME project_codec_tests",
    "add_test(NAME mesh_allocation_fault_tests", "add_test(NAME glb_codec_tests",
    "add_test(NAME scene_tests",
], "CMake retained suites")
check_script = read("scripts/check.sh")
for executable in ["octopoly_core_tests", "project_codec_tests", "mesh_allocation_fault_tests",
                   "glb_codec_tests", "scene_tests"]:
    require_markers(check_script, [f"./build/check/{executable}"], "check.sh suite execution")
if check_script.count("-std=c++20 -Wall -Wextra -Wpedantic -Werror") != 5:
    fail("check.sh must warning-clean compile exactly the five retained suites")
require_markers(check_script, ["python3 scripts/validate_project.py", "bash -n scripts/check.sh"],
                "check.sh final gates")
print("[static] OK CMake/check retain all five warning-clean suites")

pbx = read("app/OctoPolyIPad/OctoPolyIPad.xcodeproj/project.pbxproj")
for opening, closing, label in [("{", "}", "braces"), ("(", ")", "parentheses")]:
    if pbx.count(opening) != pbx.count(closing):
        fail(f"pbxproj has unbalanced {label}")
declared_ids = re.findall(r"^\t\t([0-9A-F]{24})(?: /\*.*?\*/)? = \{", pbx, re.MULTILINE)
if len(declared_ids) != len(set(declared_ids)):
    fail("pbxproj contains duplicate object declarations")
undefined_ids = sorted(set(re.findall(r"\b[0-9A-F]{24}\b", pbx)) - set(declared_ids))
if undefined_ids:
    fail(f"pbxproj references undefined IDs: {', '.join(undefined_ids)}")
require_markers(pbx, [
    "000000000000000000000112 /* scene.cpp */ = {isa = PBXFileReference; lastKnownFileType = sourcecode.cpp.cpp; path = src/scene.cpp;",
    "000000000000000000000113 /* scene.hpp */ = {isa = PBXFileReference; lastKnownFileType = sourcecode.cpp.h; path = include/octopoly/scene.hpp;",
    "00000000000000000000020D /* scene.cpp in Sources */ = {isa = PBXBuildFile; fileRef = 000000000000000000000112",
    "000000000000000000000112 /* scene.cpp */,",
    "000000000000000000000113 /* scene.hpp */,",
    "00000000000000000000020D /* scene.cpp in Sources */,",
    "project_codec.cpp in Sources", "glb_codec.cpp in Sources", "mesh.cpp in Sources",
    'SWIFT_OBJC_BRIDGING_HEADER = "Sources/OctoPolyIPad-Bridging-Header.h";',
    'HEADER_SEARCH_PATHS = "$(PROJECT_DIR)/../../core/include";',
    'CLANG_CXX_LANGUAGE_STANDARD = "c++20";', "TARGETED_DEVICE_FAMILY = 2;",
], "pbxproj Phase 4 wiring")
file_refs = pbx.split("/* Begin PBXFileReference section */", 1)[1].split(
    "/* End PBXFileReference section */", 1)[0]
for filename in ["mesh.cpp", "mesh.hpp", "project_codec.cpp", "project_codec.hpp",
                 "glb_codec.cpp", "glb_codec.hpp", "scene.cpp", "scene.hpp"]:
    if file_refs.count(f"/* {filename} */") != 1:
        fail(f"pbxproj must have exactly one file reference for {filename}")
project_directory = ROOT / "app/OctoPolyIPad"
for relative in ["../../core/src/scene.cpp", "../../core/include/octopoly/scene.hpp"]:
    if not (project_directory / relative).resolve().is_file():
        fail(f"pbxproj portable path does not resolve: {relative}")
print("[static] OK PBX IDs, Portable Core scene refs, and scene.cpp Sources membership")

app = read("app/OctoPolyIPad/Sources/OctoPolyIPadApp.swift")
content = read("app/OctoPolyIPad/Sources/ContentView.swift")
model = read("app/OctoPolyIPad/Sources/MeshViewModel.swift")
header = read("app/OctoPolyIPad/Sources/MeshBridge.h")
bridge = read("app/OctoPolyIPad/Sources/MeshBridge.mm")
scene_header = read("core/include/octopoly/scene.hpp")
scene_source = read("core/src/scene.cpp")
scene_tests = read("tests/test_scene.cpp")
fault_tests = read("tests/test_mesh_allocation_faults.cpp")
project_header = read("core/include/octopoly/project_codec.hpp")
project_source = read("core/src/project_codec.cpp")
glb_header = read("core/include/octopoly/glb_codec.hpp")
glb_source = read("core/src/glb_codec.cpp")
renderer = read("app/OctoPolyIPad/Sources/MeshRenderer.swift")

require_markers(app, ["@main", "WindowGroup", "ContentView()"], "App -> ContentView chain")
require_markers(content, ["@StateObject private var model = MeshViewModel()", "MetalViewport(model: model)"],
                "ContentView -> model/viewport chain")
require_markers(renderer, ["drawPrimitives(type: .triangle"], "Metal triangle draw")
print("[static] OK App -> ContentView -> MeshViewModel -> Metal viewport chain")

if "_meshStorage" in header or "_meshStorage" in bridge:
    fail("bridge must not retain raw Mesh storage")
require_markers(header, [
    "void *_sceneStorage;", "NSData *_cachedTriangleVertexData;",
    "NSArray<SceneOutlinerItem *> *_cachedOutlinerItems;",
    "- (nullable instancetype)init NS_DESIGNATED_INITIALIZER;",
    "NSArray<SceneOutlinerItem *> *outlinerItems", "uint64_t selectedObjectId",
    "uint64_t sceneRevision",
], "Scene-backed coherent snapshot declaration")
require_markers(bridge, ["static octopoly::Scene& sceneFromStorage", "new octopoly::Scene()",
                         "createPrimitive(octopoly::Primitive::cube, \"Cube\")"],
                "Scene-backed bridge initialization")

snapshot_body = block_from(bridge, "static BOOL prepareSceneSnapshot(")
require_markers(snapshot_body, [
    "const octopoly::Scene& scene", "for (const octopoly::SceneObject& object : scene.objects())",
    "mesh.visitTriangles(", "checkedAddSize(triangleCount, 1",
    "checkedMultiplySize(triangleCount, 3", "checkedMultiplySize(renderVertexCount, sizeof(RenderVertex)",
    "std::numeric_limits<NSUInteger>::max()", "pointRepresentable(world.transformPoint(vertex->position))",
    "[NSMutableData dataWithLength:", "world.transformPoint(mesh.vertex(vertexId)->position)",
    "NSMutableArray<SceneOutlinerItem *>", "catch (const std::bad_alloc&)", "catch (...)"
], "detached complete scene snapshot preparation")
if snapshot_body.count("for (const octopoly::SceneObject& object : scene.objects())") < 3:
    fail("scene snapshot must perform geometry preflight, geometry packing, and outliner passes")
if "sceneFromStorage" in snapshot_body or "_cached" in snapshot_body:
    fail("detached snapshot preparation must not read or mutate live bridge storage/caches")

commit_body = block_from(bridge, "static void commitPreparedScene(")
require_markers(commit_body, [
    "sceneFromStorage(storage) = std::move(candidate);",
    "*cachedTriangleVertexData = preparedTriangleVertexData;",
    "*cachedOutlinerItems = preparedOutlinerItems;",
], "no-throw coherent Scene/cache commit")
prepare_commit_body = block_from(bridge, "static BOOL prepareAndCommitScene(")
require_markers(prepare_commit_body, ["prepareSceneSnapshot(candidate", "commitPreparedScene("],
                "prepare-before-commit helper")
if prepare_commit_body.find("prepareSceneSnapshot(candidate") > prepare_commit_body.find("commitPreparedScene("):
    fail("candidate snapshot preparation must precede Scene/cache commit")

mutation_body = block_from(bridge, "static BOOL mutateSceneCandidate(")
require_markers(mutation_body, [
    "if (storage == nullptr)", "octopoly::Scene candidate = sceneFromStorage(storage);",
    "mutator(candidate)", "prepareAndCommitScene(storage",
], "detached candidate mutation helper")
if mutation_body.find("mutator(candidate)") > mutation_body.find("prepareAndCommitScene(storage"):
    fail("candidate mutation must precede snapshot preparation and commit")

init_body = block_from(bridge, "- (instancetype)init {")
require_markers(init_body, [
    "_cachedTriangleVertexData = nil;", "_cachedOutlinerItems = nil;",
    "prepareSceneSnapshot(*scene", "_sceneStorage = scene.release();", "return nil;",
], "initial Cube coherent snapshot or nil init")
if init_body.find("prepareSceneSnapshot(*scene") > init_body.find("_sceneStorage = scene.release();"):
    fail("initial Cube snapshot must be prepared before Scene ownership is published")
if "new (std::nothrow)" in init_body:
    fail("failed initial Cube/snapshot allocation must return nil, not install an empty fallback Scene")
require_markers(model, [
    "private let bridge: MeshBridge?", "bridge = MeshBridge()",
    "guard bridge != nil else", "guard let bridge else",
    "scene bridge is unavailable",
], "nullable native bridge initialization and Swift failure handling")

triangle_getter = block_from(bridge, "- (NSData *)triangleVertexData {")
outliner_getter = block_from(bridge, "- (NSArray<SceneOutlinerItem *> *)outlinerItems {")
require_markers(triangle_getter, ["return _cachedTriangleVertexData;"],
                "non-fallible cached triangle getter")
require_markers(outliner_getter, ["return _cachedOutlinerItems;"],
                "non-fallible cached outliner getter")
for getter, label in [(triangle_getter, "triangle"), (outliner_getter, "outliner")]:
    if "sceneFromStorage" in getter or "NSMutable" in getter or "_lastError" in getter:
        fail(f"cached {label} getter must not rerun fallible preparation or mutate error state")

transform_helper = block_from(bridge, "static BOOL mutateSelectedTransform(")
require_markers(transform_helper, ["if (storage == nullptr)", "mutateSceneCandidate("],
                "null-safe detached transform helper")
print("[static] OK bridge owns one committed Scene plus coherent cached initial/updated snapshots")

require_markers(scene_header, [
    "SceneResult createMeshObject(Mesh mesh, std::string name);",
    "OperationResult selectedLoopCut", "OperationResult selectedKnifeCut",
    "OperationResult selectedInsetFace", "OperationResult selectedMergeVertices",
    "OperationResult selectedExtrudeFace", "enum class MeshEditKind",
], "portable Scene integration API")
require_markers(scene_source, [
    "SceneResult Scene::createMeshObject", "const ValidationResult meshValidation = mesh.validate()",
    "OperationResult Scene::editSelectedMesh", "Scene candidate = *this;",
    "++candidate.revision_", "result = mesh.loopCut", "result = mesh.knifeCut",
    "result = mesh.insetFace", "result = mesh.mergeVertices", "result = mesh.extrudeFace",
], "portable Scene implementation")
if "std::function" in scene_header or "std::function" in scene_source:
    fail("portable Scene mesh-edit dispatch must not use std::function")
require_markers(scene_tests, [
    "imported_mesh_creation_is_transactional_and_preserves_mesh_state",
    "selected_mesh_edits_are_atomic_and_advance_both_revisions_once",
    "failed imported mesh creation preserves exact canonical scene bytes",
    "selected mesh operation failure preserves exact canonical scene bytes",
    "advances owned mesh revision exactly once", "advances scene revision exactly once",
], "Scene integration regression coverage")
require_markers(fault_tests, ["scene create imported mesh", "scene selected loop cut"],
                "Scene allocation-fault coverage")
print("[static] OK transactional import and typed selected-object mesh-edit APIs/tests")

render_markers = [
    "for (const octopoly::SceneObject& object : scene.objects())",
    "const octopoly::Mat4 world = object.worldTransform();",
    "mesh.visitTriangles(", "checkedAddSize(triangleCount, 1",
    "checkedMultiplySize(triangleCount, 3", "checkedMultiplySize(renderVertexCount, sizeof(RenderVertex)",
    "std::numeric_limits<NSUInteger>::max()", "pointRepresentable(world.transformPoint(vertex->position))",
    "[NSMutableData dataWithLength:", "world.transformPoint(mesh.vertex(vertexId)->position)",
]
require_markers(bridge, render_markers, "all-object transformed render stream")
if bridge.count("for (const octopoly::SceneObject& object : scene.objects())") < 2:
    fail("render bridge must perform all-object preflight and packing passes")
if ".triangulate(" in bridge or "std::vector<octopoly::Triangle>" in bridge:
    fail("render bridge must not materialize triangles or duplicate triangulation")
print("[static] OK all-object world-transformed, checked, two-pass streamed rendering")

require_markers(header, ["@interface SceneOutlinerItem", "objectId", "getter=isSelected",
                         "- (BOOL)selectObject:", "- (BOOL)deleteObject:", "- (BOOL)renameObject:"],
                "outliner bridge surface")
require_markers(model, ["@Published private(set) var outlinerItems", "selectedObjectId",
                        "sceneRevision", "bridge.outlinerItems.map", "$0.selectObject",
                        "$0.deleteObject", "$0.renameObject"], "scene-aware view model")
require_markers(content, ["GroupBox(\"Scene Outliner\")", "Object ID", "model.selectObject(item.id)",
                          "model.renameSelectedObject", "model.deleteSelectedObject"],
                "visible outliner controls")
outliner_body = block_from(content, "private struct SceneOutliner")
require_markers(outliner_body, ["private struct RenameSyncKey: Equatable",
                                "private var renameSyncKey: RenameSyncKey",
                                "id: model.selectedObjectId", "name: selectedName",
                                ".onChange(of: renameSyncKey)"],
                "selected-ID-and-name-driven rename field synchronization")
for stale_observer in [".onChange(of: selectedName)",
                       ".onChange(of: model.selectedObjectId)",
                       ".onChange(of: model.outlinerItems)"]:
    if stale_observer in outliner_body:
        fail("rename field synchronization must use the composite ID/name key")
print("[static] OK stable-ID outliner selection, selected state, rename, and delete")

primitive_contracts = [
    ("Cube", "addCube", ".cube"), ("Plane", "addPlane", ".plane"),
    ("Tetrahedron", "addTetrahedron", ".tetrahedron"),
    ("Cylinder", "addCylinder", ".cylinder"), ("Cone", "addCone", ".cone"),
    ("UV Sphere", "addUVSphere", ".uvSphere"),
]
require_markers(header, ["typedef NS_ENUM(NSInteger, MeshBridgePrimitive)", "- (BOOL)addPrimitive:"],
                "typed primitive bridge")
require_markers(bridge, ["candidate.createPrimitive(corePrimitive, name)", "MeshBridgePrimitiveCube",
                         "MeshBridgePrimitivePlane", "MeshBridgePrimitiveTetrahedron",
                         "MeshBridgePrimitiveCylinder", "MeshBridgePrimitiveCone",
                         "MeshBridgePrimitiveUVSphere"], "primitive bridge implementation")
for label, method, value in primitive_contracts:
    require_markers(block_from(model, f"func {method}()"), ["addPrimitive(", value],
                    f"{label} view-model action")
    require_markers(content, [f'Button("{label}") {{ model.{method}() }}'],
                    f"{label} reachable control")
add_primitive_body = block_from(bridge, "- (BOOL)addPrimitive:")
require_markers(add_primitive_body, ["const char *name = nullptr;", "mutateSceneCandidate("],
                "nonthrowing primitive dispatch before protected candidate mutation")
if "std::string name" in add_primitive_body:
    fail("addPrimitive must not construct or assign a potentially throwing std::string before protection")
print("[static] OK all six primitive controls reach detached Scene::createPrimitive safely")

require_markers(header, ["translateSelectedByX", "rotateSelectedAroundAxisX", "scaleSelectedByX"],
                "TRS bridge declarations")
require_markers(bridge, ["candidate.setLocalTransform", "normalizedQuaternion", "multiplyQuaternion",
                         "transform.translation", "transform.rotation", "transform.scale"],
                "real TRS bridge implementation")
require_markers(model, ["$0.translateSelected", "$0.rotateSelected", "$0.scaleSelected"],
                "TRS view-model bridge calls")
require_markers(content, ["model.translateX(-0.25)", "model.translateY(0.25)",
                          "model.translateZ(0.25)", "model.rotateX()", "model.rotateY()",
                          "model.rotateZ()", "model.scaleDown()", "model.scaleUp()"],
                "reachable real TRS controls")
print("[static] OK finite translation, normalized-quaternion rotation, and scale controls")

mesh_actions = {
    "loopCut": "candidate.selectedLoopCut(", "knifeCut": "candidate.selectedKnifeCut(",
    "inset": "candidate.selectedInsetFace(", "merge": "candidate.selectedMergeVertices(",
    "extrude": "candidate.selectedExtrudeFace(",
}
for action, core_call in mesh_actions.items():
    require_markers(header, [f"- (BOOL){action};"], f"{action} bridge declaration")
    require_markers(block_from(bridge, f"- (BOOL){action} {{"), [core_call],
                    f"{action} selected Scene bridge implementation")
    require_markers(block_from(model, f"func {action}()"), [f"$0.{action}()"],
                    f"{action} view-model implementation")
    require_markers(content, [f"model.{action}()"], f"{action} reachable control")
print("[static] OK retained Loop/Knife/Inset/Merge/Extrude act through selected Scene object")

require_markers(project_header, ["encodeSceneProject", "decodeSceneProject", "installSceneProject",
                                 "encodeProject", "decodeProject"], "scene and legacy codec APIs")
require_markers(project_source, ["encodeSceneProject", "decodeSceneProject", "installSceneProject"],
                "scene codec implementation")
require_markers(bridge, [
    "octopoly::project::encodeSceneProject(sceneFromStorage(_sceneStorage))",
    "dataHasMagic(data, sceneMagic)", "dataHasMagic(data, legacyMagic)",
    "octopoly::project::decodeSceneProject(bytes)", "octopoly::project::decodeProject(bytes)",
    "candidate.createMeshObject(std::move(decoded.mesh), \"Legacy Project\")",
    "prepareAndCommitScene(_sceneStorage, std::move(candidate)",
    "Project is neither an OCTOSCNE scene nor a legacy OCTOPOLY mesh.",
], "atomic OCTOSCNE/legacy bridge load")
load_project_body = block_from(bridge, "- (BOOL)loadProjectData:")
if load_project_body.count("prepareAndCommitScene(_sceneStorage, std::move(candidate)") != 2:
    fail("both OCTOSCNE and legacy loads must prepare detached snapshots before commit")
if "sceneFromStorage(_sceneStorage) =" in load_project_body:
    fail("project load must not commit a decoded Scene before complete snapshot preparation")
require_markers(model, ["OctoPoly.octoscene", "OctoPoly.octopoly", "write(to: projectURL, options: .atomic)",
                        "Data(contentsOf: sourceURL)", "bridge.loadedLegacyProject"],
                "scene Documents persistence")
require_markers(content, ["Save OCTOSCNE Scene", "Load OCTOSCNE / Legacy"],
                "explicit scene persistence labels")
print("[static] OK complete OCTOSCNE save/load with explicit legacy OCTOPOLY fallback")

require_markers(glb_header, ["encodeGlb", "decodeGlb"], "GLB API")
require_markers(glb_source, ["EncodeResult encodeGlb", "DecodeResult decodeGlb"], "GLB implementation")
export_body = block_from(bridge, "- (NSData *)encodedGlbData {")
require_markers(export_body, ["selectedObject()", "Select one object before exporting GLB",
                              "encodeGlb(selected->mesh())"], "selected-object GLB export")
import_body = block_from(bridge, "- (BOOL)loadGlbData:(NSData *)data {")
require_markers(import_body, ["decodeGlb(bytes)", "octopoly::Scene candidate = sceneFromStorage(_sceneStorage)",
                              "candidate.createMeshObject(", "\"Imported GLB\"",
                              "storeGlbDiagnostics", "prepareAndCommitScene("],
                "detached new-object GLB import")
if import_body.find("storeGlbDiagnostics") > import_body.find("prepareAndCommitScene("):
    fail("GLB diagnostics must be prepared before committing the imported candidate")
if import_body.find("_glbDiagnostics = preparedDiagnostics;") < import_body.find("prepareAndCommitScene("):
    fail("GLB diagnostics must update only after successful Scene/cache commit")
reset_body = block_from(bridge, "- (BOOL)resetSceneCube {")
require_markers(reset_body, ["octopoly::Scene candidate;", "prepareAndCommitScene("],
                "detached reset snapshot commit")
for marker in ["_glbDiagnostics = @\"\";", "_loadedLegacyProject = NO;"]:
    if reset_body.find(marker) < reset_body.find("prepareAndCommitScene("):
        fail("reset diagnostic/legacy flags must update only after successful commit")
require_markers(model, ["Exported selected object only", "Imported GLB as new selected",
                        "geometry only", "bridge.glbDiagnostics"], "GLB visible loss policy")
require_markers(content, ["Export Selected GLB", "Import GLB as New Object"],
                "GLB explicit controls")
print("[static] OK selected-only GLB export and diagnostic new-object import")

for signature in [
    "- (BOOL)selectObject:", "- (BOOL)deleteObject:", "- (BOOL)renameObject:",
    "- (BOOL)addPrimitive:", "- (BOOL)loopCut {", "- (BOOL)knifeCut {",
    "- (BOOL)inset {", "- (BOOL)merge {", "- (BOOL)extrude {",
]:
    require_markers(block_from(bridge, signature), ["mutateSceneCandidate("],
                    f"detached coherent mutation wrapper {signature}")
for signature in ["- (BOOL)translateSelectedByX:", "- (BOOL)rotateSelectedAroundAxisX:",
                  "- (BOOL)scaleSelectedByX:"]:
    require_markers(block_from(bridge, signature), ["mutateSelectedTransform("],
                    f"detached coherent transform wrapper {signature}")
for signature in ["- (instancetype)init {", "- (NSData *)encodedProjectData {",
                  "- (BOOL)loadProjectData:", "- (NSData *)encodedGlbData {",
                  "- (BOOL)loadGlbData:", "- (BOOL)resetSceneCube {"]:
    public_body = block_from(bridge, signature)
    require_markers(public_body, ["catch (const std::bad_alloc&)", "catch (...)"],
                    f"contained Objective-C++ boundary {signature}")
require_markers(bridge, [
    "static octopoly::Scene& sceneFromStorage(void *storage) noexcept",
    "static_assert(std::is_nothrow_move_assignable_v<octopoly::Scene>)",
], "nonthrowing committed Scene access/commit")
print("[static] OK public Objective-C++ boundaries contain fallible C++ work")

perform_body = block_from(model, "private func performMutation")
require_markers(perform_body, ["guard operation(bridge) else", "guard refreshSceneState() else"],
                "success-only mutation publication")
if perform_body.find("refreshSceneState()") < perform_body.find("guard operation(bridge) else"):
    fail("failed operations must not publish refreshed Scene state")
refresh_body = block_from(model, "private func refreshSceneState()")
require_markers(refresh_body, ["let geometry = bridge.triangleVertexData", "let rows = bridge.outlinerItems.map",
                               "let selected = UInt64(bridge.selectedObjectId)",
                               "let revision = UInt64(bridge.sceneRevision)",
                               "vertexData = geometry", "outlinerItems = rows",
                               "selectedObjectId = selected", "sceneRevision = revision"],
                "synchronous cached scene-aware view-model refresh")
if "bridge.lastError" in refresh_body:
    fail("cached coherent bridge getters must not introduce a second fallible refresh gate")
print("[static] OK successful bridge commits synchronously publish cached geometry plus outliner")

scene_doc = read("docs/SCENE.md")
phase4_doc = read("docs/verification/phase-4.md")
readme = read("README.md")
require_markers(readme, ["활성화할 때는 사용하는 GitHub 자격 증명의 `workflow` 권한을 확인해야 한다"],
                "conditional GitHub workflow activation guidance")
if "현재 GitHub OAuth 자격 증명에 `workflow` scope가 없어" in readme:
    fail("README must not claim transient local GitHub credential state")
require_markers(scene_doc, ["createMeshObject", "no `std::function` allocation", "worldTransform()",
                            "OctoPoly.octoscene", "legacy `OCTOPOLY`", "selected object's Mesh"],
                "SCENE contract documentation")
require_markers(phase4_doc, ["Observed RED", "Observed GREEN", "static Apple", "No Xcode",
                             "ASan", "UBSan", "_GLIBCXX_DEBUG", "PEDANTIC",
                             "<!-- phase4-candidate-files:start -->",
                             "<!-- phase4-candidate-files:end -->"],
                "Phase 4 verification record")
base_match = re.search(r"^Base HEAD: `([0-9a-f]{40})`$", phase4_doc, re.MULTILINE)
fingerprint_match = re.search(
    r"^Candidate fingerprint: `(sha256:[0-9a-f]{64})`$", phase4_doc, re.MULTILINE
)
manifest_match = re.search(
    r"<!-- phase4-candidate-files:start -->\n(.*?)\n<!-- phase4-candidate-files:end -->",
    phase4_doc, re.DOTALL,
)
if base_match is None or fingerprint_match is None or manifest_match is None:
    fail("Phase 4 provenance fields are missing or malformed")
manifest_paths = re.findall(r"^- `([^`]+)`$", manifest_match.group(1), re.MULTILINE)
if not manifest_paths or manifest_paths != sorted(set(manifest_paths)):
    fail("Phase 4 candidate manifest must be nonempty, unique, and sorted")
for relative in manifest_paths:
    if relative.startswith("/") or ".." in Path(relative).parts:
        fail(f"unsafe Phase 4 candidate manifest path: {relative}")
base_head = base_match.group(1)
current_head = git("rev-parse", "HEAD")
ancestor = subprocess.run(
    ["git", "merge-base", "--is-ancestor", base_head, current_head], cwd=ROOT,
    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
).returncode == 0
if not ancestor:
    fail("Phase 4 provenance base HEAD is not an ancestor of the current HEAD")
if current_head == base_head:
    changed = set(filter(None, git("diff", "--name-only", base_head).splitlines()))
    changed.update(filter(None, git("ls-files", "--others", "--exclude-standard").splitlines()))
    if changed != set(manifest_paths):
        missing = sorted(changed - set(manifest_paths))
        stale = sorted(set(manifest_paths) - changed)
        fail(f"Phase 4 candidate manifest mismatch; missing={missing}, stale={stale}")
working_fingerprint = phase4_candidate_fingerprint(manifest_paths)
recorded_fingerprint = fingerprint_match.group(1).removeprefix("sha256:")
matching_snapshot = (
    "working tree"
    if current_head == base_head and recorded_fingerprint == working_fingerprint
    else ""
)
if not matching_snapshot and current_head != base_head:
    manifest_set = set(manifest_paths)
    for commit in filter(None, git("rev-list", "--reverse", f"{base_head}..{current_head}").splitlines()):
        commit_boundary = set(filter(None, git(
            "diff", "--name-only", base_head, commit
        ).splitlines()))
        if commit_boundary != manifest_set:
            continue
        all_paths_exist = all(
            subprocess.run(
                ["git", "cat-file", "-e", f"{commit}:{relative}"], cwd=ROOT,
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            ).returncode == 0
            for relative in manifest_paths
        )
        if all_paths_exist and phase4_candidate_fingerprint_at_commit(
            commit, manifest_paths
        ) == recorded_fingerprint:
            matching_snapshot = commit
            break
if not matching_snapshot:
    fail(
        "Phase 4 candidate fingerprint mismatch: "
        f"recorded={recorded_fingerprint}, current={working_fingerprint}, "
        "and no matching ancestral candidate snapshot exists"
    )
if phase4_doc.count(f"`sha256:{recorded_fingerprint}`") < 5:
    fail("each Phase 4 final gate result must reference the candidate fingerprint")
print(f"[static] OK Phase 4 evidence provenance matches {matching_snapshot}")
remote = read("scripts/mac/remote-build.sh")
install = read("scripts/mac/install-device.sh")
macos_ci = read("ci/github-actions/macos.yml")
require_markers(remote, ["ssh", "xcodebuild"], "remote Mac helper")
require_markers(install, ["xcrun devicectl device install app"], "device helper")
require_markers(macos_ci, ["CODE_SIGNING_ALLOWED=NO"], "macOS CI template")
print("[static] OK documentation and truthful non-macOS handoff boundary")
print("[static] PASS (portable/static validation only; no Xcode compile claim)")
