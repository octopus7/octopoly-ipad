# Phase 2 verification

Verification host: Linux under WSL, using the available `g++` C++20 toolchain. Xcode, the iPadOS SDK, an iPad simulator, and a physical iPad were not available. The Xcode project and Swift/Objective-C++ reachability are therefore covered only by deterministic static validation; no Apple compile, launch, signing, installation, or runtime save/load test is claimed.

## Recoverable TDD evidence

The following evidence was recovered from the append-only implementation transcript and existing test/build artifacts. Only observed RED and GREEN runs are listed.

### Deterministic encoder slice

The first codec test was created before the codec header. The append-only transcript retained the compiler output below, but abbreviated the tool call itself, so no unrecoverable command line is asserted:

```console
tests/test_project_codec.cpp:2:10: fatal error: octopoly/project_codec.hpp: No such file or directory
 2 | #include "octopoly/project_codec.hpp"
 |          ^~~~~~~~~~~~~~~~~~~~~~~~~~~~
compilation terminated.
```

After the first minimal encoder implementation, the observed focused deterministic/golden-header output was:

```console
PASS default_cube_encoding_is_deterministic_with_golden_header
1 test(s), 0 failure(s)
```

### Decode and round-trip slice

The round-trip test was added before `decodeProject` existed. The observed compile included:

```console
tests/test_project_codec.cpp:17:26: error: ‘decodeProject’ has not been declared in ‘octopoly::project’
 17 | using octopoly::project::decodeProject;
      |                          ^~~~~~~~~~~~~
```

After adding the detached decoder and validation path, the focused test and then-current two-test suite passed:

```console
PASS round_trip_preserves_default_and_edited_state_and_future_ids
1 test(s), 0 failure(s)
PASS default_cube_encoding_is_deterministic_with_golden_header
PASS round_trip_preserves_default_and_edited_state_and_future_ids
2 test(s), 0 failure(s)
```

### Atomic install slice

The atomic install test was added before `installProject` existed. The observed compile included:

```console
tests/test_project_codec.cpp:19:26: error: ‘installProject’ has not been declared in ‘octopoly::project’
 19 | using octopoly::project::installProject;
      |                          ^~~~~~~~~~~~~~
```

After adding decode-then-single-move installation, the focused test and then-current three-test suite passed:

```console
PASS failed_install_is_atomic_and_success_replaces_complete_state
1 test(s), 0 failure(s)
PASS default_cube_encoding_is_deterministic_with_golden_header
PASS round_trip_preserves_default_and_edited_state_and_future_ids
PASS failed_install_is_atomic_and_success_replaces_complete_state
3 test(s), 0 failure(s)
```

### Hardening coverage

The five hardening tests for every truncation/single-bit corruption, typed malformed-header/checksum errors, CRC-recomputed structural invalidity, injected resource limits, and a fixed-seed 2,000-input mutation corpus were added after the decoder path already existed. Their first recoverable focused run passed. They are regression/hardening coverage and are **not** claimed as independently observed RED tests.

### Stable-ID exhaustion review blocker

The focused decoded/installed-fixture tests were added before the typed operation error or checked allocation capacity existed. The exact warning-clean compile command and observed RED were:

```console
$ g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -Icore/include core/src/mesh.cpp core/src/project_codec.cpp tests/test_project_codec.cpp -o build/check/project_codec_tests
tests/test_project_codec.cpp: In function ‘void {anonymous}::require_id_exhausted_atomic(const std::string&, octopoly::VertexId, octopoly::FaceId, const std::function<octopoly::OperationResult(octopoly::Mesh&)>&)’:
tests/test_project_codec.cpp:374:58: error: ‘idExhausted’ is not a member of ‘octopoly::OperationError’
  374 |     require_equal(result.code, octopoly::OperationError::idExhausted,
      |                                                          ^~~~~~~~~~~
tests/test_project_codec.cpp: In function ‘void {anonymous}::boundary_allocation_leaves_valid_maximum_next_ids()’:
tests/test_project_codec.cpp:431:58: error: ‘idExhausted’ is not a member of ‘octopoly::OperationError’
  431 |     require_equal(failed.code, octopoly::OperationError::idExhausted,
      |                                                          ^~~~~~~~~~~
```

