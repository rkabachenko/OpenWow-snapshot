#!/usr/bin/env bash
# packaging/linux/build-appimage.sh -- wrap an installed client stage into an
# AppImage.
#
# ENTRY POINT (called by CI after `cmake --install ... --component client`):
#
#   packaging/linux/build-appimage.sh <stage-dir> <out.AppImage>
#
#   <stage-dir>     the --prefix given to `cmake --install`; on Linux the client
#                   component installs bin/openwow-client,
#                   share/applications/openwow-client.desktop and
#                   share/icons/hicolor/512x512/apps/openwow.png (see
#                   apps/client/CMakeLists.txt).
#   <out.AppImage>  output path; parent directory is created.
#
# Exit status 0 on success. The script runs on Linux only (it executes
# appimagetool); on other hosts it exits 2 with a message. Host packages:
# desktop-file-utils (appimagetool validates the .desktop file), file,
# curl or wget for the pinned downloads, gpg when LINUX_GPG_PRIVATE_KEY is set.
#
# Environment (all optional):
#
#   OPENWOW_APPIMAGE_ARCH    target architecture (x86_64 | aarch64 | riscv64...).
#                            Default: `uname -m` of the host. Set it when the
#                            stage was cross-compiled (riscv64 lane).
#   APPIMAGETOOL             path to a usable appimagetool; skips the download.
#   APPIMAGE_RUNTIME_FILE    path to a type-2 AppImage runtime for the target
#                            arch; skips the runtime download.
#   OPENWOW_APPIMAGE_CACHE   directory where downloaded tools are kept
#                            (default: <stage-dir>/../.appimage-cache).
#   LINUX_GPG_PRIVATE_KEY    ASCII-armoured GPG private key. When set, a
#                            detached ASCII signature <out.AppImage>.asc is
#                            written next to the AppImage (imported into a
#                            temporary GNUPGHOME that is deleted afterwards).
#   LINUX_GPG_PASSPHRASE     passphrase of that key (may be empty).
#   OPENWOW_APPIMAGE_COMP    squashfs compressor passed to appimagetool
#                            (default: zstd).
#
# Pinned tool downloads (version + sha256, verified before use):
#   appimagetool 1.9.0       x86_64, aarch64 (upstream ships no other 64-bit
#                            hosts as of this pin)
#   type2-runtime 20251108   x86_64, aarch64
#
# Architectures without an upstream appimagetool/runtime (riscv64 today):
# the script cannot produce an AppImage. It then writes the assembled AppDir
# as a tarball at <out-without-.AppImage>-AppDir.tar.gz (runnable through its
# ./AppRun after extraction), prints
#   OPENWOW_APPIMAGE_FALLBACK=<that path>
# and exits 0, so a CI lane can still publish something honest for the arch.
# Provide APPIMAGETOOL + APPIMAGE_RUNTIME_FILE for such an arch to get a real
# AppImage.
set -euo pipefail

usage() {
  echo "usage: $0 <stage-dir> <out.AppImage>" >&2
  exit 2
}
[[ $# -eq 2 ]] || usage
STAGE_DIR="$(cd "$1" && pwd)"
OUT_PATH="$2"

log() { printf '[build-appimage] %s\n' "$*"; }

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "error: AppImages can only be assembled on Linux (appimagetool)" >&2
  exit 2
fi

CLIENT_BIN="$STAGE_DIR/bin/openwow-client"
DESKTOP_FILE="$STAGE_DIR/share/applications/openwow-client.desktop"
ICON_FILE="$STAGE_DIR/share/icons/hicolor/512x512/apps/openwow.png"
for required in "$CLIENT_BIN" "$DESKTOP_FILE" "$ICON_FILE"; do
  if [[ ! -e "$required" ]]; then
    echo "error: stage is missing $required (run 'cmake --install <build> --component client --prefix <stage>' first)" >&2
    exit 2
  fi
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ARCH="${OPENWOW_APPIMAGE_ARCH:-$(uname -m)}"
case "$ARCH" in
  amd64) ARCH=x86_64 ;;
  arm64) ARCH=aarch64 ;;
