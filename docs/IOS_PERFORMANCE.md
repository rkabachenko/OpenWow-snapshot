# iOS Release performance logging

`performanceLog` enables persistent performance diagnostics in ordinary Release
builds. It defaults to `1` on iOS and `0` on desktop, is saved in `Config.wtf`,
and can be changed with `/console performanceLog 1` or
`/console performanceLog 0`. Keep the normal INFO log level. It requires no
debugger attachment, Lua instruction hook, debug-control server, GPU capture,
or `get-task-allow` entitlement. In Xcode, select the Release build configuration
and uncheck **Debug executable** in the Run scheme. The existing development
signing and provisioning setup can remain in use.

Records go to the existing asynchronous UTC JSON-lines log at
`logs/openwow-client.log` under the resolved writable user-data root. The
startup stderr line `OpenWoW log: <absolute path>` identifies the actual path.
Do not collect logs from the game/content root. No additional trace file is
needed. These are maintained diagnostics, not temporary acceptance probes.

While away from a Mac, use the mobile HUD's **Menu / 功能 → Logs / 导出日志**
to save or share a copy of this log. It includes the queued entries up to the
export boundary and all earlier sessions still in the current log file.
See [the export workflow](IOS_INTERACTION.md#export-logs-on-ios).

For a development-signed device build, connect and unlock the iPhone, then
copy just the log with Xcode's device tool. Replace `My iPhone` with the
configured device name or identifier:

```sh
xcrun devicectl device copy from --device "My iPhone" \
  --domain-type appDataContainer --domain-identifier ink.mnt.elune \
  --source "Library/Application Support/OpenWoW/logs/openwow-client.log" \
  --destination /tmp/openwow-client-ios.log --timeout 45
```

This transfer does not launch the client or attach a debugger, and does not
download the Data archives. The source path is relative to the iOS app data
container; a custom `OPENWOW_USER_DATA` setting requires its corresponding
container-relative path. File sharing is disabled in the app's Info.plist, so
the live log is not exposed through the iPhone Files app. The export flow can
save an independent copy there through the system share sheet.

## What the records measure

Search for `Perf:`. Span records contain `operation`, monotonic `start_us`,
`duration_ms`, originating thread (the standard JSON `thread` field), and
resource or script context. Durations are inclusive wall time: nested scopes
and worker/main-thread intervals overlap and must not be summed as CPU usage.
Milestone scopes emit a begin event and an exit span, including on early
returns; an exit span alone does not indicate successful loading. Existing
error logs and the loading-screen completion event establish the outcome.

| Operation | Meaning |
| --- | --- |
| `world.enter_requested` | Validated character-entry request before local preparation |
| `world.login_network_wait` | Background login request and response wait; overlaps local preparation |
| `world.core_init` | Shared entry initialization before preparing the map |
| `world.prepare_map` | Loading-screen setup, map preparation and initial streaming requests |
| `world.finalize_entry`, `world.ui_start`, `ui.load_default` | Local world and UI startup, including FrameXML and saved state |
| `ui.load_toc` | Complete individual TOC transaction, including checkpoints, loading and rollback if needed |
| `ui.xml_parse_materialize`, `ui.xml_materialize_group`, `ui.lua_file` | Slow XML loading/materialization or Lua file compilation/execution |
| `world.wait` | Once-per-second loading gate snapshot: player/UI readiness, map streaming, terrain/doodad upload readiness, transport and texture queues |
| `world.loading_screen_dismissed` | Time from map preparation to satisfying the world-entry gate and dismissing the loading screen |
| `frame.mode_submitted` | Main loop completed its submit boundary after a UI-mode change; not proof of GPU completion or visible pixels |
| `ui.root_frame_show`, `ui.root_frame_hide` | Direct `UIParent` child visibility callback boundary, before OnShow/OnHide; identifies panel openings without invoking layout or script getters |
| `ui.script_callback` | Slow protected frame callback, with source file/definition line, retained frame name, event when applicable, Lua status and current Lua heap KB |
| `ui.layout_solve_commit`, `ui.update`, `ui.render_prepare_submit` | Layout commit, UI updates and CPU-side render preparation/submission |
| `world.scene_update`, `world.scene_prepare`, `world.render_prepare_submit`, `world.network_dispatch`, `world.publish_staged_assets` | CPU-side world work and ready-resource publication |
| `worker.queue_wait`, `worker.task`, `worker.sync_task` | Task queue latency, worker execution and synchronous fallback execution, with task/resource name |
| `vfs.resolve_read`, `mpq.read`, `mpq.read_prefix` | Resource resolution/read. MPQ records include archive, path, bytes, result and time waiting for the archive mutex |
| `world.wmo_read_parse` | WMO root/group CPU preparation |
| `texture.prepare_read_decode`, `texture.loader_read_decode`, `texture.decode`, `texture.pump_uploads`, `texture.commit_upload` | Texture source acquisition/preparation, decoding and CPU-side upload submission; not measured GPU completion time |

The loading gate's readiness fields use `1` for ready and `0` for pending.
The `texture_pending`, `texture_prepared`, and `texture_failed` fields are queue
or cache counts, not elapsed times. The final `LoadDefaultUI complete` message
also reports total milliseconds, materialized frame count and texture
validation source reads/cache hits.

## Frame summaries and overhead

`frame.summary` is emitted every five seconds while the foreground loop runs.
It reports window FPS, per-phase averages, peak work time, current render scale, frame cap,
output dimensions, draw counts, texture/render-target memory, and retained UI
layout counters. Phases are measured directly around existing work:

- `events`: window/input event handling, including synchronous input callbacks.
- `glue_requests`: Glue requests and world-entry handoff.
- `scheduler`: scheduled updates.
- `layout_beginframe`: focus/hover/layout and renderer frame setup.
- `glue_scripts`, `glue_prepare`: Glue callbacks, streaming and scene/layout preparation.
- `render`: the client render path, including the world tick and world UI work.
- `pacing`: intentional FPS pacing; separated from `work_wall_ms`.
- `endframe`: renderer end-frame/submission wait and notification draining.

`frame.hitch` identifies a loop with at least 50 ms of work after subtracting
intentional pacing. Its span duration includes the entire loop; use the
explicit `work_wall_ms` and phase fields for attribution. Background suspension
is excluded from the summary window. GPU timings are delayed bgfx samples,
not synchronous measurements of the logged frame. Unavailable timing is
reported as `unavailable`, never interpreted as zero GPU cost.

iOS summaries also include physical memory footprint (`-1` when unavailable,
with `memory_query_status`), thermal state (0 nominal, 1 fair, 2 serious,
3 critical), and low-power mode. These queries inspect only this process and
system status, without resolving UI or forcing GPU synchronization.

Ordinary spans below 20 ms are omitted. Each site's first slow call and calls
of at least 250 ms are retained; other repeats are limited to one per second,
and the next retained record includes the suppressed count. Queue-wait spans
start at 100 ms and remain rate limited even when severely delayed, since a
backlog can release many already-delayed tasks together. Milestones and root
visibility transitions are recorded at their actual boundaries. Context is
capped at 2 KiB. The log does not include Lua source text, chat contents,
passwords, session keys or packet payloads. Script metadata is inspected only
after a slow callback; the ordinary fast path does not enable Lua CPU hooks.

## Device acceptance and collection

Use the `ios-device-development` Xcode project with Release selected and
**Debug executable** unchecked. The compiled app is
`build/ios-device-development/apps/client/Release-iphoneos/OpenWoW.app`.
Use build-12340 Data with the `zhCN` locale archive chain and the configured
compatible server. Preserve the existing app container and Data.
Use `performanceLog=1` before the world-entry attempt; it is already the iOS
default. Do not attach a debugger or run a GPU capture for this collection.

1. Start the client fresh and enter the same character/world that feels slow.
   Note the time of entry and wait about 15 seconds after the loading screen
   disappears, so post-entry streaming is represented too.
2. Open and close the map, calendar, bags and merchant panel in that order.
   Open each twice, leaving about three seconds between operations. For the
   merchant, use the same NPC and compare both interactions. Record which
   first/second opening visibly stalls; root-frame names and callback/event
   context will locate the corresponding records.
3. In a fixed scene/camera, compare 100%, 75% and 50% world resolution, leaving
   ten seconds at each setting. Avoid changing other graphics options in this
   pass. Record FPS and any visible difference in stutter.
4. Exit normally and return the latest session from `log-start` / `Client
   startup` through all operations. Include the ordinary warnings/errors,
   device model, iOS version, first/second-opening observations, and whether
   the device was already hot or in low-power mode. A second restart can
   distinguish first-process initialization from persistent cache effects.

Compilation and signature checks do not validate actual interaction, loading
duration, timing availability, logging overhead or thermal behavior. Those
remain device acceptance items. Missing slow-span records do not prove that a
stage is free: sub-threshold work, suppression, and time spent waiting for
external state must be considered alongside summaries and gate snapshots.

## Minimap source diagnostics

WMO minimaps can use either room textures or a combined map covering the whole
WMO root. The combined source uses the root bounds and a synthetic texture group
equal to the authored group count; it does not request a nonexistent group file.
Only the ordinary portal traversal needs intersecting room groups to be resident.
Its portal query uses the active room's ceiling after the world-to-local transform.

The indoor zoom radius defines two different bounds. The refresh cell is snapped
in world space and is one radius wide. The source query expands that cell by one
radius on every side, including Z, before transforming it into WMO-local space.
Its horizontal width is therefore three radii; the displayed circle still has a
diameter of two radii. Using the refresh cell directly for admission drops nearby
rooms and tiles that belong inside the visible circle. Candidate room loading,
portal traversal and tile admission must all consume the expanded query.

This coverage preserves the authored room connections and floor-family rules.
The active room is drawn over other admitted rooms. Source textures are prepared
together, and a pending replacement keeps the previous published tiles visible.
Legitimate unconnected rooms, other floor families and transparent map regions
remain subject to the original source and rendering rules.

For the Stormwind black-minimap check, use the current iOS Release with the
existing build-12340 `zhCN` Data and realm. Enter the same character in Stormwind,
wait for world entry, and walk between the city streets and a nearby interior.
The minimap should display the surrounding map and keep the player marker aligned
while the source changes. Export the startup-to-reproduction log using the mobile
HUD. Include `MinimapIntegration: WMO minimap`, its `root`, `group`, `tile_groups`,
`mapped`, `submitted`, `unresolved` fields and any texture loading errors. Pending
replacement textures keep the previous published source until ready. Compilation
and static resource checks do not establish device rendering or marker alignment.

For indoor coverage acceptance, use the same configuration in Shadowglen's
Shadowthread Cave. At a fixed minimap zoom, walk from the entrance into the first
room, pause, then follow a connecting passage and return across the same boundary.
Nearby connected sections inside the circle should appear without requiring the
player to enter each section. Repeat at a wider and narrower zoom, with minimap
rotation enabled and disabled, and check a building with separated floors as a
cross-check. Export the full startup-to-attempt log, including the reproduction
time and `MinimapIntegration: WMO minimap` records (`radius`, `group`, `tile_groups`,
`records`, `mapped`, `submitted`, `unresolved` and pending/failure reasons). Device
coverage, source transitions and marker alignment remain user acceptance items.

## Layout and font optimization follow-up

Texture ownership changes now queue the changed texture and update its entry
in the existing layout dependency graph. Binding a button, slider or status-bar
texture previously invalidated the entire graph, causing the next frame-tree
construction commit to resolve all retained UI objects. Local ownership changes
now use the existing incremental dependency closure. Construction commits,
geometry-query boundaries, ownership transitions and size callbacks remain in
their existing order; graph-wide invalidation still applies to teardown and
rollback. Replacing dependency edges also retains the previous scroll owner's
range invalidation, so moving a texture out of scroll content updates the old
range at the next existing layout commit.

The measurement and rendering face caches now reuse a loaded font's immutable
source bytes for other pixel sizes and styles. Each variant still has its own
FreeType face and glyph state. Reuse stays within each existing cache and VFS
revision, with the existing failure diagnostics and invalidation lifecycle.
This avoids reading and copying the same multi-megabyte CJK font again for
each newly encountered size or outline. First-use glyph rasterization and
texture uploads still require work.

For this follow-up, keep the same Release launch configuration, character,
zhCN Data and render scale. Measure fresh world entry, first and second calendar
opening, bags and item tooltips, character panel, spellbook and quest log.
Check panel geometry, button textures, scrolling and text as well as stutter.
Compare `world.loading_screen_dismissed`, `ui.script_callback`, `frame.hitch`,
`layout_full_total` / `layout_incremental_total`, and repeated font `mpq.read`
records against the prior session. If disconnected, include the earliest
network warning through the return to login. Runtime speedup and complete
removal of first-open stalls require device confirmation.
