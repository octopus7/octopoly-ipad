# 유료 Apple Developer Program 없이 무선 기기 설치

- 대상: 개인 소유 iPhone/iPad에서 개발 빌드 테스트
- 현재 `OctoPolyIPad` target: `TARGETED_DEVICE_FAMILY=2`인 iPad 전용; iPhone 표기는 도구의 일반 절차를 설명할 때만 사용
- Apple 공식 정책·절차 확인일: 2026-08-18
- 기준: Apple Account + Xcode Personal Team
- 배포 형태: App Store/TestFlight/Ad Hoc이 아닌 **개인용 on-device development testing**
- 연계 문서: [Windows → Mac 원격 Apple 빌드 도구 설계](WINDOWS_MAC_REMOTE_BUILD_ARCHITECTURE.md)

## 1. 먼저 구분할 것

**유료 Apple Developer Program 등록은 필요하지 않지만 Apple Account는 필요하다.** Apple은 무료 Apple Account만으로 Xcode 사용과 개인 기기 테스트가 가능하고, Xcode에서는 이를 `Personal Team`이라고 부른다.[3]

Personal Team에는 현재 다음 제한이 있다.[3]

- 동시에 등록 가능한 App ID: 10개, 각각 7일 후 만료
- 플랫폼별 테스트 기기: 3대, 각각 7일 후 만료
- 프로비저닝 프로파일: 발급 후 7일 만료
- 만료 후 앱을 다시 빌드·설치해야 할 수 있음
- App Store, TestFlight, Ad Hoc 배포 용도가 아님

이 절차의 “무선 설치” 구조는 다음과 같다.

```text
Windows PC ──SSH──> Mac + Xcode ──페어링된 LAN/Wi-Fi──> iPhone/iPad
```

Windows가 기기에 직접 설치하는 것이 아니다. 이 문서의 Native 절차에서는 Apple 서명과 기기 통신을 Mac/Xcode가 담당한다.

이 문서가 제시하는 signed build/install 명령은 **Native OctoPoly**에 대한 절차다. 실제 Mac/iPad 실행은 아직 이 저장소에서 수행되지 않았으며 implementation acceptance gate로 남는다. UE 5.7 Personal Team 서명은 Epic Remote Build의 signing 자산 계약이 실제 프로브로 동결되기 전에는 지원 완료로 간주하지 않는다.

## 2. 가능한 것과 불가능한 것

### 가능

- 유료 프로그램 미가입 Apple Account로 개인 기기 테스트
- 최초 페어링 후 같은 네트워크에서 무선 빌드·설치·실행
- Windows에서 SSH로 Mac의 `xcodebuild`와 `devicectl` 호출
- 만료 전까지 설치된 개발 앱 실행
- 만료 시 다시 빌드·설치

### 불가능 또는 이 절차의 범위 밖

- Apple Account 없이 코드 서명
- Mac/Xcode 없이 iOS/iPadOS 네이티브 빌드·서명
- 최초 신뢰·Developer Mode 확인을 완전 무인 처리
- Personal Team 빌드를 다른 사용자에게 배포
- TestFlight/App Store/Ad Hoc 배포
- 유료 팀 또는 특정 entitlement가 필요한 기능 사용

## 3. 준비물

### Windows

- OpenSSH Client의 `ssh.exe`
- Mac에 접속할 SSH 키와 `~/.ssh/config` host alias
- Native 또는 UE 프로젝트
- [원격 빌드 설계](WINDOWS_MAC_REMOTE_BUILD_ARCHITECTURE.md)에 따른 Windows 빌드 도구

### Mac

- 대상 OS/프로젝트와 호환되는 Xcode
- Xcode Command Line Tools 선택 완료
- System Settings에서 Remote Login 활성화
- Apple Account를 Xcode에 추가
- 프로젝트 checkout 또는 원격 작업 bundle을 받을 디렉터리
- Apple 기기와 같은 네트워크

### iPhone/iPad