esac
COMP="${OPENWOW_APPIMAGE_COMP:-zstd}"
CACHE_DIR="${OPENWOW_APPIMAGE_CACHE:-$(dirname "$STAGE_DIR")/.appimage-cache}"
mkdir -p "$CACHE_DIR" "$(dirname "$OUT_PATH")"
OUT_PATH="$(cd "$(dirname "$OUT_PATH")" && pwd)/$(basename "$OUT_PATH")"

# ---------------------------------------------------------------------------
# Pinned downloads.
# ---------------------------------------------------------------------------
APPIMAGETOOL_VERSION="1.9.0"
RUNTIME_VERSION="20251108"
declare -A APPIMAGETOOL_SHA256=(
  [x86_64]="46fdd785094c7f6e545b61afcfb0f3d98d8eab243f644b4b17698c01d06083d1"
  [aarch64]="04f45ea45b5aa07bb2b071aed9dbf7a5185d3953b11b47358c1311f11ea94a96"
)
declare -A RUNTIME_SHA256=(
  [x86_64]="2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d"
  [aarch64]="00cbdfcf917cc6c0ff6d3347d59e0ca1f7f45a6df1a428a0d6d8a78664d87444"
)

sha256_of() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | awk '{print $1}'
  else
    shasum -a 256 "$1" | awk '{print $1}'
  fi
}

# fetch <url> <dest> <sha256>
fetch_pinned() {
  local url="$1" dest="$2" expected="$3"
  if [[ -f "$dest" && "$(sha256_of "$dest")" == "$expected" ]]; then
    log "using cached $(basename "$dest")"
    return 0
  fi
  log "downloading $url"
  local tmp="$dest.part"
  if command -v curl >/dev/null 2>&1; then
    curl -fsSL --retry 3 -o "$tmp" "$url"
  else
    wget -qO "$tmp" "$url"
  fi
  local actual
  actual="$(sha256_of "$tmp")"
  if [[ "$actual" != "$expected" ]]; then
    rm -f "$tmp"
    echo "error: sha256 mismatch for $url: expected $expected, got $actual" >&2
    exit 1
  fi
  mv "$tmp" "$dest"
}

# ---------------------------------------------------------------------------
# Assemble the AppDir.
# ---------------------------------------------------------------------------
WORK_DIR="$(mktemp -d -t openwow-appimage.XXXXXX)"
GNUPG_TMP=""
cleanup() {
  set +e
  [[ -n "$GNUPG_TMP" ]] && rm -rf "$GNUPG_TMP"
  rm -rf "$WORK_DIR"
}
trap cleanup EXIT

APPDIR="$WORK_DIR/OpenWoW.AppDir"
mkdir -p "$APPDIR/usr"
cp -a "$STAGE_DIR/." "$APPDIR/usr/"
install -m 0755 "$SCRIPT_DIR/AppRun" "$APPDIR/AppRun"
# appimagetool expects the .desktop file and its icon at the AppDir root
# (and .DirIcon) in addition to the XDG locations under usr/share.
cp "$DESKTOP_FILE" "$APPDIR/openwow-client.desktop"
cp "$ICON_FILE" "$APPDIR/openwow.png"
ln -sf openwow.png "$APPDIR/.DirIcon"
chmod 0755 "$APPDIR/usr/bin/openwow-client"
log "AppDir assembled at $APPDIR"

# ---------------------------------------------------------------------------
# Locate / download appimagetool and the runtime.
# ---------------------------------------------------------------------------
HOST_ARCH="$(uname -m)"
case "$HOST_ARCH" in
  amd64) HOST_ARCH=x86_64 ;;
  arm64) HOST_ARCH=aarch64 ;;
esac

APPIMAGETOOL_BIN="${APPIMAGETOOL:-}"
if [[ -z "$APPIMAGETOOL_BIN" ]]; then
  if [[ -n "${APPIMAGETOOL_SHA256[$HOST_ARCH]:-}" ]]; then
    APPIMAGETOOL_BIN="$CACHE_DIR/appimagetool-${APPIMAGETOOL_VERSION}-${HOST_ARCH}.AppImage"
    fetch_pinned \
      "https://github.com/AppImage/appimagetool/releases/download/${APPIMAGETOOL_VERSION}/appimagetool-${HOST_ARCH}.AppImage" \
      "$APPIMAGETOOL_BIN" "${APPIMAGETOOL_SHA256[$HOST_ARCH]}"
    chmod +x "$APPIMAGETOOL_BIN"
  fi