After adding `OperationError::idExhausted`, pre-mutation capacity checks, and future-counter validation, the observed focused GREEN was:

```console
$ g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -Icore/include core/src/mesh.cpp core/src/project_codec.cpp tests/test_project_codec.cpp -o build/check/project_codec_tests && ./build/check/project_codec_tests allocation_operations_reject_id_exhaustion_atomically_after_install && ./build/check/project_codec_tests boundary_allocation_leaves_valid_maximum_next_ids
PASS allocation_operations_reject_id_exhaustion_atomically_after_install
1 test(s), 0 failure(s)
PASS boundary_allocation_leaves_valid_maximum_next_ids
1 test(s), 0 failure(s)
```

The unusual/high-ID sorting regression was behavior-preserving and passed before and after the container refactor; it is not presented as RED evidence. It checks exact byte round-trip plus typed duplicate vertex, duplicate face, duplicate per-face loop, and missing-reference errors at their offending byte offsets.

### Persisted revision terminal-value blocker

Checksum-valid decode/install fixtures and per-operation terminal-revision assertions were added before `OperationError::revisionExhausted` existed. The observed focused RED was:

```console
$ g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -Icore/include core/src/mesh.cpp core/src/project_codec.cpp tests/test_project_codec.cpp -o build/check/project_codec_tests
tests/test_project_codec.cpp: In function ‘void {anonymous}::terminal_revision_rejects_every_mutation_atomically_after_install()’:
tests/test_project_codec.cpp:470:62: error: ‘revisionExhausted’ is not a member of ‘octopoly::OperationError’
  470 |         require_equal(result.code, octopoly::OperationError::revisionExhausted,
      |                                                              ^~~~~~~~~~~~~~~~~
tests/test_project_codec.cpp: In function ‘void {anonymous}::penultimate_revision_advances_to_valid_deterministic_terminal_state()’:
tests/test_project_codec.cpp:513:58: error: ‘revisionExhausted’ is not a member of ‘octopoly::OperationError’
  513 |     require_equal(failed.code, octopoly::OperationError::revisionExhausted,
      |                                                          ^~~~~~~~~~~~~~~~~
```

After adding the typed terminal revision check before candidate creation/mutation, the observed focused GREEN was:

```console
$ g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -Icore/include core/src/mesh.cpp core/src/project_codec.cpp tests/test_project_codec.cpp -o build/check/project_codec_tests && ./build/check/project_codec_tests terminal_revision_rejects_every_mutation_atomically_after_install && ./build/check/project_codec_tests penultimate_revision_advances_to_valid_deterministic_terminal_state
PASS terminal_revision_rejects_every_mutation_atomically_after_install
1 test(s), 0 failure(s)
PASS penultimate_revision_advances_to_valid_deterministic_terminal_state
1 test(s), 0 failure(s)
```

The GREEN revision test covers loop cut, direct knife cut, inset, extrude, and merge independently at both `UINT64_MAX - 1` and `UINT64_MAX`. It compares vertices, faces, both future-ID cursors, revision, and exact canonical bytes after each rejected terminal edit, and verifies deterministic encode/decode at the terminal value.

### Post-commit allocation-exception blocker

The dedicated allocation-fault test executable was written before changing operation commit order. It injects `std::bad_alloc` at successive global allocation ordinals, with ordinary, array, and aligned allocation/deallocation replacements confined to that executable. Against the old post-commit result construction, the observed RED was:

```console
$ g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -Icore/include core/src/mesh.cpp core/src/project_codec.cpp tests/test_mesh_allocation_faults.cpp -o build/check/mesh_allocation_fault_tests && ./build/check/mesh_allocation_fault_tests
FAIL loop cut allocation failures are atomic: loop cut escaped bad_alloc must preserve complete state (vertices)
FAIL knife cut allocation failures are atomic: knife cut escaped bad_alloc must preserve complete state (vertices)
FAIL inset allocation failures are atomic: inset escaped bad_alloc must preserve complete state (vertices)
FAIL extrude allocation failures are atomic: extrude escaped bad_alloc must preserve complete state (vertices)
FAIL merge allocation failures are atomic: merge escaped bad_alloc must preserve complete state (vertices)
5 test(s), 5 failure(s)
```