- Mac에 최초 연결할 USB 케이블
- 기기 잠금 암호
- Developer Mode
- Mac과 동일하거나 서로 통신 가능한 네트워크

## 4. 1회성 Mac/Xcode 준비

이 단계에는 계정 로그인과 기기 확인이 있으므로 Mac 화면에서 직접 수행한다. SSH 자동화가 비밀번호, 2FA 코드, 기기 암호를 입력하게 만들지 않는다. **Xcode UI 설정, 최초 signed Run, 이후 SSH `xcodebuild`는 같은 macOS 사용자로 실행한다.**

### 4.1 Xcode와 Command Line Tools

Mac Terminal에서 확인한다.

```bash
xcode-select -p
xcodebuild -version
xcodebuild -showsdks
```

Xcode가 첫 실행 라이선스나 추가 컴포넌트 설치를 요구하면 Xcode UI에서 완료한다.

### 4.2 Apple Account 추가

1. Xcode를 연다.
2. `Xcode > Settings > Accounts`로 이동한다.
3. Apple Account로 로그인한다.
4. 유료 팀이 없어도 Account 아래에 Personal Team을 사용할 수 있어야 한다.

Apple 공식 Xcode 절차도 물리 기기 실행 시 Xcode Accounts에 Apple Developer Program 계정 또는 개인 Apple Account로 로그인하고, 프로젝트의 Signing & Capabilities에서 team을 지정하도록 안내한다.[5]

### 4.3 프로젝트 서명 설정

OctoPoly Native 기준:

1. `app/OctoPolyIPad/OctoPolyIPad.xcodeproj`를 연다.
2. Target `OctoPolyIPad`를 선택한다.
3. `Signing & Capabilities`에서 `Automatically manage signing`을 켠다.
4. `Team`에서 자신의 Personal Team을 선택한다.
5. Bundle Identifier가 계정 내에서 고유한지 확인한다.
   - 저장소 기본값: `com.octopus7.OctoPolyIPad`
   - 충돌 시 개인용 고유 identifier로 바꾼다.

기능 capability를 추가할 때는 Personal Team에서 허용되는지 먼저 확인한다. 자동 서명이 profile을 만들지 못하면 entitlement를 줄인 최소 Debug target으로 다시 검증한다.

## 5. 최초 유선 페어링

Apple의 Xcode Help 절차에 따르면 iOS 기기의 최초 무선 페어링은 다음 순서다.[6]

1. Mac에서 Xcode를 연다.
2. `Window > Devices and Simulators`를 연다.
3. iPhone/iPad를 USB 케이블로 Mac에 연결한다.
4. 기기에 `Trust This Computer?`가 나타나면 사용자가 직접 Trust를 누르고 암호를 입력한다.
5. Xcode 왼쪽에서 기기를 선택한다.
6. 상세 화면에서 `Connect via network`를 켠다.
7. Xcode가 페어링을 완료할 때까지 기다린다.
8. 케이블을 분리한다.
9. 기기 옆에 network 아이콘이 보이고 `Connected` 아래에 남는지 확인한다.

“무선”은 **최초 케이블 페어링 이후의 반복 설치가 무선**이라는 뜻이다. iPhone/iPad에 대한 최초 신뢰 과정을 SSH만으로 우회하지 않는다.

## 6. Developer Mode 켜기

Apple은 Xcode에서 개발 앱을 실행하려면 Developer Mode를 켜도록 요구한다.[4]

1. 먼저 위의 기기 페어링을 시작하거나 완료한다.
2. 기기에서 `Settings > Privacy & Security > Developer Mode`를 켠다.
3. 경고에서 Restart를 선택한다.
4. 재부팅 후 다시 나타나는 확인에서 Enable을 누른다.
5. 기기 암호를 입력한다.

Developer Mode 메뉴는 Mac과 페어링을 시작했거나 과거에 페어링한 기기에서 나타난다.[4]

## 7. 최초 1회 Xcode 실행 검증

