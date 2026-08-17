#!/usr/bin/env python3
"""Deterministic Phase 1 structure and action-reachability validation.

This intentionally does not claim to parse or compile an Xcode project. It checks
that required files are referenced by the project and that every UI action has a
literal Swift -> Objective-C++ -> portable-core path followed by geometry refresh.
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
    "docs/verification/phase-1.md",
    "CMakeLists.txt",
    "core/include/octopoly/mesh.hpp",
    "core/src/mesh.cpp",
    "tests/test_mesh.cpp",
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
print("[static] OK MIT license and required roadmap phases 1-10")

cmake = read("CMakeLists.txt")
for marker in ["add_library(octopoly_core STATIC", "add_executable(octopoly_core_tests", "add_test(NAME octopoly_core_tests"]:
    if marker not in cmake:
        fail(f"CMakeLists.txt missing target marker: {marker}")
print("[static] OK CMake portable core and test targets")

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

pbx_sources = [
    "OctoPolyIPadApp.swift",
    "ContentView.swift",
    "MeshViewModel.swift",
    "MetalViewport.swift",
    "MeshRenderer.swift",
    "Shaders.metal",
    "MeshBridge.mm",
    "mesh.cpp",
]
for filename in pbx_sources:
    marker = f"{filename} in Sources"
    if marker not in pbx:
        fail(f"pbxproj source build phase missing {filename}")

for filename in ["MeshBridge.h", "OctoPolyIPad-Bridging-Header.h", "mesh.hpp"]:
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
print(f"[static] OK pbxproj source references ({len(pbx_sources) + 3}) and build settings")

header = read("app/OctoPolyIPad/Sources/MeshBridge.h")
bridge = read("app/OctoPolyIPad/Sources/MeshBridge.mm")
model = read("app/OctoPolyIPad/Sources/MeshViewModel.swift")
content = read("app/OctoPolyIPad/Sources/ContentView.swift")

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

render_markers = [
    ("octopoly::Mesh::makeDefaultCube()", bridge, "default cube"),
    ("mesh.triangulate()", bridge, "core triangulation"),
    ("@property(nonatomic, readonly) NSData *triangleVertexData;", header, "bridge geometry property"),
    ("bridge.triangleVertexData as Data", model, "geometry publication"),
    ("MetalViewport(model: model)", content, "Metal viewport"),
    ("drawPrimitives(type: .triangle", read("app/OctoPolyIPad/Sources/MeshRenderer.swift"), "triangle draw"),
]
for marker, source, label in render_markers:
    if marker not in source:
        fail(f"render path missing {label}: {marker}")
print("[static] OK cube -> triangulation -> bridge -> Metal triangle render path")

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