After fully constructing each successful `OperationResult`, moving its ID vectors, and only then calling the `noexcept` commit helper, the observed focused GREEN was:

```console
$ g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -Icore/include core/src/mesh.cpp core/src/project_codec.cpp tests/test_mesh_allocation_faults.cpp -o build/check/mesh_allocation_fault_tests && ./build/check/mesh_allocation_fault_tests
PASS loop cut allocation failures are atomic
PASS knife cut allocation failures are atomic
PASS inset allocation failures are atomic
PASS extrude allocation failures are atomic
PASS merge allocation failures are atomic
5 test(s), 0 failure(s)
```

Every operation observed at least one escaping injected allocation failure, preserved complete state and exact canonical bytes for every such failure, and eventually succeeded at the first ordinal beyond its allocation count.

## Final verification

The verification evidence below includes the initial Phase 2 integration runs, the first blocker reruns, and the latest auto-fix cycle 2 reruns. The headings distinguish those observed runs; the newest gate and diagnostic results appear under **Auto-fix cycle 2 final reruns**.

### Repository gate

```console
$ ./scripts/check.sh
[check] compiler: g++ (Ubuntu 15.2.0-16ubuntu1) 15.2.0
[check] direct C++20 warning-clean build
[check] mesh tests
PASS default_cube_is_valid_and_stable
PASS triangulation_is_deterministic_and_references_mesh_vertices
PASS loop_cut_splits_a_quad_at_opposite_edge_midpoints
PASS loop_cut_propagates_edge_points_to_adjacent_cube_faces
PASS invalid_loop_cut_is_atomic_and_reports_not_found
PASS knife_cut_splits_between_two_nonadjacent_edges
PASS knife_cut_propagates_edge_points_to_adjacent_cube_faces
PASS invalid_knife_cut_is_atomic_including_revision
PASS inset_face_creates_an_inner_face_and_side_ring
PASS extrude_face_offsets_a_new_cap_and_creates_a_side_ring
PASS invalid_extrude_is_atomic_including_revision
PASS invalid_inset_is_atomic_including_revision
PASS merge_vertices_keeps_target_id_and_repairs_faces
PASS invalid_merge_ids_are_atomic
PASS successful_operations_preserve_validation_and_advance_one_revision
15 test(s), 0 failure(s)
[check] project codec tests
PASS default_cube_encoding_is_deterministic_with_golden_header
PASS round_trip_preserves_default_and_edited_state_and_future_ids
PASS every_truncation_and_single_bit_corruption_is_rejected
PASS malformed_headers_and_checksum_have_precise_typed_errors
PASS crc_recomputed_structural_invalidity_is_rejected
PASS sorted_id_validation_preserves_unusual_ids_and_precise_errors
PASS injected_resource_limits_reject_before_allocation
PASS fixed_seed_mutations_never_crash_and_always_return_a_typed_result
PASS failed_install_is_atomic_and_success_replaces_complete_state
PASS allocation_operations_reject_id_exhaustion_atomically_after_install
PASS boundary_allocation_leaves_valid_maximum_next_ids
PASS merge_succeeds_with_terminal_next_ids_without_allocating
12 test(s), 0 failure(s)
[check] shell syntax
[check] deterministic Xcode/bridge/UI static validation
[static] OK required files (29)
[static] OK MIT license and required roadmap phases 1-10
[static] OK CMake core, mesh tests, codec tests, and portable warning gates
[static] OK check.sh warning-clean mesh/codec build and execution wiring
[static] OK pbxproj IDs, codec paths/group/build phase, source references (13), and build settings
[static] OK UI -> bridge -> core action reachability (5 actions)
[static] OK Save/Load UI -> Swift atomic Documents I/O -> bridge -> codec reachability
[static] OK implemented wire-format and atomicity documentation markers
[static] OK cube -> triangulation -> bridge -> Metal triangle render path
[static] OK remote build, device install, and unsigned simulator CI template wiring
[static] PASS
[check] PASS
```

