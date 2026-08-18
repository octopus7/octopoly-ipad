#!/usr/bin/env python3
"""Deterministic Phase 3 structure and action-reachability validation.

This intentionally does not claim to parse or compile an Xcode project. It checks
that required files are referenced by the project, modelling actions have a literal
Swift -> Objective-C++ -> portable-core path, and project/GLB file actions reach
real Swift I/O, the bridge, and their portable codecs.
"""

from __future__ import annotations

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / "app/OctoPolyIPad/OctoPolyIPad.xcodeproj/project.pbxproj"
SOURCES = ROOT / "app/OctoPolyIPad/Sources"


def fail(message: str) -> None:
    print(f"[static] FAIL {message}", file=sys.stderr)
    raise SystemExit(1)


def read(relative: str) -> str:
    path = ROOT / relative
    if not path.is_file():
        fail(f"missing required file: {relative}")
    return path.read_text(encoding="utf-8")


required_files = [
    "LICENSE",
    "README.md",
    "ROADMAP.md",
    "docs/FORMAT.md",
    "docs/GLB.md",
    "docs/verification/phase-1.md",
    "docs/verification/phase-2.md",
    "docs/verification/phase-3.md",
    "CMakeLists.txt",
    "core/include/octopoly/mesh.hpp",
    "core/include/octopoly/project_codec.hpp",
    "core/include/octopoly/glb_codec.hpp",
    "core/src/mesh.cpp",
    "core/src/project_codec.cpp",
    "core/src/glb_codec.cpp",
    "tests/test_mesh.cpp",
    "tests/test_project_codec.cpp",
    "tests/test_mesh_allocation_faults.cpp",
    "tests/test_glb_codec.cpp",
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
    "scripts/mac/remote-build.sh",
    "scripts/mac/install-device.sh",
    "scripts/check.sh",
    "ci/github-actions/linux.yml",
    "ci/github-actions/macos.yml",
]
for relative in required_files:
    if not (ROOT / relative).is_file():
        fail(f"missing required file: {relative}")
print(f"[static] OK required files ({len(required_files)})")

license_text = read("LICENSE")
if not license_text.startswith("MIT License\n") or "Copyright (c) 2026 octopus7" not in license_text:
    fail("LICENSE must be MIT and identify Copyright (c) 2026 octopus7")
roadmap = read("ROADMAP.md")
required_phase_markers = {
    1: ["Portable mesh core", "Loop Cut", "Extrude"],
    2: ["Native project save and load", "자체 포맷", "원자적 로드"],
    3: ["GLB import and export", "glTF 2.0 Binary"],
    4: ["Primitives, scene outliner, and world transforms", "오브젝트별 고유"],
    5: ["Mirror modifier, center merge, and clipping", "X/Y/Z"],
    6: ["Element transforms and edit history", "Undo/Redo", "디졸브"],
    7: ["Extended topology tools", "Bevel", "Flip Normals"],
    8: ["Retopology tools", "Poly Build", "Relax"],
    9: ["UV and texture painting", "텍스처 페인팅"],
    10: ["Armature and weight painting", "웨이트 페인팅"],
}
for phase, markers in required_phase_markers.items():
    if f"## Phase {phase} " not in roadmap:
        fail(f"ROADMAP.md missing Phase {phase}")
    for marker in markers:
        if marker not in roadmap:
            fail(f"ROADMAP.md Phase {phase} missing required scope marker: {marker}")
if "## Phase 1 — Portable mesh core and iPad shell (complete)" not in roadmap:
    fail("ROADMAP.md must keep Phase 1 complete")
if "## Phase 2 — Native project save and load (complete)" not in roadmap:
    fail("ROADMAP.md must mark Phase 2 complete")
if "## Phase 3 — GLB import and export (complete)" not in roadmap:
    fail("ROADMAP.md must mark Phase 3 complete")
for phase in range(4, 11):
    if not re.search(rf"^## Phase {phase} .* \(planned\)$", roadmap, re.MULTILINE):
        fail(f"ROADMAP.md must keep Phase {phase} planned")
print("[static] OK MIT license and required roadmap phases 1-10")

