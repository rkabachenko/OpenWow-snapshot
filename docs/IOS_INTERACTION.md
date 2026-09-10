# iOS Interaction

OpenWoW's iOS input is a native multi-touch path, not SDL's synthetic mouse
stream. UIKit supplies the device safe area, haptic feedback and the system
log-sharing sheet. The game HUD remains FrameXML/Lua rendered by OpenWoW, so it
shares action, cooldown, target, secure-execution and panel behavior with the
desktop client.

## Touch map

| Surface | Gesture | Result |
| --- | --- | --- |
| FrameXML button or hyperlink | Short tap | Original left-button down/up; wait 260 ms after release only when a secondary action is supported |
| FrameXML button or hyperlink supporting right click | One-finger double tap on the same target | One right-button down/up on the second release; no preceding left click |
| FrameXML button or hyperlink | Hold for 475 ms without moving | Hover/tooltip only; release keeps the tooltip until the next contact or cancellation |
| Registered draggable frame | Hold for 475 ms, then move | Original left-button `OnDragStart`; release delivers `OnDragStop` and `OnReceiveDrag` |
| Slider, edit box, title region, model or color picker | Touch and drag | Immediate native pointer interaction, including text selection and slider updates |
| UI with a mouse-wheel owner | Vertical swipe before the hold threshold | Scroll through the original wheel handler |
| Visible stick, exposed left movement area | Touch to reposition the stick, then drag from that point | Forward/backward plus horizontal strafe, with a 20% axis dead zone; retains capture outside the control until release |
| Empty world | Drag | Camera freelook |
| Empty world | Tap | Select or confirm the current ground-target action after the 260 ms double-tap window |
| World exposed outside UI controls | One-finger double tap in the same location | Direct right-click interaction on the second release |
| World exposed outside UI controls | Hold for 475 ms, then release | Keep the world hover/tooltip until the next contact or cancellation, without interacting |
| Utility drawer | Tap Zoom in / Zoom out | Adjust camera distance; simultaneous world touches do not zoom |
| Utility drawer | Tap Logs / 导出日志 | Prepare a copy of the client log and open the iOS system share sheet |
| Mobile action button | Tap | Runs the corresponding action on the current action-bar page |
| Touch launcher beside the minimap | Tap | Hides or restores the combat HUD and utility drawer; the stick and virtual mouse stay independent |
| Utility drawer | Tap Hide stick / Show stick | Toggle the movement area and stick, saving the choice for subsequent launches |
| Utility drawer | Tap Mouse / 虚拟鼠标 | Show the virtual mouse control; tap Mouse off / 关闭鼠标 to close it |
| Virtual mouse lower pad | Drag | Move the PC cursor without pressing a mouse button |
| Virtual mouse upper left/right button | Press, optionally drag, then release | Immediate PC left/right down, motion and up at the cursor, including window resizing and addon drag handlers |

With the stick enabled, the left 40% of its safe area activates a floating stick.
The HUD authors a background frame marked `__ow_touch_movement`, with
`__ow_touch_movement_control` naming the visible stick whose resolved size
sets the movement radius. The ordinary topmost hit preserves occluding UI,
clipping and safe-area ownership. A touch relocates the stick center to its
starting point; dragging beyond the 20% axis dead zone starts movement.
The left activation area and stick stay active when the combat HUD is collapsed.
Use Menu → Hide stick when selecting or interacting with world objects in that
area. This separate switch releases held movement immediately and is persisted
as `OpenWoWMobileMovementShown`; the default is visible.

The movement capture is separate from the UI pointer, so skills, panels and
camera gestures can use another finger in either arrival order. A second finger
on the occupied stick is consumed. A finger beginning in the world keeps its
world/camera role even if it subsequently crosses the stick. A movement finger
keeps its role outside the stick until release; it never turns into a world click.
Hiding the control or an ancestor, disabling its mouse input or releasing its
Lua binding cancels movement immediately through the frame input lifecycle.
Restoring the stick does not recapture a finger still down from before hiding.
Application deactivation, focus loss, UI-mode changes and touch cancellation
all release movement, camera and UI capture explicitly.

## Right click, inspect and drag

For precise resizing or addons that start dragging directly in `OnMouseDown`,
enable **Menu / 功能 → Mouse / 虚拟鼠标**. A 144-by-104-point rounded control sits
below the cursor: left and right mouse buttons occupy the upper half, and the
lower half moves the cursor. Dragging either upper button moves the cursor
while holding that button; releasing ends the operation. Lift and touch the
lower pad again to continue positioning over a long distance. The cursor uses
the existing desktop cursor selection and textures, including item cursors.
The panel stays inside the safe area and flips above the cursor near the bottom.
It survives collapsing the combat HUD and fullscreen panels hiding `UIParent`.

