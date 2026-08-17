# OctoPoly for iPad

OctoPoly is an iPad-first polygon modelling experiment. Phase 1 establishes a portable C++20 mesh core and a thin SwiftUI/MetalKit application shell. The app starts with a cube, triangulates it for Metal rendering, and exposes live buttons for Loop Cut, Knife Cut, Inset, Merge, and Extrude.

## Phase 1 scope

- Stable vertex and face IDs, deterministic fan triangulation, validation, and revision tracking.
- Atomic single-face Loop Cut, Knife Cut, Inset, and Extrude operations plus vertex merge.
- A deliberately limited extrusion contract: translate one existing polygon by a finite nonzero vector, retain its face ID for the new cap, and add one side quad per edge. It does not propagate across adjacent faces.
- SwiftUI controls wired through an Objective-C++ bridge to the portable core.
- MetalKit triangle rendering backed by geometry refreshed after every successful operation.

Later phases are described in [ROADMAP.md](ROADMAP.md) and are not implemented here.

## Linux verification

```sh
./scripts/check.sh
```

The check uses `${CXX:-g++}` with C++20 and `-Wall -Wextra -Wpedantic -Werror`, runs all core tests, and executes deterministic project-structure/action-reachability checks. `CMakeLists.txt` is provided for CMake-capable hosts, but CMake is not required by the Linux check. This validates portable code and Xcode project structure; it is not an iPadOS build.

GitHub Actions용 Linux·macOS 구성은 `ci/github-actions/`에 템플릿으로 보관한다. 현재 GitHub OAuth 자격 증명에 `workflow` scope가 없어 활성 `.github/workflows/` 경로는 별도 권한 승인 전까지 만들지 않는다. 로컬 `scripts/check.sh`가 차수 완료 검사의 기준이다.

## Xcode

Open `app/OctoPolyIPad/OctoPolyIPad.xcodeproj` on a Mac with Xcode. The shared `OctoPolyIPad` scheme targets iPadOS 17 or newer. Choose a simulator for an unsigned build, or configure your own Apple team and unique bundle identifier for a device build.

No Xcode or device build was run during Linux Phase 1 verification. See [docs/verification/phase-1.md](docs/verification/phase-1.md).

## Remote Mac helpers

`scripts/mac/remote-build.sh` runs an unsigned simulator `xcodebuild` over an existing SSH setup. It requires:

- `OCTOPOLY_MAC_HOST`: SSH host name.
- `OCTOPOLY_MAC_REPO`: absolute path to an already-synchronised checkout on the Mac.
- `OCTOPOLY_MAC_USER` (optional): SSH user.
- `OCTOPOLY_XCODE_DESTINATION` (optional): defaults to `generic/platform=iOS Simulator`.

The script contains no credentials and relies on your SSH agent/configuration.

`scripts/mac/install-device.sh` runs `xcrun devicectl device install app` locally on a Mac. Set `OCTOPOLY_DEVICE_ID` and `OCTOPOLY_APP_PATH` to a paired device identifier and a signed `.app` path. Device installation requires a trusted, paired device with Developer Mode and valid signing. A free Personal Team development profile normally expires after 7 days, so the app must be rebuilt/reinstalled; this is personal testing, not App Store, TestFlight, or Ad Hoc distribution.

## Build infrastructure design

- [Windows → Mac remote Apple build architecture](docs/WINDOWS_MAC_REMOTE_BUILD_ARCHITECTURE.md) — proposed controller for the native Metal app and Unreal Engine 5.7 iOS/iPadOS builds. UE Cook and Metal shader compilation are Windows-only; UE signed profiles stay disabled until the documented signing-contract probe passes.
- [Free Personal Team wireless device installation](docs/FREE_PERSONAL_TEAM_WIRELESS_INSTALL.md) — documented Native path for initial cable pairing followed by Windows-over-SSH and Mac-over-network installation without paid Apple Developer Program enrollment; UE Personal Team signing remains gated by a project-specific probe.

These documents are implementation designs. The current Phase 1 helpers do not yet implement the full orchestrator.

## License

MIT — Copyright 2026 octopus7.
