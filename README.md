# OctoPoly for iPad

OctoPoly is an iPad-first polygon modelling experiment. Phases 1-4 provide a portable C++20 mesh/scene core, deterministic native project and GLB codecs, and a SwiftUI/MetalKit application shell. The app owns a real `Scene`, starts with one selected Cube, renders every object in deterministic storage order with its world transform, and exposes an outliner, six primitives, object TRS controls, and retained selected-object mesh editing.

## Phase 1 scope

- Stable vertex and face IDs, deterministic fan triangulation, validation, and revision tracking. Stable vertex lookup uses a deterministic sorted derived index while preserving stored vertex order.
- Atomic single-face Loop Cut, Knife Cut, Inset, and Extrude operations plus vertex merge.
- A deliberately limited extrusion contract: translate one existing polygon by a finite nonzero vector, retain its face ID for the new cap, and add one side quad per edge. It does not propagate across adjacent faces.
- SwiftUI controls wired through an Objective-C++ bridge to the portable core.
- MetalKit triangle rendering backed by geometry refreshed after every successful operation. The Objective-C++ bridge streams the core fan-triangle visitation directly into its checked final `NSMutableData` buffer instead of materializing a temporary triangle vector.

## Phase 2 save and load

- Save encodes the complete mesh state (stable IDs, vertex positions, face loops, future-ID counters, and revision) into the versioned deterministic format documented in [docs/FORMAT.md](docs/FORMAT.md).
- Load rejects malformed, truncated, corrupt, unsupported, or over-limit data before replacing the live mesh. A failed codec install leaves the live mesh unchanged.
- The Phase 2 `OCTOPOLY` single-mesh format remains readable as an explicit legacy input. Phase 4 saves complete scenes instead of overwriting that legacy format ambiguously.
- Foundation performs saving with `Data.write(to:options: .atomic)` and reading with `Data(contentsOf:)`; geometry refresh occurs only after a successful load.

## Phase 3 GLB exchange

- Deterministic glTF 2.0 Binary export writes POSITION and indexed TRIANGLES with checked headers, chunks, padding, lengths, and position bounds.
- Strict bounded import supports the subset documented in [docs/GLB.md](docs/GLB.md), including uint8/uint16/uint32 indices, non-indexed triangles, offsets/strides, and multiple primitives.
- Unsupported transforms, skins, animations, morphs, extensions, external buffers, and non-triangle modes return typed errors. Ignored optional visual attributes/materials return visible diagnostics.
- Export/Import controls use Documents/`OctoPoly.glb`; failed imports leave the live mesh unchanged and successful imports alone refresh published geometry.

## Phase 4 scenes and iPad integration

- `Scene` owns stable-ID objects, complete local TRS, deterministic primitive meshes, and selected-object transactional wrappers for Loop Cut, Knife Cut, Inset, Merge, and Extrude.
- Imported/legacy meshes enter a scene through transactional `createMeshObject`; mesh stable IDs, counters, and revision are retained. Successful mesh edits advance the owned Mesh revision and Scene revision exactly once.
- The Objective-C++ bridge owns `Scene`, publishes stable outliner snapshots and selection, and streams every object's triangles through `Mesh::visitTriangles`. World-space positions are checked for finite Float32 representation before the final `NSData` allocation.
- Documents/`OctoPoly.octoscene` stores the complete canonical `OCTOSCNE` scene. Load detects `OCTOSCNE` versus legacy `OCTOPOLY` by magic and publishes only a fully decoded detached candidate. If the new file is absent, the Swift shell also checks the historical `OctoPoly.octopoly` path for migration.
- GLB export requires and exports only the selected object. GLB import creates a new selected object named `Imported GLB`; stable IDs/revisions and non-position visual data remain outside the GLB preservation contract, with diagnostics shown.
- The visible SwiftUI outliner supports stable-ID selection, rename, and delete. Reachable controls add Cube, Plane, Tetrahedron, Cylinder, Cone, and UV Sphere, and apply fixed translation, normalized-quaternion rotation, and uniform scale increments.

The complete portable and static Apple boundary is documented in [docs/SCENE.md](docs/SCENE.md) and [docs/verification/phase-4.md](docs/verification/phase-4.md). Phases 5-10 remain planned in [ROADMAP.md](ROADMAP.md).

## Linux verification

```sh
./scripts/check.sh
```

The check uses `${CXX:-g++}` with C++20 and `-Wall -Wextra -Wpedantic -Werror`, retains the four Phase 1-3 suites (`octopoly_core_tests`, `project_codec_tests`, `mesh_allocation_fault_tests`, and `glb_codec_tests`), adds `scene_tests`, and executes deterministic project-structure/action-reachability checks. `CMakeLists.txt` is provided for CMake-capable hosts, but CMake is not required by the Linux check. This validates portable code and Xcode project structure; it is not an iPadOS build.

GitHub Actions용 Linux·macOS 구성은 `ci/github-actions/`에 비활성 템플릿으로 보관한다. 활성화할 때는 사용하는 GitHub 자격 증명의 `workflow` 권한을 확인해야 한다. 로컬 `scripts/check.sh`가 차수 완료 검사의 기준이다.

## Xcode

Open `app/OctoPolyIPad/OctoPolyIPad.xcodeproj` on a Mac with Xcode. The shared `OctoPolyIPad` scheme targets iPadOS 17 or newer. Choose a simulator for an unsigned build, or configure your own Apple team and unique bundle identifier for a device build.

No Xcode or device build was run during Linux Phase 1-4 verification. Phase 4's Objective-C++/SwiftUI/PBX work is statically validated only; it is not an Apple compile claim. See [docs/verification/phase-1.md](docs/verification/phase-1.md), [docs/verification/phase-2.md](docs/verification/phase-2.md), [docs/verification/phase-3.md](docs/verification/phase-3.md), [docs/verification/phase-4-core.md](docs/verification/phase-4-core.md), and [docs/verification/phase-4.md](docs/verification/phase-4.md).

## Remote Mac helpers

`scripts/mac/remote-build.sh` runs an unsigned simulator `xcodebuild` over an existing SSH setup. It requires:

- `OCTOPOLY_MAC_HOST`: SSH host name.
- `OCTOPOLY_MAC_REPO`: absolute path to an already-synchronised checkout on the Mac.
- `OCTOPOLY_MAC_USER` (optional): SSH user.
- `OCTOPOLY_XCODE_DESTINATION` (optional): defaults to `generic/platform=iOS Simulator`.

The script contains no credentials and relies on your SSH agent/configuration.

`scripts/mac/install-device.sh` runs `xcrun devicectl device install app` locally on a Mac. Set `OCTOPOLY_DEVICE_ID` and `OCTOPOLY_APP_PATH` to a paired device identifier and a signed `.app` path. Device installation requires a trusted, paired device with Developer Mode and valid signing. A free Personal Team development profile normally expires after 7 days, so the app must be rebuilt/reinstalled; this is personal testing, not App Store, TestFlight, or Ad Hoc distribution.

## License

MIT — Copyright 2026 octopus7.