자동화 전에 Xcode UI에서 한 번 실제 실행해 서명 경계를 검증한다.

1. Xcode toolbar에서 scheme `OctoPolyIPad`를 선택한다.
2. Run destination으로 페어링된 iPad를 선택한다.
3. Run을 누른다.
4. Xcode가 기기 등록과 Personal Team development profile 생성을 완료하는지 확인한다.
5. 앱이 기기에 설치되고 실제로 시작되는지 확인한다.

물리 기기를 run destination으로 선택하고 automatic signing을 사용하면 Xcode가 기기를 등록하고 development provisioning profile을 생성한다.[5]

이 단계가 실패하면 SSH 자동화로 넘어가지 않는다. 먼저 Bundle Identifier, Team, entitlement, Developer Mode, 기기 신뢰를 해결한다.

## 8. 무선 연결 확인

Mac Terminal에서:

```bash
xcrun devicectl list devices
```

대상 기기가 표시되는지 확인한다. 명령 형식은 설치된 Xcode를 기준으로 다음도 확인한다.

```bash
xcrun devicectl help
xcrun devicectl device install app --help
xcrun devicectl device process launch --help
```

기기가 보이지 않으면 Xcode의 `Window > Devices and Simulators`를 먼저 확인한다. Xcode는 같은 네트워크에서 Bonjour로 페어링된 기기를 찾거나, 필요한 경우 기기 IP 주소로 연결할 수 있다.[7]

## 9. Windows SSH 준비

Windows PowerShell에서:

```powershell
ssh octopoly-build-mac 'id -un && uname -s && xcodebuild -version && security find-identity -v -p codesigning && xcrun devicectl list devices'
```

성공 기준:

- 원격 OS가 `Darwin`
- Xcode 버전 출력
- 대상 iPhone/iPad가 device list에 출력
- `id -un`의 사용자가 Xcode UI에서 Apple Account와 Personal Team을 설정한 사용자와 동일
- `security find-identity -v -p codesigning`에서 해당 사용자의 유효 identity 확인

권장 SSH config 예:

```sshconfig
Host octopoly-build-mac
    HostName 192.168.0.20
    User <MAC_XCODE_USER>
    IdentityFile ~/.ssh/octopoly_build_ed25519
    IdentitiesOnly yes
    StrictHostKeyChecking yes
```

Mac host key가 바뀌면 자동 승인하지 말고 실제 Mac의 fingerprint를 별도 경로로 확인한다.

비대화식 SSH에서 Keychain이 잠겨 signed probe가 실패하면 password를 SSH 명령이나 환경 변수로 전달하지 않는다. Mac 사용자 세션과 Keychain 상태를 직접 복구한 뒤 다시 실행한다. 별도 build 계정을 쓰고 싶다면 그 계정으로 Xcode 로그인, Team 선택, 최초 signed Run과 identity 확인을 모두 다시 수행한다.

## 10. Native OctoPoly 무선 빌드·설치

### 10.1 Windows shader 전환 후 Mac signed device build

> **현재 Phase 1 checkout에서는 이 절차를 실행하지 않는다.** 현재 Xcode project는 `Shaders.metal`을 Sources에서 Mac 컴파일하므로 “모든 Metal shader compile은 Windows”라는 필수 경계를 위반한다. 아래 build/install은 다음 조건을 모두 만족한 Windows 생성 source bundle에만 허용한다.

- Windows가 `default.metallib`과 `shader-manifest.json`을 생성하고 두 파일의 hash·compiler version·target profile을 receipt에 기록함
- source bundle의 PBX Sources에 `Shaders.metal`이 없고 PBX Resources에 `default.metallib`이 포함됨
- Controller가 Mac 업로드 전 manifest와 `default.metallib` hash를 검증함
- Mac preflight와 build 후 감사에서 `CompileMetalFile`, `MetalLink`, `metal`, `metallib` 실행이 하나라도 확인되면 실패함

