# Phase 4 verification — Scene-backed iPad/static integration

Verification date: 2026-08-20

## Candidate provenance

Base HEAD: `d45f261c9aa0b03f1787cf2a8e093d1533cd1e25`

Candidate fingerprint: `sha256:678178043ec974dbf6d0df2b2a492b79c0dbb34038d80b09d2d3ab9b5c28e785`

The fingerprint is SHA-256 over the sorted manifest below. Each entry contributes its UTF-8 repository-relative path, Git executable mode (`0644` or `0755`), byte length, and complete file bytes. Every `sha256:<64 lowercase hex>` value in this verification record is normalized before hashing so the record can identify itself without a circular digest. Before the candidate is committed, the validator also requires this manifest to equal the complete tracked and non-ignored untracked diff from the base HEAD. After commit, it requires the base to remain an ancestor and finds an immutable matching candidate snapshot whose complete base-diff boundary equals the manifest, so unrelated later documentation does not invalidate Phase 4 evidence.

<!-- phase4-candidate-files:start -->
- `CMakeLists.txt`
- `README.md`
- `ROADMAP.md`
- `app/OctoPolyIPad/OctoPolyIPad.xcodeproj/project.pbxproj`
- `app/OctoPolyIPad/Sources/ContentView.swift`
- `app/OctoPolyIPad/Sources/MeshBridge.h`
- `app/OctoPolyIPad/Sources/MeshBridge.mm`
- `app/OctoPolyIPad/Sources/MeshViewModel.swift`
- `core/include/octopoly/mesh.hpp`
- `core/include/octopoly/project_codec.hpp`
- `core/include/octopoly/scene.hpp`
- `core/src/mesh.cpp`
- `core/src/project_codec.cpp`
- `core/src/scene.cpp`
- `docs/SCENE.md`
- `docs/verification/phase-4-core.md`
- `docs/verification/phase-4.md`
- `scripts/check.sh`
- `scripts/validate_project.py`
- `tests/test_mesh_allocation_faults.cpp`
- `tests/test_project_codec.cpp`
- `tests/test_scene.cpp`
<!-- phase4-candidate-files:end -->

## Scope and evidence boundary

This record covers the Phase 4 integration increment after the portable core-only gate: transactional imported Mesh ownership, retained selected-object Mesh editing through `Scene`, a Scene-backed Objective-C++ bridge, all-object world-space rendering, scene outliner and object controls, complete `OCTOSCNE` persistence with explicit legacy `OCTOPOLY` fallback, selected-object GLB export, new-object GLB import, deterministic PBX wiring, and SwiftUI reachability.

Portable C++ was compiled and executed on WSL/Linux. Objective-C++, SwiftUI, Metal, and `project.pbxproj` were checked at the static Apple boundary. **No Xcode build, simulator launch, signing, installation, or physical-device run was executed or claimed.**

## Strict TDD record

### Imported/legacy Mesh Scene API

**Observed RED** after adding `imported_mesh_creation_is_transactional_and_preserves_mesh_state`:

```console
tests/test_scene.cpp:291:32: error: ‘class octopoly::Scene’ has no member named ‘createMeshObject’
tests/test_scene.cpp:318:31: error: ‘class octopoly::Scene’ has no member named ‘createMeshObject’
```

The minimal GREEN added `Scene::createMeshObject(Mesh, std::string)`: validate name and complete Mesh state/counters, preflight terminal Scene counters, copy and modify a detached Scene candidate, append/select without rewriting Mesh IDs/counters/revision, validate, advance Scene revision once, and move-commit.

**Observed GREEN:**

```console
PASS imported_mesh_creation_is_transactional_and_preserves_mesh_state
1 test(s), 0 failure(s)
```

### Selected-object retained Mesh edits

**Observed RED** after adding no-selection, failure atomicity, canonical-byte, ID-result, and dual-revision tests:

```console
error: ‘class octopoly::Scene’ has no member named ‘selectedLoopCut’
error: ‘class octopoly::Scene’ has no member named ‘selectedKnifeCut’
error: ‘class octopoly::Scene’ has no member named ‘selectedInsetFace’
error: ‘class octopoly::Scene’ has no member named ‘selectedMergeVertices’
error: ‘class octopoly::Scene’ has no member named ‘selectedExtrudeFace’
```