### AddressSanitizer and UndefinedBehaviorSanitizer

Mesh suite:

```console
$ mkdir -p build/sanitizers-phase2 && g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -Icore/include core/src/mesh.cpp tests/test_mesh.cpp -o build/sanitizers-phase2/octopoly_core_tests && ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 ./build/sanitizers-phase2/octopoly_core_tests
PASS default_cube_is_valid_and_stable
PASS triangulation_is_deterministic_and_references_mesh_vertices
PASS loop_cut_splits_a_quad_at_opposite_edge_midpoints
PASS loop_cut_propagates_edge_points_to_adjacent_cube_faces
PASS invalid_loop_cut_is_atomic_and_reports_not_found
PASS knife_cut_splits_between_two_nonadjacent_edges
PASS knife_cut_propagates_edge_points_to_adjacent_cube_faces
PASS invalid_knife_cut_is_atomic_including_revision
PASS inset_face_creates_an_inner_face_and_side_ring
PASS extrude_face_offsets_a_new_cap_and_creates_a_side_ring
PASS invalid_extrude_is_atomic_including_revision
PASS invalid_inset_is_atomic_including_revision
PASS merge_vertices_keeps_target_id_and_repairs_faces
PASS invalid_merge_ids_are_atomic
PASS successful_operations_preserve_validation_and_advance_one_revision
15 test(s), 0 failure(s)
```

Codec suite:

```console
$ mkdir -p build/sanitizers-phase2 && g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -Icore/include core/src/mesh.cpp core/src/project_codec.cpp tests/test_project_codec.cpp -o build/sanitizers-phase2/project_codec_tests && ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 ./build/sanitizers-phase2/project_codec_tests
PASS default_cube_encoding_is_deterministic_with_golden_header
PASS round_trip_preserves_default_and_edited_state_and_future_ids
PASS every_truncation_and_single_bit_corruption_is_rejected
PASS malformed_headers_and_checksum_have_precise_typed_errors
PASS crc_recomputed_structural_invalidity_is_rejected
PASS injected_resource_limits_reject_before_allocation
PASS fixed_seed_mutations_never_crash_and_always_return_a_typed_result
PASS failed_install_is_atomic_and_success_replaces_complete_state
8 test(s), 0 failure(s)
```

Both sanitizer processes exited 0 and printed no sanitizer diagnostics.

### Review-blocker ASan, UBSan, and libstdc++ debug reruns

After the deterministic-ID and exhaustion fixes, both the 15-test mesh suite and the 12-test codec suite were rebuilt and run independently under AddressSanitizer, UndefinedBehaviorSanitizer, and libstdc++ debug iterators. The compiler flag sets and exact final result lines were:

```console
$ g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -fsanitize=address -fno-omit-frame-pointer ...
$ ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./build/asan-phase2-review/octopoly_core_tests
15 test(s), 0 failure(s)
$ ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./build/asan-phase2-review/project_codec_tests
12 test(s), 0 failure(s)

$ g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -fsanitize=undefined -fno-omit-frame-pointer ...
$ UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 ./build/ubsan-phase2-review/octopoly_core_tests
15 test(s), 0 failure(s)
$ UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 ./build/ubsan-phase2-review/project_codec_tests
12 test(s), 0 failure(s)

$ g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -D_GLIBCXX_DEBUG -D_GLIBCXX_DEBUG_PEDANTIC ...
$ ./build/glibcxx-debug-phase2-review/octopoly_core_tests
15 test(s), 0 failure(s)
$ ./build/glibcxx-debug-phase2-review/project_codec_tests
12 test(s), 0 failure(s)
```

All six processes exited 0. ASan and UBSan printed no sanitizer diagnostics, and the libstdc++ debug runs printed no iterator/container assertions.

### Auto-fix cycle 2 final reruns

