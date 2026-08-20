# Phase 4 core verification — scene/object/primitives

Verification date: 2026-08-18

## Scope and completion boundary

This historical record covers only the portable C++20 Phase 4 foundation as it stood at the core-only gate: primitives, scene/object ownership, local/world TRS, deterministic lookup/index validation, scene persistence, malformed-input hardening, and transactional allocation behavior. At that gate Phase 4 was still **in progress**. No Objective-C++ bridge, SwiftUI outliner/control, Metal per-object rendering, Xcode build, simulator run, signing, or device install is claimed by this record.

## Observed RED/GREEN sequence

1. **Scene API RED:** after adding the first scene/primitive/TRS tests, the focused warning-clean compile failed exactly because `core/src/scene.cpp` and `octopoly/scene.hpp` did not exist:

   ```console
   cc1plus: fatal error: core/src/scene.cpp: No such file or directory
   tests/test_scene.cpp:1:10: fatal error: octopoly/scene.hpp: No such file or directory
   ```

   Minimal scene types, private-index ownership, primitive factories, TRS/matrix logic, validation, and detached mutations were then implemented. Focused GREEN was `5 test(s), 0 failure(s)`.

2. **Scene codec RED:** codec tests were added before the scene codec API. The focused compile failed with undeclared `encodeSceneProject`, `decodeSceneProject`, `installSceneProject`, `SceneLoadLimits`, and `SceneDecodeErrorCode`. The separate `OCTOSCNE` v1.0 envelope, nested unchanged Mesh v1.0 projects, strict preflight/decode, and atomic install were then implemented. Focused GREEN was `10 test(s), 0 failure(s)`.

3. **Allocation propagation RED:** scene allocation-ordinal sweeps were added to the existing fault executable. Thirteen paths passed, but decoded scene install failed because a nested Mesh decoder allocation error was incorrectly translated to `nestedMeshInvalid`:

   ```console
   FAIL scene allocation failures are typed and install is atomic:
   injected scene install failure is typed allocation failure
   14 test(s), 1 failure(s)
   ```

   The outer decoder now preserves nested `allocationFailed` as a scene allocation error. The same sweep was rerun GREEN: `14 test(s), 0 failure(s)`.

4. **Unknown primitive/preflight RED:** independent review showed that an unknown enum value fell through to UV-sphere generation; `rings=0` then underflowed before reserve. New tests first failed to compile because `SceneError::unsupportedPrimitive` did not exist. The explicit kind preflight and typed error were added. The same tests also require invalid tessellation to return `resourceLimit` before terminal-counter errors. Focused GREEN was `13 test(s), 0 failure(s)`.

5. **Hardening/refactor GREEN:** terminal object-cursor, fixed-seed mutation, every truncation/single-bit corruption, unknown enum values with zero/maximum parameters, and 512 reverse-ID lookup regressions leave the focused scene suite at `13 test(s), 0 failure(s)`.

## Contracts exercised

- exact primitive counts, sequential IDs, selected positions/loops/winding, min/max tessellation bounds;
- insertion order, stable object IDs, binary lookup, selection and selected-delete behavior;
- column-major `T*R*S` matrix and point golden values;
- strict UTF-8 names and finite normalized-quaternion/nonzero-scale transform rejection;
- revision and future-object-ID terminal states with canonical-byte atomicity;
- deterministic scene header/fields, repeated bytes, complete round trip, unusual high IDs, nested Mesh preservation;
- all truncation boundaries, every single-bit offset, fixed-seed mutations, semantic corruption, resource caps, invalid references, nested mesh errors;
- escaped `bad_alloc` atomicity for create/delete/rename/transform/copy assignment and typed allocation failure for scene install;
- large reverse-order object-ID resolution using exact lookup/result assertions rather than a wall-time threshold.

## Final portable verification

Final command outputs are recorded after the frozen-tree gate run. The portable targets are:

- `octopoly_core_tests`
- `project_codec_tests`
- `mesh_allocation_fault_tests`
- `glb_codec_tests`
- `scene_tests`

The legacy Mesh v1.0 golden bytes and all Phase 1-3 suites remain part of `./scripts/check.sh`.

## Apple integration excluded from this record

At the time of this core-only gate the application remained Mesh-based. Those changes are intentionally absent from this evidence snapshot. They were completed later at the static Apple boundary and are recorded separately in [phase-4.md](phase-4.md); that later record still does not claim a Mac/Xcode build.
