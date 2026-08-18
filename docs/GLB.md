# OctoPoly GLB 지원 범위

Phase 3의 `octopoly::glb` 코덱은 Apple 프레임워크에 의존하지 않는 C++20 구현이다. glTF 2.0 Binary(GLB) 전체가 아니라, OctoPoly 메시 교환에 필요한 명시적 부분집합만 fail-closed 방식으로 처리한다.

## 익스포트

익스포터는 결정적으로 다음 구조를 만든다.

- GLB version 2 컨테이너, JSON 청크 1개와 BIN 청크 1개
- scene 1개, node 1개, mesh 1개, TRIANGLES primitive 1개
- 정점 저장 순서를 유지한 `POSITION` FLOAT VEC3 accessor
- 팬 삼각분할 순서의 인덱스 accessor
- 실제로 방출되는 최대 storage index에 따라 가장 작은 합법 타입인 UNSIGNED_BYTE, UNSIGNED_SHORT 또는 UNSIGNED_INT 선택(참조되지 않는 POSITION 정점은 타입 폭을 불필요하게 키우지 않음)
- `POSITION`의 min/max 기록
- JSON 공백 패딩과 BIN 0 패딩을 포함한 4-byte 정렬

타임스탬프, 파일 경로, 포인터, 임의 값은 기록하지 않는다. 같은 유효 메시의 반복 익스포트 결과는 동일하다. double 위치가 유한하지 않거나 float32 범위로 표현될 수 없으면 typed error로 실패한다.

OctoPoly의 n-gon은 GLB에서 삼각형으로 내보낸다. 따라서 이 경로는 **polygon-to-triangle loss**가 있으며, 다시 임포트해도 원래 n-gon 경계는 복원되지 않는다. 안정 ID, revision, 미래 ID counter도 GLB 표준 데이터가 아니므로 보존되지 않는다.

## 임포트

지원하는 입력은 다음과 같다.

- GLB v2의 내장 buffer 0 하나
- scene/node/mesh 각각 하나, 변환 없는 node 0이 mesh 0을 참조
- mesh 내 하나 이상의 TRIANGLES primitive
- `POSITION`: non-normalized FLOAT VEC3
- 선택적 인덱스: non-normalized SCALAR UNSIGNED_BYTE, UNSIGNED_SHORT 또는 UNSIGNED_INT
- 인덱스가 없는 삼각형 primitive
- 합법적인 accessor/bufferView `byteOffset`과 interleaved `byteStride`
- 여러 primitive가 같은 POSITION accessor를 참조하는 경우 accessor 인덱스 기준 공유

임포트 결과는 primitive 순서대로 삼각형 face를 만들고, 최초 사용 POSITION accessor 순서대로 정점에 1부터 연속 stable ID를 부여한다. revision은 0이며 다음 ID counter는 생성된 요소 뒤를 가리킨다. float 값이 같은 서로 다른 accessor의 정점은 합치지 않는다.

NORMAL, TEXCOORD 등 POSITION 이외의 선택적 시각 속성과 material 참조는 메시 형상에 적용하지 않고, 성공 결과에 결정적 diagnostic을 반환한다. 다만 무시하는 속성도 정확한 비음수 정수 lexeme이며 실제 accessor 범위 안을 참조해야 한다. 소수·지수 표기 또는 범위 밖 참조는 malformed glTF로 거부한다. UI는 진단을 성공 상태와 함께 표시한다.

## 명시적으로 미지원

다음 항목은 조용히 손실시키지 않고 typed error로 거부한다.

- GLB v1, 외부 URI 및 data URI buffer
- 모든 위치의 `extensions` payload와 extension 사용/요구, Draco 또는 meshopt 등 압축 데이터
- 2.0보다 높은 요구 버전의 `asset.minVersion`
- sparse 또는 extension accessor
- 정수·normalized POSITION, normalized indices
- TRIANGLES 이외 primitive mode
- morph target/weights, animation, skin
- node matrix/translation/rotation/scale, children, camera
- 여러 scene/node/mesh

## 검증과 리소스 한도

파서는 strict JSON을 사용하며 decoded duplicate key, 잘못된 escape/UTF-8와 surrogate, 비유한 숫자, trailing garbage를 거부한다. 객체 키는 정렬 후 인접 비교하여 공격자가 제어하는 hash 없이 최악 O(n log n)으로 중복을 검사한다. glTF 정수 schema 필드는 부동소수점 변환 결과가 아니라 원래 JSON lexeme의 비음수 10진 숫자만 checked conversion하므로 반올림, 소수, 지수 표기가 정수로 승인되지 않는다. 컨테이너·청크·bufferView·accessor 산술은 overflow와 실제 범위를 검사하고, JSON/BIN padding도 검증한다.

기본 `LoadLimits`:

| 제한 | 기본값 |
|---|---:|
| GLB 전체 | 64 MiB |
| JSON 청크 | 4 MiB |
| JSON 깊이 | 128 |
| JSON 노드 | 1,000,000 |
| 객체당 JSON member | 100,000 |
| 정점 | 1,000,000 |
| 삼각형 | 1,000,000 |
| primitive | 10,000 |

개수와 범위를 비례 할당 전에 검사하며, `std::bad_alloc`을 allocation category의 typed error로 변환한다. 공개 encode/decode/install API는 예외를 밖으로 전파하지 않는다. 전용 allocation-ordinal 회귀 테스트는 GLB decode/index 구성의 각 주입 실패가 typed error를 반환하고 live mesh와 canonical project bytes를 그대로 유지한 뒤, 최초 성공 ordinal에서 삼각형 candidate 전체를 한 번에 설치하는지 검사한다.

## 원자성 및 iPad 파일 경로

`decodeGlb`는 detached candidate를 끝까지 구성하고 derived vertex index와 전체 메시 불변식을 검증한다. `installGlb`는 성공한 candidate만 noexcept move assignment로 live mesh에 한 번 반영하므로 실패는 atomic하며 기존 메시를 변경하지 않는다.

SwiftUI의 Export GLB / Import GLB는 앱 sandbox Documents의 `OctoPoly.glb`를 사용한다. 저장은 `Data.write(to:options: .atomic)`, 읽기는 `Data(contentsOf:)`이며, 성공한 import 이후에만 렌더 geometry와 revision을 갱신한다.
