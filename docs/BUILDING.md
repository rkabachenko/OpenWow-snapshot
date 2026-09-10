# Building OpenWoW

The client builds with CMake and Ninja on macOS, Linux and Windows. Native iOS
device and simulator applications use CMake's Xcode generator.
Dependencies come from a **vcpkg manifest** (`vcpkg.json`, baseline pinned in
`vcpkg-configuration.json`, overlay ports in `vcpkg-overlay-ports/`), so you do
not install them by hand.

You need a C++20 toolchain and CMake 3.24 or newer.

## 1. vcpkg bootstrap

The presets expect a vcpkg checkout at `.vcpkg` (git-ignored) at the repository
root. Use the baseline commit the manifest is pinned to, so you get the same
dependency versions the project was built against:

```sh
BASELINE=$(python3 -c 'import json;print(json.load(open("vcpkg-configuration.json"))["default-registry"]["baseline"])')
git clone https://github.com/microsoft/vcpkg .vcpkg
git -C .vcpkg checkout "$BASELINE"
./.vcpkg/bootstrap-vcpkg.sh -disableMetrics      # bootstrap-vcpkg.bat on Windows
```

The first configure builds every dependency (FFmpeg, Boost, bgfx, SDL2, ...) —
**expect 30–60 minutes cold**. Enable vcpkg binary caching
(`VCPKG_BINARY_SOURCES`) if you build on more than one machine.

## 2. Build

```sh
cmake --preset release
cmake --build --preset release --target openwow-client -j 4
```

On macOS the build target is a Mach-O executable; the install rules assemble
the runnable application bundle. After the compile step, create the normal
development/validation bundle with:

```sh
cmake --install build/release --component client --prefix build/macos-app
# the app is build/macos-app/OpenWoW.app
```

This bundle does not copy the original game Data unless
`OPENWOW_EMBED_GAME_DATA=ON` was selected at configure time. With the default
setting it continues to resolve the configured local content root or an
explicit `--game-data` / `OPENWOW_GAME_DATA` override. Use the bare
`build/release/apps/client/openwow-client` only for intermediate compile and
link checks; hand off `OpenWoW.app` for macOS runtime validation. The install
step applies an ad-hoc signature to the completed bundle so its executable and
resources form one valid local-development application. Distribution still
requires the project's intended Developer ID signing and notarization policy.

Available desktop configure presets (Ninja, build tree `build/<preset>`):

| Preset | Purpose |
| --- | --- |
| `debug` | Debug build |
| `release` | Optimised build |
| `release-lto` | Release + ThinLTO (`OPENWOW_ENABLE_THINLTO=ON`) |
| `release-pgo-generate` | Release, instrumented for profile generation |
| `release-pgo-use` | Release + ThinLTO using the generated profile |

Build presets of the same names exist for each. The PGO lanes are used in
sequence: build with `release-pgo-generate`, exercise the client, then build
`release-pgo-use`, which reads the profile from
`build/release-pgo-generate/pgo/openwow.profdata`.

Native iOS presets use Xcode, target arm64 and require an Apple Silicon host:

| Preset | Purpose |
| --- | --- |
| `ios-simulator` | self-contained iOS Simulator `.app`, including local `Data/` |
| `ios-device` | self-contained device `.app`, including local `Data/` |
| `ios-simulator-code-only` | compile/link validation without copying `Data/` |
| `ios-device-code-only` | unsigned-device compile/link validation without copying `Data/` |
| `ios-device-development` | signed device `.app` without bundled `Data/`; uses incrementally synchronized container Data |

The standard iOS presets intentionally embed `../LocalData/335a/Data`. The
unsigned code-only variants are intended for compile/link validation. The
signed development preset instead reads Data from the application's persistent
data container, so code builds and app updates do not package the large tree.

### Options

