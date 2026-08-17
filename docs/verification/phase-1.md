# Phase 1 verification

Verification host: Linux under WSL. The portable core was compiled with the available `g++` toolchain. Xcode, an iPad simulator, and a physical device were **not** available or executed; the Xcode shell is covered only by deterministic static structure/reachability checks on this host.

## TDD RED evidence

All requested test cases were written before the final extrusion and merge fixes. The first compile failed because the wished-for API did not exist:

```console
$ g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -Icore/include core/src/mesh.cpp tests/test_mesh.cpp -o build/octopoly_core_tests
tests/test_mesh.cpp: In function ‘void {anonymous}::extrude_face_offsets_a_new_cap_and_creates_a_side_ring()’:
tests/test_mesh.cpp:124:30: error: ‘class octopoly::Mesh’ has no member named ‘extrudeFace’
  124 |     const auto result = mesh.extrudeFace(target, {0.0, 0.0, -2.0});
      |                              ^~~~~~~~~~~
tests/test_mesh.cpp: In function ‘void {anonymous}::invalid_extrude_is_atomic_including_revision()’:
tests/test_mesh.cpp:155:34: error: ‘class octopoly::Mesh’ has no member named ‘extrudeFace’
  155 |         const auto result = mesh.extrudeFace(faceId, offset);
      |                                  ^~~~~~~~~~~
tests/test_mesh.cpp: In lambda function:
tests/test_mesh.cpp:236:21: error: ‘class octopoly::Mesh’ has no member named ‘extrudeFace’
  236 |         return mesh.extrudeFace(mesh.faces().front().id, {0.0, 0.0, -1.0});
      |                     ^~~~~~~~~~~
```

A minimal `unsupported` extrusion stub was then added only to make the tests executable. The focused behavioral run produced these RED results:

```console
$ ./build/octopoly_core_tests extrude_face_offsets_a_new_cap_and_creates_a_side_ring
FAIL extrude_face_offsets_a_new_cap_and_creates_a_side_ring: extrude is not implemented
1 test(s), 1 failure(s)

$ ./build/octopoly_core_tests invalid_extrude_is_atomic_including_revision
FAIL invalid_extrude_is_atomic_including_revision: invalid extrusion reports a typed error
1 test(s), 1 failure(s)

$ ./build/octopoly_core_tests invalid_inset_is_atomic_including_revision
PASS invalid_inset_is_atomic_including_revision
1 test(s), 0 failure(s)

$ ./build/octopoly_core_tests invalid_merge_ids_are_atomic
FAIL invalid_merge_ids_are_atomic: same-vertex merge reports invalid argument
1 test(s), 1 failure(s)

$ ./build/octopoly_core_tests successful_operations_preserve_validation_and_advance_one_revision
FAIL successful_operations_preserve_validation_and_advance_one_revision: extrude must succeed: extrude is not implemented
1 test(s), 1 failure(s)
```

The invalid-inset test and the nonexistent-ID portion of merge were characterization coverage for already-correct preserved code, so they did not independently fail. This is recorded rather than mislabelled as RED. The same-ID merge, extrusion behavior/error typing, and extrusion-backed revision invariant did fail for the intended reasons.

## GREEN evidence

After implementing failure-atomic `extrudeFace` and rejecting same-ID merge as `invalidArgument`, each focused test and the suite passed:

```console
$ ./build/octopoly_core_tests extrude_face_offsets_a_new_cap_and_creates_a_side_ring
PASS extrude_face_offsets_a_new_cap_and_creates_a_side_ring
1 test(s), 0 failure(s)

$ ./build/octopoly_core_tests invalid_extrude_is_atomic_including_revision
PASS invalid_extrude_is_atomic_including_revision
1 test(s), 0 failure(s)

$ ./build/octopoly_core_tests invalid_inset_is_atomic_including_revision
PASS invalid_inset_is_atomic_including_revision
1 test(s), 0 failure(s)

$ ./build/octopoly_core_tests invalid_merge_ids_are_atomic
PASS invalid_merge_ids_are_atomic
1 test(s), 0 failure(s)

$ ./build/octopoly_core_tests successful_operations_preserve_validation_and_advance_one_revision
PASS successful_operations_preserve_validation_and_advance_one_revision
1 test(s), 0 failure(s)

$ ./build/octopoly_core_tests
PASS default_cube_is_valid_and_stable
PASS triangulation_is_deterministic_and_references_mesh_vertices
PASS loop_cut_splits_a_quad_at_opposite_edge_midpoints
PASS invalid_loop_cut_is_atomic_and_reports_not_found
PASS knife_cut_splits_between_two_nonadjacent_edges
PASS invalid_knife_cut_is_atomic_including_revision
PASS inset_face_creates_an_inner_face_and_side_ring
PASS extrude_face_offsets_a_new_cap_and_creates_a_side_ring
PASS invalid_extrude_is_atomic_including_revision
PASS invalid_inset_is_atomic_including_revision
PASS merge_vertices_keeps_target_id_and_repairs_faces
PASS invalid_merge_ids_are_atomic
PASS successful_operations_preserve_validation_and_advance_one_revision
13 test(s), 0 failure(s)
```