The virtual mouse starts off on login and `/reload`. One finger owns its pad or
button until release; another can still use the movement stick. Other direct
UI/world fingers are ignored during that indirect contact so they cannot steal
the drag. Outside a virtual mouse contact, the existing direct gestures remain
available. A physical mouse takes over and closes the virtual control.
Control removal, backgrounding, focus loss, viewport changes and UI teardown
cancel held buttons without a synthetic click or drop. The next press begins a
new operation. `GetCursorPosition` and `IsMouseButtonDown` share the actual input
state throughout a drag, including between motion events. No touch hold timer
or double-tap recognition delays these explicit mouse buttons.

One-finger double tapping is the direct secondary action for UI and world
targets. Tap and release, then touch the same target again within 260 ms and
release within another 260 ms. The second touch must start within 24 device
points of the first. UI also requires the same frame reference and hyperlink
identity, so neighboring items or links do not form a double tap. The original
right-button down/up or world right-click path runs exactly once on the second
release. The first tap is consumed; it never activates the target first.
A second simultaneous world finger is ignored for its entire contact lifetime.
The first retains its camera or tap ownership. Camera distance is controlled by
Zoom in / Zoom out in the utility drawer.

A single tap on a UI target supporting right click waits for that recognition
window before sending the original left-button down/up. Left-only controls,
including the mobile HUD's skills, jump, target buttons and minimap launcher,
respond on release without this extra wait. Rapidly tapping them keeps sending
left clicks. Secondary support comes from the original registered click phases,
raw mouse handlers or hyperlink handlers, not guesses about a page or item.
World selection and ground-target confirmation also wait 260 ms; double tapping
uses the normal world right-click behavior, including cancelling targeting.

The HUD's Use/交互 button invokes `InteractUnit("target")` and therefore acts on
the selected target. Its tooltip explains the separate world gesture. To gather
a quest object, open a chest or use a world object that cannot be selected,
double tap the visible object itself. The world click picks a GUID under the
touch and passes it directly to object interaction; it does not first select it
or substitute the current target. GameObject activation retains the existing
range, lock/skill, quest and server checks and the normal game-object use/report
requests. The same path works with no target or with an unrelated unit selected.

A second touch on a different target commits the previous single tap, then
starts an independent gesture. If that first action changes the next target's
visibility or hit ownership, the next contact is consumed. For a matching
second touch, moving beyond the existing slop cancels the click or begins an
eligible scroll; holding for 475 ms enters inspection and can continue into a
registered drag. Neither path sends the first tap. Releasing the second touch
between the double-tap and inspection thresholds cancels it without a click.

The input router owns one pending completed tap shared by UI and world input.
SDL event timestamps determine touch duration and double-tap matching; the UI
update uses the same clock to expire single taps. Processing a queued event
batch after a slow frame does not make distant taps into a double tap. Pending
work is removed before invoking Lua and rechecks frame identity, visibility,
hit ownership and hyperlinks before dispatch. Its hardware-action scope comes
only from the completed physical tap, never from a hover or an arbitrary timer.
Focus loss, rotation, UI reload, target teardown and mouse takeover discard
pending taps. Existing movement, camera and direct-control ownership is
preserved; starting a camera drag cancels its world tap. Pressing the
movement control commits an earlier completed single tap as a separate input,
then revalidates the control before capturing the movement finger.

Long-press inspection never sends a left-button down, click or right-button
action. This matters for buttons registered for down-clicks: inspecting an
item must not equip/use it, and inspecting a spell must not cast it. Short taps
still use the original down/up sequence after recognition, with immediate
pressed feedback while the finger is down. An early move beyond 9 device points
cancels the tap or scrolls an eligible UI; it does not rearrange action bars accidentally.
Continuous native controls (sliders, edit boxes, title regions, model views
and color pickers) keep their immediate pointer path.

After a hold is released, the tooltip remains visible without an action panel.
Inspection retains only the hovered target, with no pending click or world
action. The next contact clears that hover and begins its own normal gesture;
it is not consumed solely to dismiss the tooltip. A short tap performs the
primary action and a double tap performs the supported secondary action.
Inspection works with the combat HUD hidden. Mobile action icons supply their
normal action tooltips too.

For a drag, keep the inspecting finger down and move it before releasing.
Only frames with a registered left drag and an
`OnDragStart` handler enter this path. The router publishes actual drawable
cursor coordinates and the left-button input context before invoking the
existing drag handlers. Original action-bar locking, pickup restrictions,
secure execution, cursor payloads and drop handling remain authoritative.
Unsupported drags cancel without firing a click. Cancellation ends an active
drag without delivering `OnReceiveDrag` to an accidental destination.

The input router owns the pending gesture and inspected target. UI update and
incoming touch events advance gesture recognition; draw publication does not
advance a gesture. Hover itself supplies no protected-action grant. Frame identity,
visibility, hit ownership and hyperlink identity are checked again before
dispatch, so an expired or covered UI target is not replaced by whatever is
now underneath it. World inspection follows the live pointer position, as
desktop world hover does. Focus loss, rotation and UI teardown clear inspection
and its tooltip; an actual mouse movement takes over from touch inspection.