| Option | Default | Meaning |
| --- | --- | --- |
| `OPENWOW_BUILD_CLIENT` | ON | build `openwow-client` |
| `OPENWOW_ENABLE_MPQ_VFS` | ON | StormLib-backed MPQ VFS (required to read a game install) |
| `OPENWOW_LOCAL_CONTENT_ROOT` | `../LocalData/335a` | local build-12340 content root containing `Data/`; compiled in only when present |
| `OPENWOW_EMBED_GAME_DATA` | OFF globally; ON in standard iOS presets | copy the local `Data/` into an Apple `.app` for path-free startup |
| `OPENWOW_IOS_BUNDLE_IDENTIFIER` | `ink.mnt.elune` | bundle identifier used by the native iOS target |
| `OPENWOW_IOS_DEVELOPMENT_TEAM` | `5TCGFUXXZP` | Apple team identifier used for automatic iOS signing |
| `OPENWOW_IOS_DEVICE` | empty | device name or identifier used by `openwow-ios-sync-data` |
| `OPENWOW_IOS_TEXTURE_CACHE_LOCALE` | `zhCN` | locale used when resolving the offline iOS ASTC texture cache |
| `OPENWOW_IOS_TEXTURE_CACHE_TOOL` | native release-build path | host executable invoked before incremental iOS Data sync |
| `OPENWOW_WARNINGS_AS_ERRORS` | OFF | `-Werror` / `/WX` |
| `OPENWOW_ENABLE_CLANG_TIDY` | OFF | run clang-tidy while compiling |
| `OPENWOW_ENABLE_THINLTO` | OFF | ThinLTO (clang) / LTCG (MSVC) / `-flto=auto` (GCC) |

## 3. Per-OS notes

**macOS** — Xcode command line tools, plus
`brew install cmake ninja nasm autoconf automake libtool` (nasm and the
autotools are needed by the FFmpeg port). Set `CMAKE_OSX_DEPLOYMENT_TARGET` (or
`MACOSX_DEPLOYMENT_TARGET` in the environment) if the binary must run on an
older macOS than the build host; `packaging/macos/Info.plist.in`'s
`LSMinimumSystemVersion` mirrors whatever the executable was linked for.

**iOS** — a full Xcode installation with the iOS SDK is required. The target
uses iOS 15.0 as its deployment baseline, Metal for rendering, and SDL's UIKit
application entry point. Configure a simulator build with:

```sh
cmake --preset ios-simulator
cmake --build --preset ios-simulator --target openwow-client -j 4
```

For a self-contained device build, the repository's development Team ID and
Bundle ID are applied automatically:

```sh
cmake --preset ios-device
cmake --build --preset ios-device --target openwow-client -j 4
```

Both identifiers can still be overridden with `-DOPENWOW_IOS_BUNDLE_IDENTIFIER`
and `-DOPENWOW_IOS_DEVELOPMENT_TEAM`. Certificates, private keys, Apple account
credentials and provisioning profiles remain local to Xcode.

The output is
`build/ios-device/apps/client/Release-iphoneos/OpenWoW.app`. Install it through
Xcode's Devices and Simulators window or the organization's normal signed-app
deployment flow. Automatic signing still requires a valid certificate and
provisioning profile; those credentials are never stored in this repository.

For fast device iteration, configure the signed development build with a
connected device name or identifier:

```sh
cmake --preset ios-device-development \
  -DOPENWOW_IOS_DEVICE="My iPhone"
cmake --build --preset ios-device-development --target openwow-client -j 4
```

Install the resulting small
`build/ios-device-development/apps/client/Release-iphoneos/OpenWoW.app` through
Xcode, then perform the initial Data transfer:

```sh
cmake --preset release
cmake --build --preset release --target openwow-ios-texture-cache -j 4
cmake --build --preset ios-device-development \
  --target openwow-ios-sync-data
```

The sync target first maintains an incremental ASTC 4x4 working cache under
`OpenWoWBuildCache/iOS`, outside the synchronized `Data` tree. It caps cached
world textures at 128 pixels while preserving their mip chains, which avoids
iOS expanding the retail BC payloads to RGBA during world loading. The cache
records both the winning virtual path and a source-content fingerprint; stale
or missing entries fall back to the normal decoder. A manifest of the Data and
project-override trees makes later runs skip cache enumeration and encoding
when those inputs have not changed. The tool packages the cache into 16 MPQ
shards under `Data/OpenWoWDerived/iOSPacks`, so device sync transfers about 16
large files instead of more than 100,000 small files. The original archives are
never modified, and macOS neither mounts nor consumes this iOS-only cache.