The minimal GREEN added concrete wrappers backed by a private typed enum request; there is no `std::function`. A wrapper copies the Scene, invokes the existing atomic Mesh operation on the selected candidate object, returns Mesh failure unchanged, validates successful candidate state, advances Scene revision once, and move-commits. The Mesh operation itself advances only the owned Mesh revision and preserves created/affected stable IDs.

**Observed GREEN:**

```console
PASS selected_mesh_edits_are_atomic_and_advance_both_revisions_once
1 test(s), 0 failure(s)
```

### Allocation hardening

Allocation-ordinal characterization/hardening was expanded after the APIs existed. Imported-mesh creation and selected Loop Cut both observed injected allocation failures while preserving exact canonical Scene bytes; the first non-failing ordinal succeeded:

```console
PASS scene create imported mesh allocation failures are atomic
PASS scene selected loop cut allocation failures are atomic
16 test(s), 0 failure(s)
```

The focused Scene suite after both TDD slices was:

```console
15 test(s), 0 failure(s)
```

### Legacy encode-error ABI preservation

Final independent core review found that inserting the scene-only `invalidScene` enumerator had shifted the existing numeric `EncodeErrorCode` values. Compile-time assertions first failed with `integerOverflow` observed as 3 instead of 2, `allocationFailed` as 4 instead of 3, and `internalError` as 5 instead of 4. The legacy values are now explicit and unchanged (`none` 0, `invalidMesh` 1, `integerOverflow` 2, `allocationFailed` 3, `internalError` 4), with `invalidScene` appended as 5. The focused project-codec suite returned `16 test(s), 0 failure(s)`.

### Apple integration review blockers: coherent publication and boundary safety

A separate Apple integration review found three blockers after the initial Phase 4 static pass:

1. `addPrimitive:` assigned primitive labels into a `std::string` before entering its `try`, so allocation failure could cross the Objective-C++ method boundary.
2. Scene mutation/load/import committed the live `Scene` before Swift asked `triangleVertexData` to perform fallible render preparation. A Float32-range rejection or render/outliner allocation failure could therefore leave the bridge's private Scene newer than the geometry/outliner still published by Swift.
3. `init` could fail initial Cube allocation, install a nothrow empty fallback that could itself remain null, and later let the shared transform helper dereference that storage without a null guard.
4. Follow-up review found that the corrected `init` failure path returned `nil` while the public header still inherited a nonnull initializer contract. The header now explicitly declares `- (nullable instancetype)init`, and `MeshViewModel` stores an optional bridge, reports initialization failure, and guards every save/load/import/export/mutation/refresh access instead of force-unwrapping a failed native initialization.

Static regression assertions were added first. The initial focused run observed RED:

```console
[static] FAIL Scene-backed coherent snapshot declaration missing: NSData *_cachedTriangleVertexData;
```

The fix makes `MeshBridge` own cached triangle `NSData` and cached outliner `NSArray` prepared from the same committed Scene. Initial Cube construction now prepares both caches before publishing Scene storage and returns `nil` if either Scene or snapshot preparation fails. A detached `const Scene&` snapshot helper performs checked all-object world-space geometry preflight, exact packing, and outliner creation while containing all C++ exceptions. Every reset, selection/delete/rename/add/TRS/edit mutation, `OCTOSCNE`/legacy load, and GLB import now mutates a detached candidate, prepares its full snapshot, and only then uses the Scene's statically asserted no-throw move assignment to replace the committed Scene and both caches. Failure leaves the committed Scene/caches and GLB/legacy metadata unchanged; diagnostics/legacy state changes only after a successful commit. Cached geometry/outliner getters no longer allocate or mutate error state, and project save/selected GLB export continue to read the committed Scene.

Primitive dispatch now selects a `const char *` before entering the protected candidate path, so `std::string` construction occurs only inside contained Scene work. The shared transform helper has a defensive null guard. Fallible project/GLB getters and all other public Objective-C++ methods were audited so C++ exceptions are caught at the boundary.

**Observed GREEN** for the focused static validator:

```console
[static] OK bridge owns one committed Scene plus coherent cached initial/updated snapshots
[static] OK public Objective-C++ boundaries contain fallible C++ work
[static] OK successful bridge commits synchronously publish cached geometry plus outliner
[static] PASS (portable/static validation only; no Xcode compile claim)
```

## Static Apple integration checked

The deterministic validator fails closed unless it finds:

- `_sceneStorage` and no `_meshStorage`, initial selected Cube, cached coherent triangle/outliner snapshot, stable selected ID, and Scene revision;
- detached candidate mutation/load/import followed by complete snapshot preparation before a no-throw Scene-plus-cache commit, with failure preserving committed state and metadata;
- two all-object `visitTriangles` passes in Scene storage order, `worldTransform()` application, checked aggregate triangle/vertex/byte arithmetic, Float32 representability checks, exact final `NSMutableData`, and no temporary Triangle vector or bridge-side fan triangulation;
- null-safe initialization/transform handling, protected primitive label construction, cached non-fallible getters, and C++ exception containment across public Objective-C++ boundaries;
- literal UI → view-model → Objective-C++ → portable Scene chains for selection/delete/rename, Cube/Plane/Tetrahedron/Cylinder/Cone/UV Sphere, translation/normalized-quaternion rotation/scale, and Loop/Knife/Inset/Merge/Extrude;
- complete `encodeSceneProject` save to `OctoPoly.octoscene`, exact magic dispatch to detached `OCTOSCNE` or legacy `OCTOPOLY` decode, and success-only publication;
- selected-object-only GLB export with visible no-selection error and detached GLB decode followed by new selected `Imported GLB` Scene object creation;
- `scene.cpp`/`scene.hpp` deterministic PBX references, Portable Core membership, and `scene.cpp` Sources membership;
- `@main` App → `ContentView` → `MeshViewModel` → Metal viewport/draw chain;
- success-only geometry plus outliner publication and explicit user-visible format/loss-policy labels.

This is static plausibility evidence, not an Apple compile.

## Final verification matrix

`./scripts/check.sh` rebuilt and ran all five suites with C++20 `-Wall -Wextra -Wpedantic -Werror`, then shell syntax and the deterministic static validator:

```console
octopoly_core_tests:         16 test(s), 0 failure(s)
project_codec_tests:         16 test(s), 0 failure(s)
mesh_allocation_fault_tests: 16 test(s), 0 failure(s)
glb_codec_tests:             14 test(s), 0 failure(s)
scene_tests:                 16 test(s), 0 failure(s)
[static] PASS (portable/static validation only; no Xcode compile claim)
[check] PASS
```

Total: **78 tests, 0 failures**. The same five suites were then independently rebuilt and executed three more times. Every final result is bound to the candidate above:

| Gate | Result | Candidate |
| --- | --- | --- |
| Canonical `./scripts/check.sh` | 78 tests, static validator, and shell syntax passed | `sha256:678178043ec974dbf6d0df2b2a492b79c0dbb34038d80b09d2d3ab9b5c28e785` |
| ASan (`detect_leaks=1`, `halt_on_error=1`) | 78 tests, 0 failures | `sha256:678178043ec974dbf6d0df2b2a492b79c0dbb34038d80b09d2d3ab9b5c28e785` |
| UBSan (`print_stacktrace=1`, `halt_on_error=1`) | 78 tests, 0 failures | `sha256:678178043ec974dbf6d0df2b2a492b79c0dbb34038d80b09d2d3ab9b5c28e785` |
| `_GLIBCXX_DEBUG` + `_GLIBCXX_DEBUG_PEDANTIC` + `-Wpedantic` | 78 tests, 0 failures | `sha256:678178043ec974dbf6d0df2b2a492b79c0dbb34038d80b09d2d3ab9b5c28e785` |
| Auxiliary syntax, whitespace, Markdown-link, and credential-pattern gates | Passed | `sha256:678178043ec974dbf6d0df2b2a492b79c0dbb34038d80b09d2d3ab9b5c28e785` |

No sanitizer or libstdc++ debug-container diagnostic was emitted. `cmake` was not installed on this WSL host, so the instrumented matrices used direct `g++` commands equivalent to the five CMake targets; `CMakeLists.txt` target/source/test retention was checked statically and the canonical non-CMake gate ran successfully.

Final auxiliary gates also passed:

- `git diff --check`;
- `python3 -m py_compile scripts/validate_project.py`;
- `bash -n scripts/check.sh scripts/mac/remote-build.sh scripts/mac/install-device.sh`;
- credential-pattern scan of changed tracked and untracked source/document files: no findings.

The Apple tiers remain **NOT RUN**: Xcode compile, simulator launch, signing, install, and device launch. This is a truthful platform limitation, not a static PASS promoted into an Apple-build claim.