이 전환과 검증기는 [원격 빌드 설계](WINDOWS_MAC_REMOTE_BUILD_ARCHITECTURE.md)의 구현 차수 B 산출물이다. 구현되기 전에는 현재 helper나 아래 `xcodebuild`를 Personal Team 설치 우회 경로로 사용하지 않는다.

아래 placeholder를 실제 값으로 대체한다.

- `<MAC_REPO>`: Mac의 저장소 절대 경로
- `<XCODE_DESTINATION_ID>`: 아래 `xcodebuild -showdestinations`에서 확인한 물리 기기 destination ID
- `<DEVICE_ID>`: `devicectl list devices`에서 확인한 설치 대상 식별자
- `<DEVICE_UDID>`: Xcode의 기기 상세에서 확인하고 embedded provisioning profile의 `ProvisionedDevices`와 대조할 Apple 기기 UDID
- `<TEAM_ID>`: Xcode Personal Team의 development team 식별자
- `<BUNDLE_ID>`: 개인 계정에서 고유한 Bundle Identifier

`<XCODE_DESTINATION_ID>`, `<DEVICE_ID>`, `<DEVICE_UDID>`는 도구별 식별자 계약이므로 값이 같다고 가정하지 않고 각각 발견·검증한다.

빌드 destination을 먼저 발견한다.

```bash
cd <MAC_REPO>
xcodebuild \
  -project app/OctoPolyIPad/OctoPolyIPad.xcodeproj \
  -scheme OctoPolyIPad \
  -showdestinations
```

위 Windows shader 계약을 통과한 source bundle의 Mac 작업 디렉터리에서만 다음 `xcodebuild`를 실행한다.

```bash
cd <MAC_REPO>

xcodebuild \
  -project app/OctoPolyIPad/OctoPolyIPad.xcodeproj \
  -scheme OctoPolyIPad \
  -configuration Debug \
  -destination 'id=<XCODE_DESTINATION_ID>' \
  -derivedDataPath "$HOME/Library/Developer/Xcode/DerivedData/OctoPolyRemote" \
  -allowProvisioningUpdates \
  -allowProvisioningDeviceRegistration \
  DEVELOPMENT_TEAM=<TEAM_ID> \
  PRODUCT_BUNDLE_IDENTIFIER=<BUNDLE_ID> \
  CODE_SIGN_STYLE=Automatic \
  build
```

주의:

- `-allowProvisioningUpdates`는 Xcode가 Apple Account의 signing 자산을 갱신하도록 허용한다. 1회성 UI 검증이 끝난 프로필에서만 사용한다.
- Team/Bundle ID를 저장소에 고정하지 않을 경우 로컬 전용 `.xcconfig` 또는 `octobuild` local overlay로 관리한다.
- `.app`이 생성됐다는 것과 기기에 설치됐다는 것은 별도 성공 단계다.

명시적으로 지정한 DerivedData 경로를 사용한 이 예의 앱 위치:

```text
~/Library/Developer/Xcode/DerivedData/OctoPolyRemote/Build/Products/Debug-iphoneos/OctoPolyIPad.app
```

실제 경로는 build setting과 출력 로그로 확인한다.

### 10.2 서명과 profile 검증

설치 전에 Mac에서 `.app`의 서명과 embedded profile을 확인한다.