After preparation, the sync target copies into the installed app's private
`Library/Application Support/OpenWoW/GameRoot/Data` and writes a
content-identity readiness marker only after the Data transfer succeeds. The
retail archives and derived texture cache have separate identities. A device
with the legacy successful whole-Data marker, or with the current retail
identity, never has its original MPQ and locale files submitted for transfer
merely because the derived cache changed. Fresh-install retail archives are
copied independently, and each ASTC shard is a separate resumable transfer;
each operation is retried up to three times. After all shards and their
manifest arrive, the target publishes the new readiness marker. On the next
client start, iOS removes the legacy loose ASTC directory only after validating
that all 16 archives and the pack manifest are present. A failed run is safe to
rerun: Xcode's device service skips unchanged files, completed archives are not
duplicated, and the previous readiness marker is not replaced by a partial run.
When both device identities match, the target performs no Data transfer at all.

Keep the iPhone unlocked and connected over USB during the initial transfer.
If even the initial marker probe reports a CoreDevice network-socket timeout,
reconnect the device and wait for Xcode to finish preparing it before rerunning
the same target. Normal source iterations only rebuild and reinstall the small
`.app`; the container survives app updates as long as the Bundle ID stays
unchanged. Uninstalling the app deletes its container, after which the Data
sync must be run again.

These presets suppress Xcode's automatic CMake regeneration so its SDK
environment cannot accidentally rebuild host-side vcpkg tools for iOS. Re-run
the `cmake --preset ...` configure command after changing CMake files, adding
resources, or changing the contents of a resource tree.

The initial iOS configure cross-builds the dependency graph and can take as
long as the first desktop configure. The self-contained standard build copies
roughly the full size of the original `Data/` directory into the `.app`; use it
for standalone packaging and the signed development preset for daily work.
The self-contained build includes the user's original game assets. For
TestFlight program updates, use `ios-device-development`, which leaves that
large Data tree in the existing application data container.