## Mobile HUD

The mobile HUD is loaded in iOS world sessions from the internal
`Interface/OpenWoW/MobileUI` TOC. macOS can opt into the same layer with the
`mobileHudPreview` CVar; it is off by default on macOS. Other desktop builds do
not load it.

- iPhone and iPad share a six-action thumb fan with a deliberate 6+6 layer
  switch. The first slot on each layer is a 68-point primary target; five
  56-point actions follow the right thumb's sweep. The layer button sits inside
  the fan and displays the active slot range, `1–6` or `7–12`. Switching layers
  preserves each slot's position and the normal action-bar page mapping.
- Jump and interact sit near the right edge; enemy and friendly targeting sit
  along the outer edge of the fan. All four remain one tap away.
- Character, spellbook, talents, quests, map, bags, chat and system panels live
  in a lower-right utility drawer with targets up to 54 points. Opening it replaces
  the combat fan, and the same menu button becomes Back. Selecting a panel
  closes the drawer. Its background absorbs touches between menu buttons.
- Menu / 功能 → Graphics / 画质 opens the world-resolution control in the same
  drawer. The slider adjusts from 50% to 100% in 5% steps; 50%, 75% and 100%
  buttons make scene comparisons quick. The panel shows the current setting
  and FPS. Changes apply on the next world frame while UI keeps its output
  resolution. Back returns to the utility buttons.
- Counts, usability tint, current-action highlight, cooldowns, current action
  page and bindings use the normal game APIs. The original target frame supplies
  target information; there is no duplicate target caption or inactive-layer
  preview covering the world. Gaps between combat buttons remain available for
  camera gestures, without a solid panel behind the fan.
- A separate 48-point Touch/Hide launcher sits to the left of the minimap,
  clear of its lower-left encounter-journal launcher. It remains visible when
  the mobile root is hidden and falls back to the upper-right safe area if the
  minimap is hidden. Hiding the HUD closes the drawer; restoring it returns to
  the combat fan. `OpenWoWMobileHUDShown` uses the normal account saved-variable
  lifecycle, including logout and UI reload.
- A visible 118-point movement stick rests at the lower left, 32 points inside
  the safe root. Its 54-point knob returns to center on release. The separate
  Hide stick menu switch releases and disables this control; its former position
  becomes ordinary UI/world input. Collapsing the combat HUD preserves movement.
  Re-enabling the stick requires a fresh press before movement can resume.
- Interactive targets are at least 48 points, labels are 11 points, and action
  counts are 12 points. Layout and text scale from device points, including the
  current safe-area insets, in both landscape orientations. Chinese labels are
  used for `zhCN`; other locales receive English labels.

The original FrameXML remains loaded and unchanged underneath the mobile HUD.
On iOS, the platform publishes the drawable size and UIKit safe-area insets
together to the world UI layout. `UIParent` occupies that safe rectangle, so
the original player/target frames, chat, action bars, minimap and add-ons
anchored to it all avoid the notch and home indicator. Screen-clamped frames
and saved/dragged positions use the same bounds. The world viewport and
world-input frame still cover the full drawable; UI scale is based on that
full drawable too, so adding insets does not shrink fonts or buttons. Insets
survive UI reload and update with orientation/window changes even when the
mobile HUD is hidden. Frames deliberately positioned outside `UIParent` or
anchored to the world viewport need their own presentation policy.

The mobile HUD adds its existing 14-point spacing inside this shared root;
it does not apply the UIKit insets a second time. The joystick anchors to the
same root and hit testing consumes its resolved drawable rectangle.
External keyboard, mouse/trackpad and controller routes stay active. Virtual
movement uses independent source ownership, so releasing one input device does
not cancel the same command still held by another.

## Export logs on iOS

Open the mobile HUD's **Menu / 功能 → Logs / 导出日志**. If the HUD is hidden,
restore it using the **Touch / 触控** launcher first. The native share sheet
offers Save to Files and the sharing apps installed on the device; save or send
the file yourself, then provide the exported `.log` file for diagnosis. This
works with development and TestFlight signing and needs no connected Mac or
debugger. `/console exportlogs` invokes the same flow. The export button is
disabled in macOS HUD preview, where the native sharing service is unavailable.

The exporter copies the entire current `openwow-client.log`, including earlier
app sessions and UI reloads. It queues a flush boundary behind existing log
entries and copies that exact byte prefix in 64 KiB chunks on a background
worker. Live logging continues. The file is named
`OpenWoW-iOS-<UTC timestamp>-<identifier>.log`; a preparation entry records the
app version/build. No Data archives, account configuration or other files are
included. Preparation and sharing failures have `Log export` diagnostics and
a visible error when the presenting screen remains available.