fi

RUNTIME_BIN="${APPIMAGE_RUNTIME_FILE:-}"
if [[ -z "$RUNTIME_BIN" ]]; then
  if [[ -n "${RUNTIME_SHA256[$ARCH]:-}" ]]; then
    RUNTIME_BIN="$CACHE_DIR/runtime-${RUNTIME_VERSION}-${ARCH}"
    fetch_pinned \
      "https://github.com/AppImage/type2-runtime/releases/download/${RUNTIME_VERSION}/runtime-${ARCH}" \
      "$RUNTIME_BIN" "${RUNTIME_SHA256[$ARCH]}"
  fi
fi

if [[ -z "$APPIMAGETOOL_BIN" || -z "$RUNTIME_BIN" ]]; then
  FALLBACK="${OUT_PATH%.AppImage}-AppDir.tar.gz"
  log "no pinned appimagetool (host $HOST_ARCH) / runtime (target $ARCH) available; writing AppDir tarball instead"
  tar -C "$WORK_DIR" -czf "$FALLBACK" "OpenWoW.AppDir"
  echo "OPENWOW_APPIMAGE_FALLBACK=$FALLBACK"
  exit 0
fi

# ---------------------------------------------------------------------------
# Build.
# ---------------------------------------------------------------------------
# appimagetool shells out to desktop-file-validate (desktop-file-utils) and
# aborts without it; fail here with a message that names the package.
if ! command -v desktop-file-validate >/dev/null 2>&1; then
  echo "error: desktop-file-validate not found; install desktop-file-utils (appimagetool requires it)" >&2
  exit 1
fi
log "building $OUT_PATH (arch=$ARCH, comp=$COMP)"
# APPIMAGE_EXTRACT_AND_RUN lets appimagetool (itself an AppImage) run on hosts
# without FUSE, such as containers and GitHub runners.
ARCH="$ARCH" APPIMAGE_EXTRACT_AND_RUN=1 \
  "$APPIMAGETOOL_BIN" --no-appstream --comp "$COMP" \
    --runtime-file "$RUNTIME_BIN" \
    "$APPDIR" "$OUT_PATH"
chmod +x "$OUT_PATH"
log "wrote $OUT_PATH"

# ---------------------------------------------------------------------------
# Optional detached GPG signature.
# ---------------------------------------------------------------------------
if [[ -n "${LINUX_GPG_PRIVATE_KEY:-}" ]]; then
  if ! command -v gpg >/dev/null 2>&1; then
    echo "error: LINUX_GPG_PRIVATE_KEY set but gpg is not installed" >&2
    exit 1
  fi
  GNUPG_TMP="$(mktemp -d -t openwow-gnupg.XXXXXX)"
  chmod 700 "$GNUPG_TMP"
  export GNUPGHOME="$GNUPG_TMP"
  printf '%s\n' "$LINUX_GPG_PRIVATE_KEY" | gpg --batch --quiet --import
  KEY_FPR="$(gpg --batch --list-secret-keys --with-colons | awk -F: '$1=="fpr"{print $10; exit}')"
  if [[ -z "$KEY_FPR" ]]; then
    echo "error: no secret key found after importing LINUX_GPG_PRIVATE_KEY" >&2
    exit 1
  fi
  log "signing with GPG key $KEY_FPR"
  gpg --batch --yes --pinentry-mode loopback \
    --passphrase "${LINUX_GPG_PASSPHRASE:-}" \
    --local-user "$KEY_FPR" --armor --detach-sign \
    --output "$OUT_PATH.asc" "$OUT_PATH"
  gpg --batch --verify "$OUT_PATH.asc" "$OUT_PATH"
  log "wrote $OUT_PATH.asc"
else
  log "LINUX_GPG_PRIVATE_KEY not set: AppImage left unsigned"
fi