After the revision-overflow and post-commit-allocation fixes, the repository gate rebuilt and ran all three warning-clean C++20 executables. The observed result summary was:

```console
$ ./scripts/check.sh
[check] mesh tests
15 test(s), 0 failure(s)
[check] project codec tests
14 test(s), 0 failure(s)
[check] mesh allocation-fault tests
PASS loop cut allocation failures are atomic
PASS knife cut allocation failures are atomic
PASS inset allocation failures are atomic
PASS extrude allocation failures are atomic
PASS merge allocation failures are atomic
5 test(s), 0 failure(s)
[static] OK required files (30)
[static] OK check.sh warning-clean mesh/codec/fault build and execution wiring
[static] PASS
[check] PASS
```

All three executables were then rebuilt and run independently with each of these flag sets:

```console
-fsanitize=address -fno-omit-frame-pointer
-fsanitize=undefined -fno-omit-frame-pointer
-D_GLIBCXX_DEBUG -D_GLIBCXX_DEBUG_PEDANTIC
```

The observed result totals for **each** of ASan, UBSan, and `_GLIBCXX_DEBUG`/pedantic were:

```console
octopoly_core_tests:          15 test(s), 0 failure(s)
project_codec_tests:          14 test(s), 0 failure(s)
mesh_allocation_fault_tests:   5 test(s), 0 failure(s)
```

All nine processes exited 0. ASan (with leak detection) and UBSan (with stack traces and halt-on-error) printed no sanitizer diagnostics; libstdc++ debug/pedantic printed no iterator/container assertions. The dedicated allocation replacements therefore also completed cleanly under all three diagnostic configurations.

### Bounded indexed-lookup and streaming-render resolution

The new streaming API test was added before `Mesh::visitTriangles` existed. The observed warning-clean compile RED was:

```console
$ g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -Icore/include core/src/mesh.cpp tests/test_mesh.cpp -o build/check/octopoly_core_tests
tests/test_mesh.cpp: In function ‘void {anonymous}::streaming_triangulation_matches_materialized_sequence()’:
tests/test_mesh.cpp:102:14: error: ‘const class octopoly::Mesh’ has no member named ‘visitTriangles’
  102 |         mesh.visitTriangles([&visited](const octopoly::Triangle& triangle) {
      |              ^~~~~~~~~~~~~~
```

After `triangulate()` was changed to collect the new allocation-free visitation path, the observed focused GREEN was:

```console
PASS streaming_triangulation_matches_materialized_sequence
1 test(s), 0 failure(s)
```

The unsorted/high-ID fixture initially passed as characterization coverage with the old linear lookup. After the core switched to the private derived index but before codec candidate construction rebuilt it, the required intermediate RED proved the codec construction gap:

```console
FAIL decoded_unsorted_high_ids_resolve_positions_and_round_trip: unsorted high-ID lookup fixture must decode
1 test(s), 1 failure(s)
```

Removing the incorrect `noexcept` from the allocating friend constructor and rebuilding the index there produced the observed focused GREEN. The 30,000-vertex reverse-storage-order fixture then also passed, checking 30,000 worst-order direct resolutions, exactly 29,998 streamed fan triangles, exactly 89,994 referenced positions, exact IDs, and exact positions:

```console
PASS decoded_unsorted_high_ids_resolve_positions_and_round_trip
1 test(s), 0 failure(s)
PASS large_reverse_id_lookup_and_streaming_traversal_is_exact
1 test(s), 0 failure(s)
```

The large regression is structural rather than a wall-clock threshold: ascending stable IDs map to descending storage positions. A prior linear lookup therefore requires billions of vertex comparisons across its direct and streamed traversals, while the derived sorted index bounds each resolution to O(log V). The persistent index is O(V), remains nonserialized, and preserves vertex record order and exact round-trip bytes. Rendering visits triangles twice with O(1) traversal state (checked count, then direct packing) and retains only the required final `NSMutableData`; it no longer creates an additional O(T) `std::vector<Triangle>`.