```bash
set -euo pipefail

APP="$HOME/Library/Developer/Xcode/DerivedData/OctoPolyRemote/Build/Products/Debug-iphoneos/OctoPolyIPad.app"
EXPECTED_TEAM_ID='<TEAM_ID>'
EXPECTED_BUNDLE_ID='<BUNDLE_ID>'

codesign --verify --deep --strict --verbose=2 "$APP"

PROFILE_DIR="$(mktemp -d)"
chmod 700 "$PROFILE_DIR"
trap 'rm -rf "$PROFILE_DIR"' EXIT
codesign -d --entitlements :- "$APP" > "$PROFILE_DIR/app-entitlements.plist" 2> "$PROFILE_DIR/codesign-display.log"
security cms -D -i "$APP/embedded.mobileprovision" > "$PROFILE_DIR/profile.plist"

APP_BUNDLE_ID="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$APP/Info.plist")"
APP_IDENTIFIER="$(/usr/libexec/PlistBuddy -c 'Print :application-identifier' "$PROFILE_DIR/app-entitlements.plist")"
APP_TEAM_ID="$(/usr/libexec/PlistBuddy -c 'Print :com.apple.developer.team-identifier' "$PROFILE_DIR/app-entitlements.plist")"
PROFILE_IDENTIFIER="$(/usr/libexec/PlistBuddy -c 'Print :Entitlements:application-identifier' "$PROFILE_DIR/profile.plist")"
PROFILE_TEAM_ENTITLEMENT="$(/usr/libexec/PlistBuddy -c 'Print :Entitlements:com.apple.developer.team-identifier' "$PROFILE_DIR/profile.plist")"
PROFILE_TEAM_ID="$(/usr/libexec/PlistBuddy -c 'Print :TeamIdentifier:0' "$PROFILE_DIR/profile.plist")"

printf 'APP_BUNDLE_ID=%s\n' "$APP_BUNDLE_ID"
printf 'APP_IDENTIFIER=%s\n' "$APP_IDENTIFIER"
printf 'APP_TEAM_ID=%s\n' "$APP_TEAM_ID"

test "$APP_BUNDLE_ID" = "$EXPECTED_BUNDLE_ID"
test "$APP_TEAM_ID" = "$EXPECTED_TEAM_ID"
test "$APP_IDENTIFIER" = "$APP_TEAM_ID.$APP_BUNDLE_ID"
test "$APP_TEAM_ID" = "$PROFILE_TEAM_ENTITLEMENT"
test "$PROFILE_TEAM_ID" = "$PROFILE_TEAM_ENTITLEMENT"
case "$PROFILE_IDENTIFIER" in
  *'*')
    PROFILE_PREFIX="${PROFILE_IDENTIFIER%\*}"
    case "$APP_IDENTIFIER" in
      "$PROFILE_PREFIX"*) ;;
      *) echo "app identifier is outside the wildcard profile" >&2; exit 1 ;;
    esac
    ;;
  *) test "$APP_IDENTIFIER" = "$PROFILE_IDENTIFIER" ;;
esac

/usr/libexec/PlistBuddy -c 'Print :Name' "$PROFILE_DIR/profile.plist"
/usr/libexec/PlistBuddy -c 'Print :ExpirationDate' "$PROFILE_DIR/profile.plist"
/usr/libexec/PlistBuddy -c 'Print :ProvisionedDevices' "$PROFILE_DIR/profile.plist"

python3 - "$PROFILE_DIR/profile.plist" '<DEVICE_UDID>' <<'PY'
import plistlib
import sys
from datetime import datetime, timezone

with open(sys.argv[1], "rb") as source:
    profile = plistlib.load(source)
if sys.argv[2] not in profile.get("ProvisionedDevices", []):
    raise SystemExit("device UDID is not provisioned")
expiry = profile.get("ExpirationDate")
if not isinstance(expiry, datetime):
    raise SystemExit("profile has no valid ExpirationDate")
if expiry.tzinfo is None:
    expiry = expiry.replace(tzinfo=timezone.utc)
if expiry <= datetime.now(timezone.utc):
    raise SystemExit("provisioning profile is expired")
PY
```

성공 기준:

- `codesign --verify`가 0으로 종료
- 앱 application identifier가 `<TEAM_ID>.<BUNDLE_ID>`와 일치하고 profile의 exact 또는 wildcard application identifier 범위에 포함됨
- 앱·profile의 Team identifier 대조가 통과
- `<DEVICE_UDID>`가 profile의 `ProvisionedDevices`에 포함됨
- profile 만료일이 현재 시각보다 이후
- 출력된 `APP_BUNDLE_ID`가 설치·launch에 사용할 `<BUNDLE_ID>`와 일치