cmake = read("CMakeLists.txt")
cmake_markers = [
    "add_library(octopoly_core STATIC",
    "core/src/project_codec.cpp",
    "core/src/glb_codec.cpp",
    "add_executable(octopoly_core_tests",
    "tests/test_mesh.cpp",
    "target_link_libraries(octopoly_core_tests PRIVATE octopoly_core)",
    "add_executable(project_codec_tests",
    "tests/test_project_codec.cpp",
    "target_link_libraries(project_codec_tests PRIVATE octopoly_core)",
    "add_executable(mesh_allocation_fault_tests",
    "tests/test_mesh_allocation_faults.cpp",
    "target_link_libraries(mesh_allocation_fault_tests PRIVATE octopoly_core)",
    "add_executable(glb_codec_tests",
    "tests/test_glb_codec.cpp",
    "target_link_libraries(glb_codec_tests PRIVATE octopoly_core)",
    "target_compile_options(project_codec_tests PRIVATE /W4 /WX /permissive-)",
    "target_compile_options(project_codec_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)",
    "target_compile_options(mesh_allocation_fault_tests PRIVATE /W4 /WX /permissive-)",
    "target_compile_options(mesh_allocation_fault_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)",
    "target_compile_options(glb_codec_tests PRIVATE /W4 /WX /permissive-)",
    "target_compile_options(glb_codec_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)",
    "add_test(NAME octopoly_core_tests COMMAND octopoly_core_tests)",
    "add_test(NAME project_codec_tests COMMAND project_codec_tests)",
    "add_test(NAME mesh_allocation_fault_tests COMMAND mesh_allocation_fault_tests)",
    "add_test(NAME glb_codec_tests COMMAND glb_codec_tests)",
]
for marker in cmake_markers:
    if marker not in cmake:
        fail(f"CMakeLists.txt missing target marker: {marker}")
print("[static] OK CMake core and all four portable warning-clean test targets")

check_script = read("scripts/check.sh")
check_markers = [
    "core/src/mesh.cpp tests/test_mesh.cpp",
    "core/src/mesh.cpp core/src/project_codec.cpp tests/test_project_codec.cpp",
    "core/src/mesh.cpp core/src/project_codec.cpp core/src/glb_codec.cpp tests/test_mesh_allocation_faults.cpp",
    "core/src/mesh.cpp core/src/project_codec.cpp core/src/glb_codec.cpp tests/test_glb_codec.cpp",
    "./build/check/octopoly_core_tests",
    "./build/check/project_codec_tests",
    "./build/check/mesh_allocation_fault_tests",
    "./build/check/glb_codec_tests",
    "python3 scripts/validate_project.py",
]
for marker in check_markers:
    if marker not in check_script:
        fail(f"scripts/check.sh missing Phase 3 gate marker: {marker}")
if check_script.count("-std=c++20 -Wall -Wextra -Wpedantic -Werror") != 4:
    fail("scripts/check.sh must warning-clean compile all four C++20 test suites")
print("[static] OK check.sh warning-clean mesh/project/GLB/fault build and execution wiring")

pbx = read("app/OctoPolyIPad/OctoPolyIPad.xcodeproj/project.pbxproj")
for opening, closing, name in [("{", "}", "braces"), ("(", ")", "parentheses")]:
    if pbx.count(opening) != pbx.count(closing):
        fail(f"pbxproj has unbalanced {name}")
declared_ids = re.findall(r"^\t\t([0-9A-F]{24})(?: /\*.*?\*/)? = \{", pbx, re.MULTILINE)
if len(declared_ids) != len(set(declared_ids)):
    fail("pbxproj contains duplicate object declarations")
referenced_ids = set(re.findall(r"\b[0-9A-F]{24}\b", pbx))
undefined_ids = sorted(referenced_ids - set(declared_ids))
if undefined_ids:
    fail(f"pbxproj references undefined object IDs: {', '.join(undefined_ids)}")


def pbx_object_body(object_id: str) -> str:
    match = re.search(
        rf"^\t\t{object_id}(?: /\*.*?\*/)? = \{{\n(.*?)^\t\t\}};",
        pbx,
        re.MULTILINE | re.DOTALL,
    )
    if match is None:
        fail(f"pbxproj missing object declaration: {object_id}")
    return match.group(1)


