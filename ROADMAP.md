# OctoPoly iPad Roadmap

각 차수는 독립적인 구현·정적 검사·커밋·푸시 경계다. 현재 저장소 상태에서는 Phase 1부터 Phase 3까지 구현되었으며, 이후 차수는 계획 상태다.

## Phase 1 — Portable mesh core and iPad shell (complete)

C++20 메시 코어, 안정 ID, 유효성 검사와 revision, 결정적 삼각분할, 기본 큐브, 제한된 단일 면 Loop Cut·Knife Cut·Inset·Merge·Extrude, SwiftUI/MetalKit 셸, Objective-C++ 브리지를 구현한다.

## Phase 2 — Native project save and load (complete)

버전이 명시된 OctoPoly 자체 포맷으로 프로젝트를 결정적으로 저장·로드한다. 손상·잘림·과도한 리소스 입력을 거부하고, 실패 시 기존 문서를 변경하지 않는 원자적 로드를 보장한다.

## Phase 3 — GLB import and export (complete)

지원 범위를 명시한 glTF 2.0 Binary(GLB) 임포트·익스포트를 구현한다. 알 수 없거나 미지원인 필드는 조용히 손실시키지 않고 명시적으로 오류 또는 진단을 반환한다.

## Phase 4 — Primitives, scene outliner, and world transforms (planned)

기본 도형 추가, 안정적인 오브젝트 ID와 씬 소유권, iPad 씬 아웃라이너, 오브젝트별 고유 로컬·월드 트랜스폼을 구현한다.

## Phase 5 — Mirror modifier, center merge, and clipping (planned)

X/Y/Z 축 비파괴 미러 모디파이어, 중심면 정점 병합, 정점이 미러 평면을 넘어가지 않도록 하는 clipping을 구현한다.

## Phase 6 — Element transforms and edit history (planned)

정점·에지·면 선택, 이동·회전·스케일, 삭제·디졸브, 트랜잭션 기반 Undo/Redo를 구현한다.

## Phase 7 — Extended topology tools (planned)

Bevel, Bridge, Fill, Subdivide, Flip Normals와 토폴로지 검사·복구 도구를 구현한다.

## Phase 8 — Retopology tools (planned)

표면 스냅, Poly Build, Relax와 리토폴로지 작업을 위한 표시·입력 보조 기능을 구현한다.

## Phase 9 — UV and texture painting (planned)

UV 데이터·편집·심, 텍스처 표시와 이미지 리소스 관리, 레이어를 고려한 텍스처 페인팅 코어를 구현한다.

## Phase 10 — Armature and weight painting (planned)

아마추어·본 계층, 스키닝 데이터, 정규화된 정점 웨이트, 웨이트 페인팅과 검증 도구를 구현한다.

## 공통 완료 조건

각 차수는 Linux에서 해당 코어 테스트, 경고를 오류로 처리한 C++20 컴파일, 구조·포맷 정적 검사를 통과한 뒤 완료로 본다. 현재 WSL 호스트에서는 Xcode/iPad 실빌드를 완료 조건으로 요구하지 않으며, 실행하지 못한 Apple 빌드·서명·설치는 검증 문서에 명시한다.