The iOS app icon is compiled from `packaging/ios/Assets.xcassets`, with
`AppIcon` selected as the app icon source. Xcode generates the device PNGs,
the compiled `Assets.car` (including the App Store icon), and the icon entries
merged into the final Info.plist, including `CFBundleIconName`. Keep those
entries owned by the asset compiler rather than maintaining a separate manual
icon list in `Info.plist.in`. The existing source image is 1024×1024 with no
alpha channel; Xcode derives the required device sizes from it. See Apple's
[asset catalog icon configuration](https://developer.apple.com/documentation/xcode/configuring-your-app-icon).

**Archive for Internal TestFlight**

Regenerate the signed device project before archiving:

```sh
cmake --preset ios-device-development
```

If multiple Xcode versions are installed, set `DEVELOPER_DIR` to the intended
Xcode's `Contents/Developer` directory before both configuration and archiving.
Use the same version as the Xcode GUI. A listed iPhoneOS SDK alone does not
guarantee that version has the platform components required to select an
archive destination.

Open `build/ios-device-development/OpenWoW.xcodeproj` in Xcode, select the
`openwow-client` scheme and the **Any iOS Device (arm64)** destination, then
choose **Product → Archive**. Select the newly created archive in Organizer
and choose **Distribute App → TestFlight Internal Only**. Xcode handles the
distribution signing and upload using the selected development team's App
Store Connect account. A local `.ipa` export is not required for this route.
App Store Connect must have an app record for the bundle identifier, and each
uploaded build needs a new build number; Xcode's distribution flow can manage
that number. Uploading is a separate action from creating the archive.

The iOS plist declares `ITSAppUsesNonExemptEncryption = false` for the
client's authentication and game protocol encryption. This declares exempt
encryption, rather than the absence of cryptography, and avoids repeating the
export questionnaire for each build. See Apple's [plist key documentation](https://developer.apple.com/documentation/bundleresources/information-property-list/itsappusesnonexemptencryption)
and BIS's [authentication and entertainment exclusions](https://media.bis.gov/learn-support/encryption-controls/cryptography-for-data-confidentiality).
Reassess this declaration if encryption beyond these uses is introduced.

The equivalent local archive command, without uploading or installing, is:

```sh
xcodebuild -project build/ios-device-development/OpenWoW.xcodeproj \
  -scheme openwow-client -configuration Release \
  -destination 'generic/platform=iOS' -jobs 4 \
  -archivePath "$PWD/build/ios-device-development/OpenWoW.xcarchive" archive
```

The archive must contain `Products/Applications/OpenWoW.app` and its top-level
`Info.plist` must contain `ApplicationProperties` with an `ApplicationPath`
of `Applications/OpenWoW.app`. The app target uses `SKIP_INSTALL=NO` and
`INSTALL_PATH=$(LOCAL_APPS_DIR)`; dependencies use `SKIP_INSTALL=YES`. An empty
installation path can leave the app in Xcode's `UninstalledProducts` directory
and produce a generic archive without **Distribute App**. Re-archive after
regenerating the project; old generic archives are not repaired automatically.
See Apple's [generic archive troubleshooting](https://developer.apple.com/documentation/technotes/tn3110-resolving-generic-xcode-archive-issue)
and [distribution workflow](https://developer.apple.com/documentation/xcode/distributing-your-app-for-beta-testing-and-releases).

Keep the same bundle identifier and signing team when updating the existing
phone installation, and install the TestFlight update over the app without
uninstalling it first. The signed development preset does not embed or clear
`Library/Application Support/OpenWoW/GameRoot/Data`; a fresh installation on a
different device still needs its own Data import.

Keyboard, mouse/trackpad and existing controller input paths remain available
on iOS alongside native multi-touch world controls and the safe-area-aware
mobile HUD. See `docs/IOS_INTERACTION.md` for the touch map and device
acceptance checklist.

**Linux (Debian/Ubuntu)** — what the vcpkg ports build against:

```
build-essential ninja-build pkg-config autoconf autoconf-archive automake libtool libtool-bin libltdl-dev nasm yasm python3
libx11-dev libxext-dev libxft-dev libxrandr-dev libxi-dev libxcursor-dev libxinerama-dev
libxss-dev libxxf86vm-dev libxkbcommon-dev libwayland-dev wayland-protocols
libegl1-mesa-dev libgl-dev libglu1-mesa-dev libibus-1.0-dev
```

plus `libfuse2t64 file desktop-file-utils zip` for the AppImage step. On aarch64
hosts vcpkg needs `VCPKG_FORCE_SYSTEM_BINARIES=1`.

**Windows** — Visual Studio 2022 with the C++ workload. Configure from a
Developer Command Prompt (`vcvarsall x64`) so Ninja finds `cl.exe`.
`SDL2::SDL2main` is linked automatically and the executable is a GUI-subsystem
app, so it opens without a console window.

## 4. Compatibility audit

The cross-system compatibility audit is a source-only check and does not start
the client or require a configured build:

```sh
cmake -DOPENWOW_SOURCE_DIR:PATH="$PWD" -P cmake/CompatibilityAudit.cmake
```

See [COMPATIBILITY_AUDIT.md](COMPATIBILITY_AUDIT.md) for the guarded contracts,
known limits and the one-time user acceptance matrix.

## 5. Content roots and running

The client needs the data files from your own copy of the game. The workspace
default is `../LocalData/335a/Data`, outside this Git repository. When that
directory exists at configure time, local builds find it automatically; no
folder picker or launch argument is needed. `--game-data <path>` and
`OPENWOW_GAME_DATA` remain available as explicit overrides.

Project-owned replacement files belong under `assets/overrides` using their
client logical paths. They are mounted above the original archives and are
included in macOS and iOS application bundles. Runtime downloads and generated
content are kept separately under the platform user-data directory. See
`docs/CONTENT_ROOTS.md` for precedence, writable paths, and bundle layout.

## 6. Packaging

`cmake/Packaging.cmake` drives CPack for the `client` install component: ZIP on
macOS and Windows, TGZ on Linux. The `packaging/` directory holds the platform
scaffolding — the macOS and iOS `Info.plist` templates and assets, the Windows
manifest and resource templates, and the Linux AppImage runner and desktop
entry.

```sh
cmake --build build/release --target package
```

To create a self-contained macOS application bundle, configure with
`-DOPENWOW_EMBED_GAME_DATA=ON` before packaging. This copies the local `Data/`
into `OpenWoW.app/Contents/Resources/GameRoot/Data`; it does not add those files
to Git.

The standard iOS presets perform the corresponding local-only packaging at
`OpenWoW.app/GameRoot/Data`. Project overrides are always copied to
`OpenWoW.app/OpenWoWOverrides`. Neither tree is committed as part of a build.