codec_pbx_markers = [
    "00000000000000000000010E /* project_codec.cpp */ = {isa = PBXFileReference; lastKnownFileType = sourcecode.cpp.cpp; path = src/project_codec.cpp; sourceTree = \"<group>\"; };",
    "00000000000000000000010F /* project_codec.hpp */ = {isa = PBXFileReference; lastKnownFileType = sourcecode.cpp.h; path = include/octopoly/project_codec.hpp; sourceTree = \"<group>\"; };",
    "00000000000000000000020B /* project_codec.cpp in Sources */ = {isa = PBXBuildFile; fileRef = 00000000000000000000010E /* project_codec.cpp */; };",
    "000000000000000000000110 /* glb_codec.cpp */ = {isa = PBXFileReference; lastKnownFileType = sourcecode.cpp.cpp; path = src/glb_codec.cpp; sourceTree = \"<group>\"; };",
    "000000000000000000000111 /* glb_codec.hpp */ = {isa = PBXFileReference; lastKnownFileType = sourcecode.cpp.h; path = include/octopoly/glb_codec.hpp; sourceTree = \"<group>\"; };",
    "00000000000000000000020C /* glb_codec.cpp in Sources */ = {isa = PBXBuildFile; fileRef = 000000000000000000000110 /* glb_codec.cpp */; };",
]
for marker in codec_pbx_markers:
    if marker not in pbx:
        fail(f"pbxproj codec wiring missing deterministic declaration: {marker}")

file_reference_section = pbx.split("/* Begin PBXFileReference section */", 1)[1].split(
    "/* End PBXFileReference section */", 1
)[0]
for filename in ["project_codec.cpp", "project_codec.hpp", "glb_codec.cpp", "glb_codec.hpp"]:
    if file_reference_section.count(f"/* {filename} */") != 1:
        fail(f"pbxproj must declare exactly one file reference for {filename}")

portable_core_group = pbx_object_body("000000000000000000000005")
for object_id, filename in [
    ("00000000000000000000010E", "project_codec.cpp"),
    ("00000000000000000000010F", "project_codec.hpp"),
    ("000000000000000000000110", "glb_codec.cpp"),
    ("000000000000000000000111", "glb_codec.hpp"),
]:
    if f"{object_id} /* {filename} */," not in portable_core_group:
        fail(f"Portable Core group missing {filename}")

sources_phase = pbx_object_body("00000000000000000000000F")
if "00000000000000000000020B /* project_codec.cpp in Sources */," not in sources_phase:
    fail("Sources build phase missing project_codec.cpp build file")
if "00000000000000000000020C /* glb_codec.cpp in Sources */," not in sources_phase:
    fail("Sources build phase missing glb_codec.cpp build file")

project_directory = PROJECT.parent.parent
for relative in [
    "../../core/src/project_codec.cpp",
    "../../core/include/octopoly/project_codec.hpp",
    "../../core/src/glb_codec.cpp",
    "../../core/include/octopoly/glb_codec.hpp",
]:
    if not (project_directory / relative).resolve().is_file():
        fail(f"pbxproj codec path does not resolve to a file: {relative}")

pbx_sources = [
    "OctoPolyIPadApp.swift",
    "ContentView.swift",
    "MeshViewModel.swift",
    "MetalViewport.swift",
    "MeshRenderer.swift",
    "Shaders.metal",
    "MeshBridge.mm",
    "mesh.cpp",
    "project_codec.cpp",
    "glb_codec.cpp",
]
for filename in pbx_sources:
    marker = f"{filename} in Sources"
    if marker not in pbx:
        fail(f"pbxproj source build phase missing {filename}")

pbx_headers = ["MeshBridge.h", "OctoPolyIPad-Bridging-Header.h", "mesh.hpp",
               "project_codec.hpp", "glb_codec.hpp"]
for filename in pbx_headers:
    if filename not in pbx:
        fail(f"pbxproj file reference missing {filename}")

for setting in [
    'SWIFT_OBJC_BRIDGING_HEADER = "Sources/OctoPolyIPad-Bridging-Header.h";',
    'HEADER_SEARCH_PATHS = "$(PROJECT_DIR)/../../core/include";',
    'CLANG_CXX_LANGUAGE_STANDARD = "c++20";',
    "TARGETED_DEVICE_FAMILY = 2;",
]:
    if setting not in pbx:
        fail(f"pbxproj required build setting missing: {setting}")