The static render marker was also changed before the bridge implementation. Its observed RED was `render path missing allocation-free core triangle visitation: mesh.visitTriangles(`. After the bridge streamed through the core API and added checked `size_t`/`NSUInteger` capacity arithmetic, the validator reported:

```console
[static] OK cube -> streamed core triangulation -> checked bridge buffer -> Metal render path
[static] PASS
```

The allocation-fault suite was extended with a decode/install sweep. It reaches both parsed-ID-sized and derived-index-sized allocations, requires the existing typed codec allocation error, and compares complete live state plus canonical bytes after every injected failure. The five prior edit-operation sweeps remain unchanged and continue to verify every escaping `std::bad_alloc` before eventual successful commit.

A later full-gate run exposed an intermediate stale-index use inside vertex-adding edits: after the first append, fail-closed `vertex()` correctly rejected the candidate's now-incomplete index, and UBSan identified the resulting null dereference at `core/src/mesh.cpp:145`. The existing streaming edited-mesh test reproduced it. Knife, inset, and extrude now resolve all source positions before the first vertex append, then rebuild once after additions; the focused test and a combined ASan/UBSan mesh run passed afterward.

### Transactional copy-assignment remediation

An independent allocation-ordinal probe found that defaulted memberwise `Mesh` copy assignment could expose a partial destination if copying one of the correlated topology/index vectors threw. The dedicated fault suite was extended before the implementation change. Its observed RED was:

```console
FAIL copy assignment allocation failures are atomic: copy-assignment bad_alloc must preserve destination state (vertices)
7 test(s), 1 failure(s)
```

Copy assignment now copy-constructs a complete temporary and commits through the existing non-throwing move assignment. The focused GREEN was:

```console
PASS copy assignment allocation failures are atomic
7 test(s), 0 failure(s)
```

The observed warning-clean repository gate after this resolution was:

```console
$ ./scripts/check.sh
[check] mesh tests
16 test(s), 0 failure(s)
[check] project codec tests
16 test(s), 0 failure(s)
[check] mesh allocation-fault tests
7 test(s), 0 failure(s)
[static] OK cube -> streamed core triangulation -> checked bridge buffer -> Metal render path
[static] PASS
[check] PASS
```

This preserves all prior 15 mesh, 14 codec, and 5 allocation-fault tests and adds one mesh, two codec, and two allocation-fault regressions.

All three executables were then rebuilt independently under each required diagnostic configuration:

```console
-fsanitize=address -fno-omit-frame-pointer
-fsanitize=undefined -fno-omit-frame-pointer
-D_GLIBCXX_DEBUG -D_GLIBCXX_DEBUG_PEDANTIC
```

The observed totals for **each** of ASan, UBSan, and libstdc++ debug/pedantic were:

```console
octopoly_core_tests:          16 test(s), 0 failure(s)
project_codec_tests:          16 test(s), 0 failure(s)
mesh_allocation_fault_tests:   7 test(s), 0 failure(s)
```

All nine final diagnostic processes exited 0. ASan used leak detection and halt-on-error, UBSan used stack traces and halt-on-error, and none printed sanitizer or debug-container diagnostics.

### Earlier whitespace and secret scan

```console
$ git diff --check
```

The command exited 0 with no output.

A repository-local high-confidence scan read every text path returned by `git ls-files --cached --others --exclude-standard` and checked for private-key PEM headers, AWS access-key IDs, GitHub tokens, Slack tokens, and quoted long generic secret assignments. Its exact result was:

```console
[secret-scan] scanned 31 text files with 5 high-confidence patterns
[secret-scan] PASS no candidate secrets found
```

## Limitations

- `cmake` and `ctest` were unavailable on this host, so the completed CMake target graph received static validation but was not configured or executed here.
- `xcodebuild` was unavailable. No Xcode project parse/build, Swift compile, Objective-C++ compile under Apple Clang, iPad simulator run, code signing, `devicectl` installation, or physical-device launch was performed.
- The Apple Save/Load UI and Foundation Documents I/O path is statically traced from controls through Swift, Objective-C++, and the portable codec. Its runtime behavior still requires verification on a Mac/iPadOS environment.