위 명령은 identity/team/bundle/device/expiry 수동 진단 절차다. `octobuild` 구현은 앱 entitlement 전체가 profile 허용 집합의 유효한 부분집합인지 구조적으로 비교하고 그 결과를 receipt에 기록해야 한다.

### 10.3 signed `.app` 설치

저장소의 기존 Mac helper를 사용할 수 있다.

```bash
cd <MAC_REPO>
OCTOPOLY_DEVICE_ID=<DEVICE_ID> \
OCTOPOLY_APP_PATH="$HOME/Library/Developer/Xcode/DerivedData/OctoPolyRemote/Build/Products/Debug-iphoneos/OctoPolyIPad.app" \
./scripts/mac/install-device.sh
```

helper 없이 직접 실행:

```bash
xcrun devicectl device install app \
  --device <DEVICE_ID> \
  "$HOME/Library/Developer/Xcode/DerivedData/OctoPolyRemote/Build/Products/Debug-iphoneos/OctoPolyIPad.app"
```

### 10.4 앱 실행

설치된 Xcode의 help가 아래 형태를 확인한 경우 실행한다.

```bash
xcrun devicectl device process launch \
  --device <DEVICE_ID> \
  <BUNDLE_ID>
```

성공 기준은 install 명령 exit code뿐 아니라 다음을 포함한다.

- 설치 결과에 대상 device와 app이 표시됨
- launch 명령 성공
- 기기 화면에서 앱 첫 프레임 확인
- OctoPoly의 경우 Metal viewport와 기본 cube 렌더 확인

### 10.5 Windows 한 번의 SSH 세션으로 호출

실제 구현에서는 복잡한 quoting을 피하기 위해 Mac Agent JSON 요청을 사용한다. 현재 helper를 수동 호출하는 최소 형태는 다음과 같다.

```powershell
ssh octopoly-build-mac `
  'cd "<MAC_REPO>" && OCTOPOLY_DEVICE_ID="<DEVICE_ID>" OCTOPOLY_APP_PATH="$HOME/Library/Developer/Xcode/DerivedData/OctoPolyRemote/Build/Products/Debug-iphoneos/OctoPolyIPad.app" ./scripts/mac/install-device.sh'
