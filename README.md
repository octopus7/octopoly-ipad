# OctoPoly for iPad

OctoPoly is an iPad-first polygon modelling experiment. Phases 1 and 2 provide a portable C++20 mesh core, a deterministic native project codec, and a thin SwiftUI/MetalKit application shell. The app starts with a cube, triangulates it for Metal rendering, and exposes live controls for Save, Load, Loop Cut, Knife Cut, Inset, Merge, and Extrude.

## Phase 1 scope

- Stable vertex and face IDs, deterministic fan triangulation, validation, and revision tracking. Stable vertex lookup uses a deterministic sorted derived index while preserving stored vertex order.
- Atomic single-face Loop Cut, Knife Cut, Inset, and Extrude operations plus vertex merge.
- A deliberately limited extrusion contract: translate one existing polygon by a finite nonzero vector, retain its face ID for the new cap, and add one side quad per edge. It does not propagate across adjacent faces.
- SwiftUI controls wired through an Objective-C++ bridge to the portable core.
- MetalKit triangle rendering backed by geometry refreshed after every successful operation. The Objective-C++ bridge streams the core fan-triangle visitation directly into its checked final `NSMutableData` buffer instead of materializing a temporary triangle vector.

## Phase 2 save and load

- Save encodes the complete mesh state (stable IDs, vertex positions, face loops, future-ID counters, and revision) into the versioned deterministic format documented in [docs/FORMAT.md](docs/FORMAT.md).
- Load rejects malformed, truncated, corrupt, unsupported, or over-limit data before replacing the live mesh. A failed codec install leaves the live mesh unchanged.
- The iPad shell stores one project at the app sandbox's Documents URL as `OctoPoly.octopoly`. The concrete on-device container path is assigned by iPadOS and must not be assumed to be stable.
- Foundation performs saving with `Data.write(to:options: .atomic)` and reading with `Data(contentsOf:)`; geometry refresh occurs only after a successful load.

Phases 3-10 are described in [ROADMAP.md](ROADMAP.md) and are not implemented here.

## Linux verification

```sh
./scripts/check.sh
```

The check uses `${CXX:-g++}` with C++20 and `-Wall -Wextra -Wpedantic -Werror`, runs all three portable suites (`octopoly_core_tests`, `project_codec_tests`, and `mesh_allocation_fault_tests`), and executes deterministic project-structure/action-reachability checks. `CMakeLists.txt` is provided for CMake-capable hosts, but CMake is not required by the Linux check. This validates portable code and Xcode project structure; it is not an iPadOS build.

GitHub Actions용 Linux·macOS 구성은 `ci/github-actions/`에 템플릿으로 보관한다. 현재 GitHub OAuth 자격 증명에 `workflow` scope가 없어 활성 `.github/workflows/` 경로는 별도 권한 승인 전까지 만들지 않는다. 로컬 `scripts/check.sh`가 차수 완료 검사의 기준이다.

## Xcode

Open `app/OctoPolyIPad/OctoPolyIPad.xcodeproj` on a Mac with Xcode. The shared `OctoPolyIPad` scheme targets iPadOS 17 or newer. Choose a simulator for an unsigned build, or configure your own Apple team and unique bundle identifier for a device build.

No Xcode or device build was run during Linux Phase 1 or Phase 2 verification. See [docs/verification/phase-1.md](docs/verification/phase-1.md) and [docs/verification/phase-2.md](docs/verification/phase-2.md).

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
