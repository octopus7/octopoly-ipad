#!/usr/bin/env bash
set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "error: install-device.sh must run on macOS with Xcode installed" >&2
  exit 1
fi

: "${OCTOPOLY_DEVICE_ID:?Set OCTOPOLY_DEVICE_ID to a device identifier from: xcrun devicectl list devices}"
: "${OCTOPOLY_APP_PATH:?Set OCTOPOLY_APP_PATH to the signed .app bundle path}"

if [[ ! -d "$OCTOPOLY_APP_PATH" ]]; then
  echo "error: app bundle not found: $OCTOPOLY_APP_PATH" >&2
  exit 1
fi

cat <<'NOTICE'
Installing requires a paired/trusted device with Developer Mode and a valid signed app.
Free Personal Team provisioning normally expires after 7 days; rebuild and reinstall
when it expires. It does not provide Ad Hoc, TestFlight, or App Store distribution.
NOTICE

xcrun devicectl device install app   --device "$OCTOPOLY_DEVICE_ID"   "$OCTOPOLY_APP_PATH"