## Reviewer-remediation TDD evidence

The closed-cube cut conformity regression was added before the implementation change. Both focused tests failed because `affectedFaces` still contained only the selected and newly created regions; the neighboring cube faces had not received the shared cut vertex IDs:

```console
$ g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -Icore/include core/src/mesh.cpp tests/test_mesh.cpp -o build/reviewer-tdd/octopoly_core_tests
$ ./build/reviewer-tdd/octopoly_core_tests knife_cut_propagates_edge_points_to_adjacent_cube_faces
FAIL knife_cut_propagates_edge_points_to_adjacent_cube_faces: knife cut reports every changed face once in deterministic order
1 test(s), 1 failure(s)
$ ./build/reviewer-tdd/octopoly_core_tests loop_cut_propagates_edge_points_to_adjacent_cube_faces
FAIL loop_cut_propagates_edge_points_to_adjacent_cube_faces: loop cut reports every changed face once in deterministic order
1 test(s), 1 failure(s)
```

After propagating each new edge point through all other faces sharing that cyclic edge, both focused tests passed. They assert exact face loops, exact deterministic affected IDs `{1, 5, 6, 7}`, and two-face incidence for every edge of the cut cube:

```console
$ ./build/reviewer-tdd/octopoly_core_tests knife_cut_propagates_edge_points_to_adjacent_cube_faces
PASS knife_cut_propagates_edge_points_to_adjacent_cube_faces
1 test(s), 0 failure(s)
$ ./build/reviewer-tdd/octopoly_core_tests loop_cut_propagates_edge_points_to_adjacent_cube_faces
PASS loop_cut_propagates_edge_points_to_adjacent_cube_faces
1 test(s), 0 failure(s)
```

The adjacent-cube-vertex merge assertion was then added before its implementation change and failed because target-only face 5 was omitted:

```console
$ ./build/reviewer-tdd/octopoly_core_tests merge_vertices_keeps_target_id_and_repairs_faces
FAIL merge_vertices_keeps_target_id_and_repairs_faces: merge reports all faces moved or repaired in deterministic order
1 test(s), 1 failure(s)
```

After collecting every face that contained either vertex before mutation, the exact expected affected sequence `{1, 3, 5, 6}` passed, followed by the complete suite:

```console
$ ./build/reviewer-tdd/octopoly_core_tests merge_vertices_keeps_target_id_and_repairs_faces
PASS merge_vertices_keeps_target_id_and_repairs_faces
1 test(s), 0 failure(s)
$ ./build/reviewer-tdd/octopoly_core_tests
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

## Final Phase 1 command

`cmake` was not installed on this Linux host, so `CMakeLists.txt` received deterministic target/reference validation but was not configured or built. The portable code was compiled directly with the available `g++`, as required. Final output after the completed scaffold:

```console
$ ./scripts/check.sh
[check] compiler: g++ (Ubuntu 15.2.0-16ubuntu1) 15.2.0
[check] direct C++20 warning-clean build
[check] core tests
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
[check] shell syntax
[check] deterministic Xcode/bridge/UI static validation
[static] OK required files (23)
[static] OK MIT license and required roadmap phases 1-10
[static] OK CMake portable core and test targets
[static] OK pbxproj source references (11) and build settings
[static] OK UI -> bridge -> core action reachability (5 actions)
[static] OK cube -> triangulation -> bridge -> Metal triangle render path
[static] OK remote build, device install, and unsigned simulator CI template wiring
[static] PASS
[check] PASS
```

The direct AddressSanitizer/UndefinedBehaviorSanitizer build and complete test run also passed with no sanitizer diagnostics:

```console
$ mkdir -p build/sanitizers && g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -Icore/include core/src/mesh.cpp tests/test_mesh.cpp -o build/sanitizers/octopoly_core_tests && ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1 ./build/sanitizers/octopoly_core_tests
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

This is static Linux verification only. No `xcodebuild`, simulator launch, code signing, `devicectl` installation, or physical-device launch was executed.
