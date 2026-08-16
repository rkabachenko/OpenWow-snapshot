#!/usr/bin/env bash
# packaging/macos/sign-and-notarize.sh -- sign (and, with credentials, notarize
# and staple) a staged OpenWoW.app.
#
# ENTRY POINT (called by CI after `cmake --install ... --component client`):
#
#   packaging/macos/sign-and-notarize.sh <path/to/OpenWoW.app>
#
# Exactly one positional argument: the bundle to sign, in place. Exit status
# is non-zero on any failure of a step that was attempted.
#
# Behaviour is chosen from the environment (all optional):
#
#   APPLE_CERTIFICATE_P12_BASE64   Developer ID Application certificate,
#                                  base64-encoded .p12. When empty/unset the
#                                  script performs an AD-HOC signature
#                                  (`codesign --force --deep --sign -`) and
#                                  exits 0 -- the bundle is then launchable
#                                  locally but not notarized.
#   APPLE_CERTIFICATE_PASSWORD     Password of the .p12.
#   APPLE_TEAM_ID                  Team identifier; used to pick the signing
#                                  identity out of the imported certificate
#                                  ("Developer ID Application: ... (TEAMID)").
#   APPLE_API_KEY_ID               App Store Connect API key id      \
#   APPLE_API_ISSUER_ID            App Store Connect issuer id        | notarize
#   APPLE_API_KEY_P8_BASE64        base64 of the AuthKey_<id>.p8     /
#                                  Notarization runs only when all three are
#                                  set; otherwise the Developer ID signature is
#                                  applied without notarization (a warning is
#                                  printed).
#   OPENWOW_MACOS_ENTITLEMENTS     Optional path to an entitlements plist to
#                                  pass to codesign (none are required by the
#                                  client: no JIT, no dynamic libraries).
#   OPENWOW_SIGN_KEYCHAIN_PASSWORD Optional password for the temporary
#                                  keychain (random when unset).
#
# The temporary keychain, the decoded certificate and the API key are removed
# on exit regardless of outcome.
set -euo pipefail

usage() {
  echo "usage: $0 <path/to/OpenWoW.app>" >&2
  exit 2
}