Export preparation and sharing allow only one operation at a time. Opening
the native UI releases held game input; it does not retain the current Lua
runtime. Temporary copies remain available until sharing completes or is
cancelled, then are removed. A later export removes copies left by a terminated
process. Selecting Save to Files creates a separate copy at the chosen location.
The iPad share sheet is anchored to the active game view's bounds.

For device acceptance, use an iOS Release build containing this change with the
normal build-12340 `zhCN` Data and compatible realm. Reproduce the reward issue,
tap a reward and Complete Quest once, then export before `/reload`. Verify that
the received file contains the preceding startup, `ui.touch_release`,
`ui.pointer_click` and `quest reward selection` entries. Also cancel sharing and
export again, and verify Files export on both iPhone and iPad. Keep the original
log through acceptance; native presentation, cancellation and delivery require
device confirmation.

## UI scale

On iOS, the original video settings UI scale slider supports **0.64–1.50**, in
the original **0.01** steps. Enable Use UI Scale/使用 UI 缩放, then move the
slider above 1 to enlarge the original frames and text. A value of 1.20 makes
them 20% larger than scale 1. The default remains 1 with custom scaling off;
Apply/Okay, Cancel, defaults and configuration persistence use the original
FrameXML paths. Dragging retains the existing immediate preview behavior.

The extension publishes an iOS-only `GetCVarMax("uiScale")` result before the
original options loader sets up the slider. Both login-screen and in-world
video settings use that same range, so a saved 1.20 is represented without
clipping it to 1 during refresh. No original XML/Lua override or generic slider
change is needed. Desktop range-query behavior and the 0.64–1 scale computation
remain unchanged, including when macOS mobile HUD preview is enabled.

The upper bound is the supported slider range, not a new clamp on `SetScale`
or console CVar writes. Values manually entered outside 0.64–1.50 or between
the slider's steps are not exactly representable by this control. The mobile
joystick, skill fan and launcher keep their device-point sizes as the original
UI grows; touch hit testing uses the same resolved geometry as rendering.
The configured scale belongs to `UIParent` and is inherited by its children.
Parentless fullscreen frames use their own scale in layout, rendering, geometry
queries, hit testing and saved placement. `SetupFullscreenScale` also fits the
standard fullscreen guide inside the viewport's safe bounds. Opening the world
map therefore keeps its close and size controls reachable at larger HUD scales;
the original map's Lua/XML and its fullscreen HUD visibility behavior remain in
charge of the page.

For map acceptance, use the current iOS Release with build-12340 `zhCN` Data and
the existing realm. Open the map from the mobile utility drawer at default UI
scale and at 1.50, close it using the map's close button, then repeat after
switching between windowed/fullscreen map modes and after `/reload`. Check both
landscape directions on a device with safe insets. The expected result is a map
that fits the safe area, reachable controls and restored HUD after closing.
Return the exported log from startup through the attempt, especially
`HUD viewport safe insets`, `ui.fullscreen_scale` (the native scale request),
`ui.touch_release` and `ui.pointer_click`. Actual device layout and touch delivery
require user confirmation.

## Iterate the HUD on macOS

Build and install the normal Release application using [BUILDING.md](BUILDING.md).
In world, enable the preview with these two chat commands:

```text
/console mobileHudPreview 1
/reload
```

The setting is saved in the normal client configuration. To disable it, run
`/console mobileHudPreview 0` followed by `/reload`. The switch is consumed
when the world UI starts or reloads; it does not create or destroy frames in
the middle of a Lua callback. iOS continues to load its HUD unconditionally.
The existing reload lifecycle recreates FrameXML/Lua while retaining the
current world session, so preview changes do not require another login or
world load.

For source iteration, close the running client, then start the installed app
with the existing override-root environment variable. From the repository
root, run:

```sh
OPENWOW_ENHANCED_ASSETS="$PWD/assets/overrides" \
  ./build/macos-app/OpenWoW.app/Contents/MacOS/openwow-client
```

Edit `assets/overrides/Interface/OpenWoW/MobileUI/OpenWoWMobileUI.lua`, save,
and run `/reload` in game. The UI loader starts a fresh source-cache lifetime
and reads the changed file from the mounted directory; Lua, XML and TOC edits
do not require a build or reinstall. The environment variable must point to
the whole `assets/overrides` root, not its `MobileUI` subdirectory. Without it,
an installed app reads its packaged overrides, which change only after an
install. C++ changes still require compilation and restarting the app.

