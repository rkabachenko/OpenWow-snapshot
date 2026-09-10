# Compatibility audit and acceptance matrix

OpenWoW has one lightweight cross-system audit for compatibility boundaries
that are easy to regress while changing otherwise unrelated code. Run it from
the source root without configuring or starting the client:

```sh
cmake -DOPENWOW_SOURCE_DIR:PATH="$PWD" -P cmake/CompatibilityAudit.cmake
```

An already configured build also exposes the equivalent target:

```sh
cmake --build --preset release --target openwow-compatibility-audit
```

The audit tokenizes production sources after removing comments, so legal
formatting changes do not affect it. It checks ordered call boundaries and
explicit ownership markers rather than source line layout. The same script
runs in the Linux, macOS and Windows CI lanes.

## Contracts covered

| Area | Static contract | Runtime evidence still required |
| --- | --- | --- |
| Resource provenance | DBC and font reads use the active VFS, bind caches to its revision, and retain winning-source diagnostics. | The intended build-12340 `zhCN` archive wins for each logical path; overrides and patch overlays display their actual content. |
| Complete replacements | Object batches validate packet-local types before mutation; gossip, quest and loot snapshots publish only after complete parsing. | Consecutive interactions replace old rows and targets without stale UI. |
| Animation ownership | Passive selectors, explicit M2 requests, completion callbacks, authored effect lifetimes and blend-source endpoints remain distinct. | One-shots finish once, locomotion does not replay them, and authored effects retire at animation completion. |
| Lua event sequence | Changed fields map through build-12340 slots before per-unit/global publication. | Stock UI changes once and in the expected order for health, power, XP, money and quest transitions. |
| UI publication | Geometry-resolving diagnostics are explicit; hover replay follows layout and traversal publication. | Stationary-pointer hover state follows frames that appear, disappear, move or change mouse eligibility. |
| Failure handling | Parse logs retain stage/input context and independent renderer restores are all attempted. | A local failure is visible in the log and does not silently truncate an otherwise independent result. |

This is a regression guard, not proof of original-client behavior. It catches
removal, reordering and accidental bypass of the established boundaries. It
does not infer protocol semantics from identifier names and does not replace
the runtime matrix below.

## One-time user acceptance matrix

Use the installed Release bundle at `build/macos-app/OpenWoW.app`, build 12340
Data, `zhCN`, and the normal configured content root. Keep each run's complete
artifacts and isolate the relevant log session by its latest `log-start` and
`Client startup` records.

| Pass | Action | Expected result | Evidence to retain |
| --- | --- | --- | --- |
| Offline production path | Run the bundle executable with `--scenario world_offline_play_regression --artifacts-dir <empty-dir>`. | Exit status 0; terrain, player, stock world UI, chat publication, movement start/heartbeat/stop and a non-degenerate final frame all pass the existing oracle. | The scenario directory, `world_oracle.json`, final capture, and the scenario log. |
| Glue/UI path | Run `glue_ui_regression`, `glue_character_create_regression`, and `glue_delete_dialog_regression` with separate artifact directories. Do not move the pointer while dialogs and mouse-enabled frames change. | Each scenario exits 0; no stale highlight, click target or dialog survives a published mutation. | Scenario artifact directories and any failure capture/UI dump. |
| Locale and provenance | Start normally with build 12340 `zhCN`; reach login, character selection and the world once. | Localized text remains Chinese where the active slot is populated; no cross-language fallback masks an overlay error. `Faction.dbc`, `SkillLine.dbc` and `Spell.dbc` report their actual winning archive. | Log lines containing `DBC localized source`, including `path`, `localized_slot` and `source`. |
| Live state replacement | On one character, successively open two different gossip/quest givers, vendors, loot sources and taxi masters; close and reopen each interaction once. | The second snapshot wholly replaces the first. No stale target, row, price, quest, loot slot or taxi route remains. Failed publication closes or preserves the previous interaction according to its operation instead of showing a partial mixture. | Screen recording or concise notes plus log lines containing `interaction reject`, `publication failed`, `quest replication` or `UpdateObject` if any occur. |
| Animation ownership | While idle and moving, trigger a non-looping emote and representative instant, channelled and travel-time spell visuals. Repeat during a blend transition. | Each requested one-shot reaches its endpoint once; passive stand/locomotion selection does not restart it; no first-frame snap appears while fading; authored effects end on their animation completion. | Short recording with action names and timestamps; any `SpellVisualRenderer` warning from the same session. |
| Lua and UI publication | Change health, maximum health, current/max power, XP, rested XP, money and a quest-log slot through ordinary gameplay. Open/close or move frames under a stationary pointer. | Stock UI publishes each resulting transition without duplicate pulses or stale values. `OnSizeChanged`-driven layout settles before the new hit target/highlight is observed. | Recording or notes identifying each transition; the complete session log if UI diverges. |
| Failure isolation | Review the latest session from its first warning/error, rather than only the final symptom. If the server naturally sends an unsupported or malformed item, continue an unrelated interaction afterward. | The first failure names stage/operation, entity or opcode, input size/source and direct reason. Independent later work continues; no apparently successful partial list is published. | The contiguous log range from the first warning through the next successful independent operation. |

For a normal macOS run without `OPENWOW_USER_DATA`, the append-only JSON-lines
log is at
`~/Library/Application Support/OpenWoW/logs/openwow-client.log`. If it is not
there, use the absolute `OpenWoW log:` path printed to stderr and verify the
resolved user-data root; the content root does not own logs.

## Known residual boundaries

- The offline oracle covers the production world/UI/render/input path but not
  live-server replacement semantics, every animation class, locale overlays or
  malformed packet recovery.
- The static audit proves that the current ownership and publication edges are
  still wired; only a runtime observation can prove timing, visuals and stock
  Lua side effects.
- Renderer device-loss recovery and long-session performance remain explicit
  milestone/release checks rather than part of this focused acceptance pass.
- No temporary tracing is required by this matrix. If a future investigation
  adds any, remove it before a production commit while retaining permanent
  contextual error diagnostics.