[[ $# -eq 1 ]] || usage
APP_PATH="$1"
if [[ ! -d "$APP_PATH" || ! -f "$APP_PATH/Contents/Info.plist" ]]; then
  echo "error: '$APP_PATH' is not an application bundle" >&2
  exit 2
fi

log() { printf '[sign-and-notarize] %s\n' "$*"; }

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "error: this script must run on macOS (codesign/notarytool)" >&2
  exit 2
fi

# macOS ships bash 3.2, where expanding an empty array under `set -u` is an
# error; ${ARR[@]+"${ARR[@]}"} is the portable spelling used below.
ENTITLEMENT_ARGS=()
if [[ -n "${OPENWOW_MACOS_ENTITLEMENTS:-}" ]]; then
  [[ -f "$OPENWOW_MACOS_ENTITLEMENTS" ]] || {
    echo "error: OPENWOW_MACOS_ENTITLEMENTS='$OPENWOW_MACOS_ENTITLEMENTS' not found" >&2
    exit 2
  }
  ENTITLEMENT_ARGS=(--entitlements "$OPENWOW_MACOS_ENTITLEMENTS")
fi

# ---------------------------------------------------------------------------
# Ad-hoc path: no certificate available.
# ---------------------------------------------------------------------------
if [[ -z "${APPLE_CERTIFICATE_P12_BASE64:-}" ]]; then
  log "APPLE_CERTIFICATE_P12_BASE64 not set: applying AD-HOC signature (not notarized)"
  codesign --force --deep --sign - ${ENTITLEMENT_ARGS[@]+"${ENTITLEMENT_ARGS[@]}"} "$APP_PATH"
  codesign --verify --deep --verbose=2 "$APP_PATH"
  log "ad-hoc signature applied to $APP_PATH"
  exit 0
fi

# ---------------------------------------------------------------------------
# Developer ID path.
# ---------------------------------------------------------------------------
WORK_DIR="$(mktemp -d -t openwow-sign.XXXXXX)"
KEYCHAIN_PATH="$WORK_DIR/signing.keychain-db"
KEYCHAIN_PASSWORD="${OPENWOW_SIGN_KEYCHAIN_PASSWORD:-$(head -c 24 /dev/urandom | base64)}"
CERT_PATH="$WORK_DIR/certificate.p12"
API_KEY_PATH=""

cleanup() {
  set +e
  if [[ -f "$KEYCHAIN_PATH" ]]; then
    security delete-keychain "$KEYCHAIN_PATH" >/dev/null 2>&1
  fi
  rm -rf "$WORK_DIR"
}
trap cleanup EXIT

printf '%s' "$APPLE_CERTIFICATE_P12_BASE64" | base64 --decode > "$CERT_PATH"

log "creating temporary keychain"
security create-keychain -p "$KEYCHAIN_PASSWORD" "$KEYCHAIN_PATH"
security set-keychain-settings -lut 21600 "$KEYCHAIN_PATH"
security unlock-keychain -p "$KEYCHAIN_PASSWORD" "$KEYCHAIN_PATH"
security import "$CERT_PATH" -k "$KEYCHAIN_PATH" \
  -P "${APPLE_CERTIFICATE_PASSWORD:-}" \
  -T /usr/bin/codesign -T /usr/bin/security -f pkcs12
# Allow codesign to use the key without a UI prompt.
security set-key-partition-list -S apple-tool:,apple:,codesign: \
  -s -k "$KEYCHAIN_PASSWORD" "$KEYCHAIN_PATH" >/dev/null
# Put the temporary keychain in the search list without disturbing the rest.
ORIGINAL_KEYCHAINS=$(security list-keychains -d user | tr -d '"' | tr '\n' ' ')
# shellcheck disable=SC2086
security list-keychains -d user -s "$KEYCHAIN_PATH" $ORIGINAL_KEYCHAINS

IDENTITY_FILTER="Developer ID Application"
if [[ -n "${APPLE_TEAM_ID:-}" ]]; then
  IDENTITY_FILTER="Developer ID Application: .*(${APPLE_TEAM_ID})"
fi
IDENTITY_HASH="$(security find-identity -v -p codesigning "$KEYCHAIN_PATH" \
  | grep -E "$IDENTITY_FILTER" | head -n1 | awk '{print $2}')"
if [[ -z "$IDENTITY_HASH" ]]; then
  echo "error: no 'Developer ID Application' identity found in the imported certificate" >&2
  security find-identity -v -p codesigning "$KEYCHAIN_PATH" >&2 || true
  exit 1
fi
log "signing with identity $IDENTITY_HASH"

# Sign inner Mach-Os first (there are none today: the client is a single static
# executable), then the bundle. --options runtime enables the hardened runtime
# notarization requires; --timestamp embeds a secure timestamp.
find "$APP_PATH/Contents" -type f \( -perm -u+x -o -name '*.dylib' \) \
  ! -path "$APP_PATH/Contents/MacOS/*" -print0 2>/dev/null \
  | while IFS= read -r -d '' inner; do
      codesign --force --options runtime --timestamp --sign "$IDENTITY_HASH" \
        --keychain "$KEYCHAIN_PATH" "$inner"
    done
codesign --force --options runtime --timestamp --sign "$IDENTITY_HASH" \
  --keychain "$KEYCHAIN_PATH" ${ENTITLEMENT_ARGS[@]+"${ENTITLEMENT_ARGS[@]}"} "$APP_PATH"
codesign --verify --deep --strict --verbose=2 "$APP_PATH"
log "Developer ID signature applied"

if [[ -z "${APPLE_API_KEY_ID:-}" || -z "${APPLE_API_ISSUER_ID:-}" || -z "${APPLE_API_KEY_P8_BASE64:-}" ]]; then
  log "WARNING: notarization credentials (APPLE_API_KEY_ID / APPLE_API_ISSUER_ID / APPLE_API_KEY_P8_BASE64) incomplete; skipping notarization"
  exit 0
fi

API_KEY_PATH="$WORK_DIR/AuthKey_${APPLE_API_KEY_ID}.p8"
printf '%s' "$APPLE_API_KEY_P8_BASE64" | base64 --decode > "$API_KEY_PATH"

NOTARIZE_ZIP="$WORK_DIR/$(basename "$APP_PATH" .app).zip"
log "submitting for notarization"
ditto -c -k --keepParent "$APP_PATH" "$NOTARIZE_ZIP"
xcrun notarytool submit "$NOTARIZE_ZIP" \
  --key "$API_KEY_PATH" \
  --key-id "$APPLE_API_KEY_ID" \
  --issuer "$APPLE_API_ISSUER_ID" \
  --wait
log "stapling ticket"
xcrun stapler staple "$APP_PATH"
xcrun stapler validate "$APP_PATH"
spctl --assess --type execute --verbose=2 "$APP_PATH" || \
  log "WARNING: spctl assessment failed (Gatekeeper may still reject; check notarization log)"
log "done: $APP_PATH is signed, notarized and stapled"