Use a landscape window sized in macOS logical points, for example about
844 by 390 for a phone layout or 1194 by 834 for a tablet layout. HUD sizes use
those window points, while drawing and mouse hit testing use framebuffer
pixels; Retina scaling is accounted for. Resizing and UI reload republish
the viewport metrics. Desktop safe-area insets default to zero. With preview
enabled, `/console mobileHudPreviewSafeInset 44` simulates a uniform 44-point
inset on all four sides, including the original HUD. Changes apply without a
reload; set it to `0` to restore the full window. This setting is macOS-only
and does not replace the device's actual safe area. This previews the
shared layout, labels, action layers, cooldowns and panel interactions;
device safe areas, multi-touch ownership, haptics and thumb reach still need
an iOS device. Desktop keyboard and mouse keep their usual behavior.

For user acceptance, use `build/macos-app/OpenWoW.app` with build-12340 Data,
the `zhCN` locale archive chain and the configured compatible realm. In world,
enable the preview and verify that the HUD appears; check both action layers,
the utility drawer and Touch/Hide, resize the window, then change a Lua label
or spacing and reload to confirm the source edit appears without leaving the
world. Disable and reload to verify the mobile frames disappear. If the HUD
was previously hidden, restore it with the minimap's Touch/触控 launcher.

The log should contain `macOS mobile HUD preview loaded (mobileHudPreview=1)`
after each enabled UI start. Verify that `Override content root:` names the
source directory for live editing. For failures, return the latest session
and reload span from the resolved user-data root's `logs/openwow-client.log`
(normally `~/Library/Application Support/OpenWoW/logs/openwow-client.log`),
including the first `OpenWoWMobile` or FrameXML/Lua error and its context.
These visual, interaction and reload checks remain user acceptance items.

## World rendering resolution

`renderScale` is an archived CVar shared by the world renderer on every
platform. iOS defaults to `0.75`; desktop defaults to `1.0`. The iOS performance
profile does not overwrite the user's value. The normal `Config.wtf` lifecycle
preserves it across UI reloads and client restarts. Invalid, non-finite and
out-of-range inputs are rejected with a console message and a diagnostic;
invalid saved values are reconciled to the platform default at registration.

Entering the background also snapshots account and character CVars into their
existing `config-cache.wtf` files before suspension, without requiring logout
or a live server connection. This includes `autoLootDefault`, whose account
scope excludes it from the device's `Config.wtf`. The existing account-data
sync metadata preserves the normal upload and restore path. Background-save
failures are logged as `Runtime configuration` with the phase and reason;
device CVars are still saved independently when an account-cache write fails.

Use Menu / 功能 → Graphics / 画质 on the mobile HUD, or enter
`/console renderScale 0.75` in chat. Values from `0.5` through `1.0` are accepted;
`/run print(GetCVar("renderScale"))` reads the setting. Only world scene targets
are scaled; output resolution, UI sizing and touch coordinates keep their
existing behavior. The regular graphics resolution setting still controls
the output window/display, independently of this scale.

For this control's device acceptance, use the signed Release
`build/ios-device-development/apps/client/Release-iphoneos/OpenWoW.app`,
build-12340 Data with `zhCN`, and a character in the configured compatible realm:

1. Keep the same scene and camera, then switch between 100%, 75% and 50% and
   drag the slider through intermediate steps. The caption and world sharpness
   should update without reloading UI; UI text and touch targets stay unchanged.
   Compare the displayed FPS after each setting settles. A CPU or frame-cap
   limit may keep FPS unchanged.
2. Use Back to return to the utility menu, reopen Graphics, rotate the device,
   then hide/restore the HUD. The current setting and controls should remain
   usable. Reload UI and restart normally; the chosen value should persist,
   including while `iosPerformanceProfile` remains enabled.
3. If behavior differs, return the latest session's `logs/openwow-client.log`
   from the resolved user-data root, from `log-start` through the switches.
   `PostProcess: world render targets` records the scale, output and capture
   dimensions, sample count and allocation status; `CVar validation` identifies
   rejected inputs. These are persistent diagnostics. Touch behavior, visual
   quality, restart persistence and performance gains require device feedback.

## Device acceptance

For Release performance and first-open stalls, use the focused collection
procedure in [IOS_PERFORMANCE.md](IOS_PERFORMANCE.md).

Use the signed Release `ios-device-development` build at
`build/ios-device-development/apps/client/Release-iphoneos/OpenWoW.app`, with
build-12340 Data and the `zhCN` locale archive chain already synchronized into
the application container. Use the configured compatible realm and an in-world
character with actions assigned to slots 1–12. Validate both landscape
orientations on iPhone and iPad, plus background/resume. Static/build checks do
not establish device visual, interaction or comfort acceptance.

1. On login, realm and character screens, tap buttons, edit fields and scroll
   or drag controls; no duplicate click should occur.
2. With the HUD visible, touch several exposed locations in the left movement
   area: the stick must move to the touch and remain idle until dragged away
   from its new center. Release a stationary touch and wait several frames;
   the stick must stay at that position until another touch or a viewport change.
   Drag diagonally, reverse and return to center; release
   inside/outside the stick must stop movement without a world click. Cover
   the activation area with a UI window and verify that UI receives input.
   A second movement finger must not steal the stick. Use Menu → Hide stick and verify
   the same left-side locations select/interact with world objects again.
