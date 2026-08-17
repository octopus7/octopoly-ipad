#!/usr/bin/env bash
set -euo pipefail

: "${OCTOPOLY_MAC_HOST:?Set OCTOPOLY_MAC_HOST to the SSH host name}"
: "${OCTOPOLY_MAC_REPO:?Set OCTOPOLY_MAC_REPO to the absolute checkout path on the Mac}"

TARGET="${OCTOPOLY_MAC_HOST}"
if [[ -n "${OCTOPOLY_MAC_USER:-}" ]]; then
  TARGET="${OCTOPOLY_MAC_USER}@${OCTOPOLY_MAC_HOST}"
fi
DESTINATION="${OCTOPOLY_XCODE_DESTINATION:-generic/platform=iOS Simulator}"
case "$OCTOPOLY_MAC_REPO$DESTINATION" in
  *"'"*) echo "error: remote path and destination must not contain a single quote" >&2; exit 2 ;;
esac

# Authentication belongs in the caller's SSH agent/config. This script stores no credentials.
ssh "$TARGET" "cd '$OCTOPOLY_MAC_REPO' && xcodebuild -project app/OctoPolyIPad/OctoPolyIPad.xcodeproj -scheme OctoPolyIPad -configuration Debug -destination '$DESTINATION' CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO build"