print(
    f"[static] OK pbxproj IDs, codec paths/group/build phase, source references "
    f"({len(pbx_sources) + len(pbx_headers)}), and build settings"
)

header = read("app/OctoPolyIPad/Sources/MeshBridge.h")
bridge = read("app/OctoPolyIPad/Sources/MeshBridge.mm")
model = read("app/OctoPolyIPad/Sources/MeshViewModel.swift")
content = read("app/OctoPolyIPad/Sources/ContentView.swift")
codec_header = read("core/include/octopoly/project_codec.hpp")
codec_source = read("core/src/project_codec.cpp")
glb_header = read("core/include/octopoly/glb_codec.hpp")
glb_source = read("core/src/glb_codec.cpp")

actions = {
    "loopCut": "mesh.loopCut(",
    "knifeCut": "mesh.knifeCut(",
    "inset": "mesh.insetFace(",
    "merge": "mesh.mergeVertices(",
    "extrude": "mesh.extrudeFace(",
}
for action, core_call in actions.items():
    checks = [
        (f"- (BOOL){action};", header, "bridge declaration"),
        (f"- (BOOL){action} {{", bridge, "bridge implementation"),
        (core_call, bridge, "portable core call"),
        (f"func {action}()", model, "view-model action"),
        (f"bridge.{action}()", model, "view-model bridge call"),
        (f"model.{action}()", content, "button action"),
    ]
    for needle, haystack, stage in checks:
        if needle not in haystack:
            fail(f"{action}: missing {stage}: {needle}")

if "let succeeded = operation()\n        refreshGeometry()" not in model:
    fail("view-model operation path does not refresh geometry immediately")
print(f"[static] OK UI -> bridge -> core action reachability ({len(actions)} actions)")

save_load_markers = [
    ("@property(nonatomic, readonly, nullable) NSData *encodedProjectData;", header,
     "bridge encoded NSData declaration"),
    ("- (BOOL)loadProjectData:(NSData *)data;", header, "bridge atomic load declaration"),
    ("octopoly::project::encodeProject(mesh)", bridge, "bridge encoder call"),
    ("octopoly::project::installProject(mesh, bytes)", bridge, "bridge atomic installer call"),
    ("EncodeResult encodeProject(const Mesh& mesh) noexcept", codec_header,
     "portable encoder declaration"),
    ("InstallResult installProject(Mesh& liveMesh", codec_header,
     "portable atomic installer declaration"),
    ("EncodeResult encodeProject(const Mesh& mesh) noexcept", codec_source,
     "portable encoder implementation"),
    ("DecodeResult decoded = decodeProject(bytes, limits);", codec_source,
     "installer detached decode"),
    ("liveMesh = std::move(decoded.mesh);", codec_source,
     "installer success-only commit"),
    ("func saveProject()", model, "view-model save action"),
    ("func loadProject()", model, "view-model load action"),
    ("OctoPoly.octopoly", model, "Documents project filename"),
    ("FileManager.default.urls(for: .documentDirectory", model, "Documents directory lookup"),
    ("data.write(to: projectURL, options: .atomic)", model, "atomic Swift file write"),
    ("Data(contentsOf: projectURL)", model, "Swift file read"),
    ("bridge.encodedProjectData", model, "Swift encoder bridge call"),
    ("if bridge.loadProjectData(data) {\n                refreshGeometry()", model,
     "successful-load-only geometry refresh"),
    ("Button(\"Save\") { model.saveProject() }", content, "reachable Save control"),
    ("Button(\"Load\") { model.loadProject() }", content, "reachable Load control"),
]
for marker, source, label in save_load_markers:
    if marker not in source:
        fail(f"Save/Load path missing {label}: {marker}")
print("[static] OK Save/Load UI -> Swift atomic Documents I/O -> bridge -> codec reachability")