3. Hold movement while dragging the camera and activating an action, in both
   arrival orders. Pinch two world fingers: camera distance must remain fixed.
   Open the utility drawer and tap Zoom in / Zoom out repeatedly; only those
   buttons change camera distance, and the drawer remains open.
   Check the smaller combat buttons and tighter utility grid for readable
   labels and distinct touch targets, including the graphics drawer.
4. Tap a world unit and confirm a ground-target spell with a short world tap.
   Double tap an NPC/object, including lower-left world with the HUD hidden: expect right-click
   interaction on the second release, with no toolbar or preliminary left-click
   selection/cast. Double tap with a ground-target spell active to check the
   normal right-click cancel. A single tap waits about 260 ms before selection
   or ground-target confirmation. Pinching must never produce a right click.
   Reload/background between taps and during the second touch: releasing the
   old contact must not trigger an action in the resumed/reloaded UI.
   Hold an interactable in any exposed world area, then release: its normal
   tooltip should remain without an action panel or interaction. Double tap
   the object afterwards to verify normal NPC/object interaction. A new
   contact in the movement area must immediately relocate the stick and clear
   the tooltip. An ignored extra finger must not produce a second action.
   Find a quest gathering object that cannot become the selected target. Stand
   within use range, clear the target and double tap the object itself; expect
   its normal use/loot/cast response and the corresponding quest progress after
   the server responds. Repeat with an unrelated NPC selected, with the object
   in the lower-left region, and with the mobile HUD hidden. The selected NPC
   must not receive the object's interaction. Repeat with an ordinary chest or
   another usable GameObject, and check an out-of-range interaction still follows
   the existing approach/error behavior. A scene model obscured by a UI control
   remains a UI hit; hide the mobile HUD or move the camera to expose it.
5. Verify both action layers on iPhone and iPad, including cooldown, count,
   usability and action-page changes. Check that the thumb can move from the
   primary action to jump, the layer switch and the surrounding skills without
   triggering an adjacent button. Enemy/friendly targeting stays one tap away.
6. Tap the minimap's Hide/收起 launcher while the combat fan is shown, then
   repeat with the utility drawer open. The combat fan and drawer must disappear;
   operate the exposed original action bars, bags and panels. Touch/触控 must
   restore the fan with current icons and cooldowns, with the drawer closed.
   Repeat while another finger holds movement: collapsing the HUD must preserve
   movement. Use Menu → Hide stick: movement must stop immediately; showing the
   stick again while that finger stays down must not restart movement or make
   its release click the world. Drag over the former stick position while
   disabled: expect camera motion, with no invisible movement zone. Repeat during
   camera drag: that camera contact must continue. Reload while holding the
   stick: the old finger must stop and stay inactive through its release.
   Log out/re-enter, reload UI and restart the app to check both saved visibility choices.
   Enable Mouse in the menu, hover an item, and press/drag/release each upper
   button on a window or addon resize handle. The window must follow the cursor
   and stop on release. Check all screen edges, a fullscreen map, and a second
   finger on the stick. Background/resume and reload during a drag: no stuck
   button, extra click or drop is expected. Report the addon name, gesture and
   any `Touch mouse`, `Cursor rendering failed` or preceding Lua errors in an
   exported log covering startup through reproduction.
   Hide/show the minimap and check the launcher remains
   reachable inside the safe area, without covering the journal launcher.
7. Open every utility panel. The drawer must replace the fan; tapping its gaps
   must not trigger world actions, and Back/返回 must restore the fan. Check
   original windows and dialogs with the HUD shown and hidden, then repeat
   the core checks with external keyboard/mouse/controller input.
8. Background the application while moving or dragging the camera. Resume must
   not leave movement, freelook, pressed visuals or text input stuck.
9. On a notched iPhone, check the original player/target frames, chat, action
   bars, minimap and opened panels with both landscape orientations. The HUD
   should move inside the current safe area while the world continues behind
   it to the screen edges. Repeat with the touch HUD hidden and after `/reload`;
   check that touch hit targets and joystick visuals stay aligned. Drag a
   screen-clamped panel to each edge and reload to check its saved position.
   Repeat the drag/reload check with a custom UI scale enabled.
   On iPad, confirm that zero side insets do not create artificial side gaps.
