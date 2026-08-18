# Phase 3 검증 — GLB import/export

검증일: 2026-08-18

## 범위

- stdlib-only C++20 GLB v2 encode/decode/install
- strict bounded JSON 및 GLB 청크 파싱
- indexed/non-indexed TRIANGLES와 interleaved accessor
- 실패 원자성 및 결정적 diagnostic
- Objective-C++ bridge와 SwiftUI Documents I/O 연결
- Xcode project/CMake/Linux gate 정적 wiring

## TDD 기록

GLB 전용 테스트를 먼저 추가했을 때 `octopoly/glb_codec.hpp`와 구현이 존재하지 않아 컴파일할 수 없는 RED 상태를 확인했다. 이후 컨테이너 export, parser, accessor 해석, detached install을 수직으로 구현했다.

초기 GREEN 이후 UTF-8 검증과 POSITION accessor 최초 사용 순서 결정을 강화했다. 독립 보안 리뷰에서는 객체 중복 키 O(n²), 부동소수점 반올림을 통한 정수 schema 우회, `asset.minVersion`/여러 위치의 extension payload 승인, malformed ignored attribute 승인, 전체 정점 수 기반 index 폭 선택을 probe로 재현했다. 각각 테스트를 먼저 추가해 RED를 확인한 뒤 다음처럼 수정했다.

- decoded key 정렬·인접 비교로 최악 O(n log n), 객체당 100,000 member cap
- 원본 number lexeme 기반 checked 비음수 10진 정수 검증
- 2.0 초과 `minVersion` 및 모든 semantic object의 extension payload fail-closed 거부
- ignored attribute도 유효 accessor index인지 선검증
- 실제 최대 emitted storage index로 index component 폭 결정

40,000개 고유 root key를 가진 4 MiB 미만 회귀 입력은 최적화 빌드에서 0.13초, 약 16.9 MiB RSS로 완료됐다. index 폭 회귀의 최종 RED는 300 POSITION 중 0/1/2만 참조한 경우 UNSIGNED_SHORT가 선택되는 것이었고, 최대 참조 index를 추적한 뒤 255/256 및 65,535/65,536 경계와 함께 GREEN이 됐다.

최종 GLB 전용 결과:

```console
PASS deterministicCubeExportHasExactStructure
PASS exportIndexWidthUsesMaximumReferencedStorageIndex
PASS cubeAndEditedRoundTripsAreTriangulatedAndDeterministic
PASS importsUnsignedIndexWidthsAndNonIndexedTriangles
PASS importsInterleavedAndMultiplePrimitivesWithAccessorLocalSharing
PASS ignoredVisualDataProducesDeterministicWarnings
PASS malformedContainerJsonAndPaddingAreRejected
PASS exactIntegerPropertiesRejectRoundedFractionsExponentsAndOverflow
PASS ignoredVisualAttributesRequireValidAccessorIndices
PASS unsupportedExtensionsAndNewerMinimumVersionsAreRejected
PASS jsonObjectMembersAreBoundedAndLargeUniqueObjectsDecode
PASS unsupportedFeaturesTypesBoundsIndicesAndLimitsAreRejected
PASS truncationMutationAndAtomicInstallStaySafe
PASS encoderRejectsFloat32Overflow
14 test(s), 0 failure(s)
```

테스트는 반복 export byte equality, GLB/청크 길이와 padding, cube 및 편집 메시의 삼각형 round trip, uint8/uint16/uint32 index와 폭 경계, non-indexed primitive, offset/stride, 다중 primitive, diagnostic, malformed JSON/container/accessor, 정확한 정수 lexeme, extension/minVersion, 대형 객체 복잡도와 member limit, 모든 truncation boundary, fixed-seed mutation, 실패 install의 live project byte 불변을 검사한다.

## 최종 Linux 게이트

최종 `./scripts/check.sh`는 네 전용 실행 파일을 각각 C++20 `-Wall -Wextra -Wpedantic -Werror`로 빌드하고 실행하며, 이어서 shell 문법과 deterministic project validator를 실행했다.

```console
octopoly_core_tests:         16 test(s), 0 failure(s)
project_codec_tests:         16 test(s), 0 failure(s)
mesh_allocation_fault_tests:  8 test(s), 0 failure(s)
glb_codec_tests:             14 test(s), 0 failure(s)
[static] PASS
[check] PASS
```

동일한 네 suite를 ASan, UBSan, `_GLIBCXX_DEBUG` + `_GLIBCXX_DEBUG_PEDANTIC` 구성으로 각각 독립 재빌드·실행했다.

```console
asan PASS: 54 tests, 0 failures
ubsan PASS: 54 tests, 0 failures
debug PASS: 54 tests, 0 failures
```

ASan은 leak detection과 halt-on-error, UBSan은 stack trace와 halt-on-error를 사용했으며 sanitizer 또는 debug-container 진단은 없었다.

## Apple 검증 경계

이 WSL/Linux 호스트에는 macOS와 Xcode가 없으므로 Swift, Objective-C++, Metal, simulator, signing, 실기기 설치는 실행하지 않았다. `project.pbxproj`의 GLB `.cpp/.hpp` 참조와 Sources membership, SwiftUI → Foundation I/O → bridge → portable codec 경로만 정적으로 검사한다. 이 문서는 Apple compile 성공을 주장하지 않는다.

## 데이터 손실 계약

GLB는 삼각형 교환 포맷 경로다. OctoPoly n-gon은 결정적 fan triangulation으로 손실 변환되며 stable ID, revision, future-ID counter는 보존되지 않는다. POSITION 이외 시각 속성과 material은 유효 참조를 확인한 후 diagnostic을 동반해 무시하고, transforms/skins/animations/morphs/extensions는 오류로 거부한다. 상세 계약은 [../GLB.md](../GLB.md)에 있다.