```

Team ID, device ID, bundle ID는 암호는 아니지만 로그와 공개 저장소에 불필요하게 고정하지 않는다. Apple Account 비밀번호나 2FA 코드는 절대 SSH 명령에 넣지 않는다.

## 11. UE 5.7 무선 설치

### 11.1 Windows 작업

UE 프로젝트는 [원격 빌드 설계](WINDOWS_MAC_REMOTE_BUILD_ARCHITECTURE.md)의 UE 프로필을 따른다.

- Cook: Windows
- ShaderCompileWorker: Windows
- Metal Developer Tools 5.3: Windows
- Stage/Pak/IoStore: Windows
- Native compile/link: Primary Mac
- Package/sign: [원격 빌드 설계](WINDOWS_MAC_REMOTE_BUILD_ARCHITECTURE.md)의 §2.4 실제 프로브 전에는 미확정

Epic의 Remote Mac Build는 Windows에서 SSH로 Mac에 연결해 iOS 빌드를 만들며, Primary Mac은 Xcode가 필요한 주요 빌드 작업을 수행한다.[1]

기본 `BuildCookRun`은 Build→Cook→Stage→Package→Deploy→Run 순서이므로 UBT의 Remote Mac Build 단계가 Windows Cook보다 먼저 실행될 수 있다.[10] 이 절차는 “Cook이 먼저 끝난 뒤 Mac build”라고 가정하지 않고, 각 단계의 실제 host를 UAT 로그와 exec evidence로 확인한다.

### 11.2 Personal Team 지원 판정

UE C++ 프로젝트의 Remote Mac Build는 인증서와 provisioning 구성이 맞아야 한다. Epic의 UE 5.7 공식 절차는 Windows의 Project Settings에서 Remote Build에 사용할 Provisioning Profile과 그 profile을 만든 Signing Certificate가 서로 일치해야 한다고 설명한다.[9] 따라서 Native처럼 Mac Xcode에서 Personal Team만 선택하면 UE signed package도 된다고 가정하지 않는다.

권장 순서:

1. Windows에서 UE 5.7 `BuildCookRun`을 실행하되 Build→Cook→Stage→Package의 실제 이벤트와 host를 기록한다.[10]
2. Cook과 ShaderCompileWorker가 Windows에서만 실행됐음을 확인한다.
3. Windows UE Project Settings가 참조하는 profile/certificate selector와 실제 signing 실행 host를 기록한다.[9]
4. [원격 빌드 설계](WINDOWS_MAC_REMOTE_BUILD_ARCHITECTURE.md)의 §2.4 Mac-managed signing 또는 explicit provisioning 프로브를 실행한다.
5. Mac-managed signing이 선택되면 Xcode UI와 SSH를 같은 macOS 사용자로 실행하고, Keychain identity와 signed probe를 확인한다.
6. 프로젝트 entitlement를 포함한 signed `.app` 생성과 실제 기기 launch가 통과한 뒤에만 그 toolchain lock에서 Personal Team install profile을 활성화한다.

Epic 문서에서 Secondary Mac은 빌드나 Cook을 하지 않고 Primary Mac에서 만든 캐시 데이터를 받아 Xcode 디버깅을 준비하며, `Prepare for Debugging`은 previously-cooked data를 Xcode build에 주입한다.[1] 이 경로는 cooked data 전달을 설명할 뿐 Personal Team signing 자산 위치를 자동으로 해결한다는 증거가 아니므로, 위 signing decision gate를 대체하지 않는다.

### 11.3 UE 산출물 설치

`octobuild`의 표준 설치 입력은 signed device `.app` bundle이다.

- `.app` 경로가 있으면 `devicectl device install app`으로 설치한다.
- `.ipa`만 있는 경우 임의로 압축 해제·재서명하지 않는다. 빌드 영수증이 가리키는 Xcode/UE 공식 설치 경로를 사용하거나 signed `.app` 산출물을 보존하도록 패키징 구성을 수정한다.
- 설치 후 bundle identifier로 launch하고 첫 렌더 프레임을 확인한다.

## 12. 7일 만료 후 재설치

Personal Team profile이 만료되면 다음을 반복한다.[3]

1. Mac의 Xcode Accounts에서 Apple Account 상태 확인
2. 기기가 여전히 페어링되고 Developer Mode가 켜져 있는지 확인
3. signed Debug `.app` 재빌드
4. 동일 기기에 `devicectl`로 재설치
5. launch 및 첫 프레임 확인

소스가 바뀌지 않았더라도 signing/provisioning 만료 때문에 재빌드가 필요할 수 있다. 이전 profile을 억지로 재사용하거나 만료 검사를 우회하지 않는다.

## 13. 문제 해결

### 기기가 Mac에 보이지 않음

1. 기기를 깨우고 잠금 해제한다.
2. Mac과 기기가 같은 네트워크인지 확인한다.
3. Xcode `Devices and Simulators`에서 network 아이콘을 확인한다.
4. 필요 시 Xcode에서 device를 IP 주소로 연결한다.[7]
5. 네트워크에서 TCP 62078 통신이 차단됐는지 확인한다.[8]
6. VPN, guest Wi-Fi, AP isolation을 확인한다.
7. 해결되지 않으면 cable로 연결해 unpair/re-pair한다.

### Developer Mode 메뉴가 없음

- 먼저 Mac/Xcode에서 기기 페어링을 시작한다.
- Apple 문서상 Developer Mode 메뉴는 페어링을 시작했거나 과거에 페어링한 뒤 나타난다.[4]

### `Signing requires a development team`

- Xcode `Signing & Capabilities`에서 Personal Team 선택
- `Automatically manage signing` 활성화
- CLI의 `DEVELOPMENT_TEAM` 값 확인

### Bundle Identifier 충돌

- 개인 계정에서 고유한 reverse-DNS identifier로 변경
- Xcode target과 UE Project Settings의 Bundle Identifier를 일치시킴

### 프로비저닝 실패

- Xcode UI에서 먼저 같은 target/device를 Run
- Apple Account 세션과 Personal Team 확인
- unsupported capability 제거
- 7일 만료 시 profile 재생성 후 재빌드

### 설치는 성공했지만 앱이 실행되지 않음

- Developer Mode 확인
- signed app의 bundle identifier 확인
- profile 만료 확인
- `devicectl` launch help와 실제 명령 결과 확인
- device console/crash log와 `.xcresult` 수집
- Native Metal 앱은 `default.metallib` 및 shader function 로딩 실패를 별도 확인

## 14. 완료 체크리스트

### 최초 준비

- [ ] Xcode Command Line Tools 선택
- [ ] Xcode에 개인 Apple Account 로그인
- [ ] Xcode UI 사용자와 SSH `id -un` 사용자가 동일
- [ ] 같은 사용자의 `security find-identity -v -p codesigning`에서 유효 identity 확인
- [ ] Target의 Team = Personal Team
- [ ] Automatically manage signing 활성화
- [ ] 고유 Bundle Identifier 설정
- [ ] USB로 최초 Trust/Pair
- [ ] Connect via network 활성화
- [ ] Developer Mode 활성화 및 재부팅 확인
- [ ] Xcode UI에서 최초 build/install/launch 성공

### 반복 무선 작업

- [ ] Windows→Mac SSH 성공
- [ ] `devicectl list devices`에서 기기 발견
- [ ] signed device `.app` 생성
- [ ] `.app` signature/provisioning 검증
- [ ] 무선 install 성공
- [ ] launch 성공
- [ ] 실제 첫 프레임 확인
- [ ] profile 만료 예정일/재설치 필요성 기록

## 15. 보안 체크리스트

- [ ] Apple Account 비밀번호와 2FA를 자동화에 넣지 않음
- [ ] Native signing private key를 Mac 밖으로 내보내지 않음
- [ ] SSH host key를 엄격히 검증
- [ ] Native signed job은 Xcode UI 설정과 같은 macOS 사용자로 실행
- [ ] 별도 비관리자 계정을 쓰면 그 계정에서 Xcode 로그인·Team·최초 signed probe를 독립적으로 완료
- [ ] Keychain password를 명령행·환경 변수·로그에 넣지 않음
- [ ] device/team ID가 공개 로그에 불필요하게 노출되지 않음
- [ ] `-allowProvisioningUpdates` 사용 프로필을 명시적으로 제한
- [ ] Personal Team 빌드를 외부 배포하지 않음

## Sources

[1] https://dev.epicgames.com/documentation/en-us/unreal-engine/creating-remote-builds-of-unreal-engine-projects-for-ios?application_version=5.7
[3] https://developer.apple.com/support/compare-memberships
[4] https://developer.apple.com/documentation/xcode/enabling-developer-mode-on-a-device
[5] https://developer.apple.com/documentation/xcode/running-your-app-on-simulated-or-physical-devices
[6] https://help.apple.com/xcode/mac/current/en.lproj/devbc48d1bad.html
[7] https://help.apple.com/xcode/mac/current/en.lproj/dev3e2f4ee6d.html
[8] https://help.apple.com/xcode/mac/current/en.lproj/devac3261a70.html
[9] https://dev.epicgames.com/documentation/en-us/unreal-engine/setting-up-ios-tvos-and-ipados-provisioning-profiles-and-signing-certificates-for-unreal-engine-projects?application_version=5.7
[10] https://dev.epicgames.com/documentation/en-us/unreal-engine/build-operations-cooking-packaging-deploying-and-running-projects-in-unreal-engine?application_version=5.7