10. Hold an inventory item, a spell/action button, a unit frame and a chat
    hyperlink. Inspect their normal tooltip without activating them; release
    must keep the tooltip with no Click / Right click panel. The first new
    tap must act normally, including on a different control, without requiring
    an extra dismissal tap. Include a button registered for `LeftButtonDown`:
    only a short tap may trigger its left action. Repeat with the combat HUD hidden.
    At a quest reward choice, hold a reward icon/name, release, then tap a
    choice: the tooltip should close and the selection highlight should update
    on that first tap. Release after inspection alone must never select a reward.
    Double tap the same targets and check their original right-click behavior
    exactly once, never a preliminary `LeftButtonDown`. Single tap and wait:
    expect one left click, with no repeat on later updates. Tap neighboring
    items/links quickly: expect separate left clicks, never a right click on
    either. Rapidly tap the left-only mobile skill/jump buttons: every release
    must respond immediately. Repeat during held movement, with custom UI scale
    and with the combat HUD hidden. Hold or move the matching second touch:
    expect inspection/scroll/drag, with no first-tap action. Hide or replace a
    queued target, reload or background before the timeout: no delayed action
    may reach a newly exposed control. Try double taps during a slow frame and
    check the recognition window remains tied to the physical touch timing.
    If a deferred click focuses an edit box, the system keyboard must appear
    without requiring another touch.
11. With normal pickup permissions and action-bar locking configured for the
    intended drag, hold a draggable item/action then move to a valid slot.
    Verify pickup and drop exactly once, with no click/cast afterwards. Moving
    before the hold threshold must not start that drag. Cancel a drag by
    backgrounding or rotating and check that no unintended drop occurs.
12. Swipe a scrollable list/chat view; drag a slider, text selection, title
    region and model control. Check hit/cursor alignment at device scale,
    including during simultaneous movement with the other finger. Hide or
    replace the inspected frame: inspection must end without activating a
    newly exposed control. Reload/background while a tooltip is pinned and
    verify it is cleared.
13. In the original video settings, enable UI scaling and exercise 0.64, 0.80
    and 1.00 before trying 1.20 and 1.50. Apply each value, reopen the settings,
    then change it and Cancel: the previous applied scale must return, including
    transitions in both directions across 1. Open the panel at a saved 1.20
    and Apply without dragging: it must stay at 1.20. Repeat after `/reload`
    and logout/login, including opening the login-screen video settings before
    re-entering the world. Defaults and disabling custom scale must retain
    their original behavior. With each scale, check text, panel/button hit
    alignment, dragging, tooltips and minimap placement in both orientations;
    mobile controls must keep their device-point size. Repeat the 0.64–1
    checks on desktop, whose slider remains unchanged. Scaling, panel fit and
    apply/cancel behavior remain device acceptance items until confirmed.

For configuration persistence, enable auto-loot in the stock interface options,
apply the change and enter the background without logging out. Terminate the
backgrounded app, launch the updated build and enter the same character: the
checkbox and actual auto-loot behavior must remain enabled. Repeat with it
disabled, with a changed world render scale, and with the connection unavailable
before backgrounding. Include both sessions and the interval containing
`Application entered background`, `Runtime configuration`, `AccountData` and
`CVarSystem` when reporting a reset or save failure.

The log records `iOS internal mobile interaction layer loaded` after the
internal TOC succeeds. A missing or invalid mobile layer is a world-UI startup
error rather than a silent fallback.
`Touch movement cancelled` records control/lifecycle cancellation with the frame,
input source and reason. Include it for hidden-HUD, reload or stuck-movement
failures; normal finger release does not require a cancellation entry.
`HUD viewport safe insets` records changed drawable dimensions and the
left/top/right/bottom insets in framebuffer pixels. Include those entries when
reporting notch, rotation or hit-target alignment failures.

`World secondary click` records each dispatched coordinate-based right click,
the drawable touch coordinates, hit kind, picked GUID and separately selected
GUID. `hit=gameobject` with a nonzero picked GUID confirms model picking reached
object interaction; it does not prove server acceptance. `hit=terrain` or
`hit=none` means that click did not pick the object. If this entry is absent,
include preceding `Touch action` cancellations. Existing debug-level
`interaction send CMSG_GAMEOBJ_USE` / `CMSG_GAMEOBJ_REPORT_USE` entries, when
present, identify outgoing object requests. For a gathering failure include
the quest/object name, any use/range/skill error, whether loot or casting began,
and whether the quest counter changed.

For quest reward display checks, use the signed Release build containing the
current source changes, build-12340 `zhCN` Data and the configured compatible
realm. Compare a usable reward with one whose armor/weapon type the character
cannot equip, in both the quest log and the NPC reward dialog. Wait for item
queries to finish without reloading. The unusable icon/name background should
turn red; inspecting it should mark the unsupported type or specific unmet
requirement red, while keeping unrelated tooltip lines in their normal colors.
Release inspection and tap a choice: its selection highlight should appear.
Repeat after closing/reopening the panel and after UI reload, recording which
transition changes the result. These checks require device confirmation.

Quest-log item query completion queues `QUEST_LOG_UPDATE` after the record is
readable. `quest item display query failed` identifies the item and log/dialog
source when completion fails. `item proficiency updated` records each accepted
server class/subclass mask replacement; `item proficiency update rejected`
identifies malformed packets. Zero masks retain the normal no-gate semantics.
`Frame input method failed` records failed pointer-driven `Click`/`SetValue`
calls independently of the Lua error popup setting. Include these entries and
the character class/level, quest name and item name or ID when reporting reward
selection or tint failures; preserve the login-to-failure log span so the
original proficiency updates remain available. These are permanent diagnostics.