glb_markers = [
    ("@property(nonatomic, readonly, nullable) NSData *encodedGlbData;", header,
     "bridge GLB encoder declaration"),
    ("- (BOOL)loadGlbData:(NSData *)data;", header, "bridge GLB installer declaration"),
    ("octopoly::glb::encodeGlb(mesh)", bridge, "bridge GLB encoder call"),
    ("octopoly::glb::installGlb(mesh, bytes)", bridge, "bridge GLB installer call"),
    ("EncodeResult encodeGlb(const Mesh& mesh) noexcept", glb_header,
     "portable GLB encoder declaration"),
    ("InstallResult installGlb(Mesh& liveMesh", glb_header,
     "portable GLB installer declaration"),
    ("DecodeResult decoded = decodeGlb(bytes, limits);", glb_source,
     "GLB installer detached decode"),
    ("liveMesh = std::move(decoded.mesh);", glb_source, "GLB success-only commit"),
    ("func exportGlb()", model, "view-model GLB export action"),
    ("func importGlb()", model, "view-model GLB import action"),
    ("OctoPoly.glb", model, "Documents GLB filename"),
    ("data.write(to: glbURL, options: .atomic)", model, "atomic GLB write"),
    ("Data(contentsOf: glbURL)", model, "GLB file read"),
    ("bridge.encodedGlbData", model, "Swift GLB encoder bridge call"),
    ("if bridge.loadGlbData(data) {\n                refreshGeometry()", model,
     "successful-import-only geometry refresh"),
    ("Button(\"Export GLB\") { model.exportGlb() }", content, "reachable GLB export"),
    ("Button(\"Import GLB\") { model.importGlb() }", content, "reachable GLB import"),
]
for marker, source, label in glb_markers:
    if marker not in source:
        fail(f"GLB path missing {label}: {marker}")
print("[static] OK GLB UI -> atomic Documents I/O -> bridge -> portable codec reachability")

format_doc = read("docs/FORMAT.md")
format_markers = [
    "32-byte header",
    "`OCTOPOLY`",
    "little-endian",
    "CRC-32/IEEE",
    "48-byte payload prefix",
    "64 MiB",
    "1,000,000",
    "4,000,000",
    "Data.write(to:options: .atomic)",
    "derived stable-vertex lookup index",
    "allocation-free triangle visitation",
    "no transient `std::vector<Triangle>` copy",
]
for marker in format_markers:
    if marker not in format_doc:
        fail(f"docs/FORMAT.md missing implemented-format marker: {marker}")
print("[static] OK implemented wire-format and atomicity documentation markers")

glb_doc = read("docs/GLB.md")
for marker in ["glTF 2.0", "TRIANGLES", "FLOAT VEC3", "UNSIGNED_BYTE",
               "UNSIGNED_SHORT", "UNSIGNED_INT", "64 MiB", "polygon-to-triangle",
               "atomic", "diagnostic"]:
    if marker not in glb_doc:
        fail(f"docs/GLB.md missing supported-subset marker: {marker}")
print("[static] OK documented GLB subset, limits, diagnostics, and loss policy")

render_markers = [
    ("octopoly::Mesh::makeDefaultCube()", bridge, "default cube"),
    ("mesh.visitTriangles(", bridge, "allocation-free core triangle visitation"),
    ("checkedMultiplySize(triangleCount, 3", bridge, "checked render vertex count"),
    ("checkedMultiplySize(renderVertexCount, sizeof(RenderVertex)", bridge,
     "checked render byte capacity"),
    ("std::numeric_limits<NSUInteger>::max()", bridge, "Foundation capacity bound"),
    ("@property(nonatomic, readonly) NSData *triangleVertexData;", header, "bridge geometry property"),
    ("bridge.triangleVertexData as Data", model, "geometry publication"),
    ("MetalViewport(model: model)", content, "Metal viewport"),
    ("drawPrimitives(type: .triangle", read("app/OctoPolyIPad/Sources/MeshRenderer.swift"), "triangle draw"),
]
for marker, source, label in render_markers:
    if marker not in source:
        fail(f"render path missing {label}: {marker}")
print("[static] OK cube -> streamed core triangulation -> checked bridge buffer -> Metal render path")

remote = read("scripts/mac/remote-build.sh")
install = read("scripts/mac/install-device.sh")
macos_ci = read("ci/github-actions/macos.yml")
if "ssh" not in remote or "xcodebuild" not in remote:
    fail("remote-build.sh must invoke xcodebuild over SSH")
if "xcrun devicectl device install app" not in install:
    fail("install-device.sh must invoke xcrun devicectl device install app")
if "CODE_SIGNING_ALLOWED=NO" not in macos_ci:
    fail("macOS CI must disable signing")
print("[static] OK remote build, device install, and unsigned simulator CI template wiring")
print("[static] PASS")