For an unresponsive reward choice, preserve the failure before running
`/reload`: tap a reward, then tap Complete Quest once. With the existing iOS
performance logging enabled, `ui.touch_release` records the recognized gesture
and target, and `ui.pointer_click` records entry into the button's `Click`
method, including the modifier mask and recursion guard. Neither entry alone
proves that the Lua `OnClick` handler changed the selection. A subsequent
`ui.quest_item_refresh` identifies a query-driven reward refresh, which the
stock UI uses to clear its selection. `quest reward selection` records the
missing-choice error's retained `itemChoice`/`chooseItems`/`questLog` fields or
the accepted one-based choice at the outgoing-request boundary; a request is
not server confirmation. These diagnostics do not invoke Lua getters, force
layout or change reward behavior, and remain part of the maintained logging.

For a failed device check, return `logs/openwow-client.log` from the resolved
iOS user-data root, covering the latest `log-start`/`Client startup` through the
failure, together with the device, orientation, HUD visibility/layer and steps.
Keep the first Lua/resource error and surrounding context, especially entries
mentioning `OpenWoWMobile`, `RegisterForSave`, `SetFont` or `SavedVariables`.
For scaling failures, also report `GetCVar("useUiScale")` and
`GetCVar("uiScale")` before opening, after dragging and after Apply/Cancel,
plus whether the issue occurred in login or world settings. Include the first
FrameXML/Lua error and `HUD viewport safe insets` entries from that interval.
For double-tap/inspection/drag failures also include `Touch action`,
`source=double-tap`, `source=single-tap-timeout`, `Touch drag`,
`Touch scroll`, and the first originating FrameXML/Lua error. Record whether
the failure occurred on the first/second tap or release, at the single-tap
timeout, before the hold, during inspection, on the next contact, at drag
start, on drop or on cancellation.
Device overlap, finger comfort, multi-touch timing and reload/resume behavior
remain user acceptance items until confirmed on this build.

## Gameplay checks after the September 9 fixes

Use the current Release client with build 12340 Data, the `zhCN` locale archive
chain, the usual server, and sound/effects enabled. These are device acceptance
checks; compilation does not establish the visual, input or audio result.

| Area | Steps | Expected result |
| --- | --- | --- |
| Hearthstone binding | Visit a different inn while still bound to the old one; request a new bind before confirming | Confirmation names the destination area; cancelling preserves the old bind |
| Camera zoom | After a fresh launch, tap Menu → zoom in/out several times without using a keyboard or mouse | Each tap moves the camera in the requested direction within the configured distance limits |
| Settings | Change auto loot and outdoor/indoor minimap zoom; background the app, close it, and log in to the same character; also reload the UI | Auto loot and both minimap zoom values are retained; indoor/outdoor transitions select their corresponding zoom |
| Quest prose | Open quest details, progress, rewards and the quest log with male and female characters | `$G` gender branches and `$T` rank/sex branches show the selected text; player/class/race substitutions use the active character and locale |
| Release spirit | Die during the current session without reconnecting; tap Release Spirit | The stock death popup appears and release enters ghost form; resurrection clears the popup |
| Auto attack | Select a living hostile target, approach melee range, then tap Interact or the ordinary attack action once | Ordinary swings continue until stopped or the target becomes invalid; selecting a target alone does not start attacking |
| Combat audio | Let several creatures attack and exchange ordinary hits, misses and blocked attacks | Swing audio does not suppress subsequent creature/contact/impact sounds; the correct result type selects the feedback |
| NPC voice | Interact with voiced NPCs and close their dialogs, allowing each line to finish | Greetings and farewell lines play from the NPC's voice set |
| Flightmaster marker | Approach the Rut'theran Village flightmaster with a character that has not learned that flight point | An unlearned point has a green exclamation mark; report whether the point was already known if the marker is absent |

If a check fails, export the log with Menu → Export Logs after the attempt.
Include the latest `log-start`/`Client startup` through the failed interaction,
and identify the character location and action. Useful entries are
`DeathManager` (including `release_seconds`), `Runtime configuration`,
`AccountData`, `Minimap zoom`, `Quest text`, `Object text`, `NPC sound`,
`Taxi node status`, `NPC overlay`, and any preceding Lua, audio or resource error.
The Rut'theran marker issue still needs a device log to distinguish the server's
learned-point state from model attachment or rendering failure.
`Quest text` records the server packet, quest ID,
active locale and server-authored title, so an English quest can be traced even
when its name was not recorded separately. Full English quest prose still needs
a concrete sample to distinguish missing server translation from cached text.
