cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED OPENWOW_SOURCE_DIR OR OPENWOW_SOURCE_DIR STREQUAL "")
  get_filename_component(OPENWOW_SOURCE_DIR
    "${CMAKE_CURRENT_LIST_DIR}/.." REALPATH)
else()
  get_filename_component(OPENWOW_SOURCE_DIR
    "${OPENWOW_SOURCE_DIR}" REALPATH)
endif()

if(NOT EXISTS "${OPENWOW_SOURCE_DIR}/CMakeLists.txt")
  message(FATAL_ERROR
    "OPENWOW_SOURCE_DIR does not name the OpenWoW source tree: ${OPENWOW_SOURCE_DIR}")
endif()

function(openwow_audit_fail contract detail)
  set_property(GLOBAL APPEND PROPERTY OPENWOW_AUDIT_FAILURES
    "${contract}: ${detail}")
endfunction()

function(openwow_source_tokens relative_path output_variable)
  set(source_path "${OPENWOW_SOURCE_DIR}/${relative_path}")
  if(NOT EXISTS "${source_path}")
    openwow_audit_fail("source" "missing ${relative_path}")
    set(${output_variable} "" PARENT_SCOPE)
    return()
  endif()

  file(READ "${source_path}" source)
  string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" " " source "${source}")
  string(REGEX REPLACE "//[^\r\n]*" " " source "${source}")
  string(REGEX REPLACE "[^A-Za-z0-9_]+" " " tokens "${source}")
  string(REGEX REPLACE " +" " " tokens "${tokens}")
  set(${output_variable} " ${tokens} " PARENT_SCOPE)
endfunction()

function(openwow_require_tokens contract relative_path)
  openwow_source_tokens("${relative_path}" tokens)
  set(missing "")
  foreach(required IN LISTS ARGN)
    string(FIND "${tokens}" " ${required} " found)
    if(found EQUAL -1)
      list(APPEND missing "${required}")
    endif()
  endforeach()
  if(missing)
    list(JOIN missing " | " missing_text)
    openwow_audit_fail("${contract}"
      "${relative_path} is missing token sequence(s): ${missing_text}")
  else()
    message(STATUS "[compatibility-audit] PASS ${contract}")
  endif()
endfunction()

function(openwow_require_ordered_tokens contract relative_path)
  openwow_source_tokens("${relative_path}" remaining)
  set(missing "")
  foreach(required IN LISTS ARGN)
    set(needle " ${required} ")
    string(FIND "${remaining}" "${needle}" found)
    if(found EQUAL -1)
      set(missing "${required}")
      break()
    endif()
    string(LENGTH "${needle}" needle_length)
    # Retain the separator after the previous match so an immediately
    # following token sequence still has a left boundary.
    math(EXPR next "${found} + ${needle_length} - 1")
    string(SUBSTRING "${remaining}" ${next} -1 remaining)
  endforeach()
  if(missing)
    openwow_audit_fail("${contract}"
      "${relative_path} does not preserve ordered token sequence at: ${missing}")
  else()
    message(STATUS "[compatibility-audit] PASS ${contract}")
  endif()
endfunction()

function(openwow_require_token_count contract relative_path required minimum)
  openwow_source_tokens("${relative_path}" tokens)
  string(REGEX MATCHALL " ${required} " matches "${tokens}")
  list(LENGTH matches count)
  if(count LESS minimum)
    openwow_audit_fail("${contract}"
      "${relative_path} has ${count}, expected at least ${minimum}: ${required}")
  else()
    message(STATUS "[compatibility-audit] PASS ${contract}")
  endif()
endfunction()

function(openwow_forbid_tokens contract relative_path)
  openwow_source_tokens("${relative_path}" tokens)
  set(found_forbidden "")
  foreach(forbidden IN LISTS ARGN)
    string(FIND "${tokens}" " ${forbidden} " found)
    if(NOT found EQUAL -1)
      list(APPEND found_forbidden "${forbidden}")
    endif()
  endforeach()
  if(found_forbidden)
    list(JOIN found_forbidden " | " forbidden_text)
    openwow_audit_fail("${contract}"
      "${relative_path} contains forbidden ambiguous token(s): ${forbidden_text}")
  else()
    message(STATUS "[compatibility-audit] PASS ${contract}")
  endif()
endfunction()

# Resource reads must stay bound to the active VFS view and report the winning
# source rather than merely naming the requested logical path.
openwow_require_tokens("resource-provenance/dbc"
  "src/openwow/data/formats/dbc/dbc_loader.cpp"
  "vfs ReadFileBytes path"
  "DescribeVfsSource vfs path"
  "DBC localized source path"
  "localized_slot")
openwow_require_token_count("resource-provenance/dbc-source-coverage"
  "src/openwow/data/formats/dbc/dbc_loader.cpp"
  "DescribeVfsSource vfs path" 2)
openwow_require_tokens("resource-provenance/font-layout-cache"
  "src/openwow/ui/font_layout.cpp"
  "CacheBucket"
  "vfs_revision"
  "vfs lookup_revision"
  "vfs ReadFileBytes path"
  "vfs Resolve path")
openwow_require_tokens("resource-provenance/render-text-cache"
  "src/openwow/render/backend/bgfx/bgfx_text_cache.cpp"
  "BgfxTextCache RefreshVfsRevision"
  "vfs_ lookup_revision"
  "vfs_ ReadFileBytes path"
  "vfs_ Resolve path")

# Complete state replacements validate before mutation and interaction
# snapshots remain local until their entire payload has parsed.
openwow_require_ordered_tokens("state-replacement/object-batch"
  "src/openwow/game/object_manager.cpp"
  "ValidateUpdateObjectPacketBeforeMutation data len"
  "UpdateObjectHandler leading_out_of_range_handler"
  "PreallocateCreateObjects data len created_shells"
  "DrainWorldPublicationQueue"
  "finish_batch true")
openwow_require_tokens("state-replacement/object-packet-types"
  "src/openwow/game/object_manager.cpp"
  "packet_create_field_counts"
  "conflicting create field widths in one packet")
openwow_require_token_count("state-replacement/object-validation-replay"
  "src/openwow/game/object_manager.cpp"
  "ParseUpdateObject data len validation_handler" 2)
openwow_require_ordered_tokens("state-replacement/gossip"
  "src/openwow/game/gossip_manager.cpp"
  "GossipDialogData d"
  "before_publish d npc_guid"
  "gossip_ std move d"
  "gossip_guid_ gossip_ npc_guid")
openwow_require_ordered_tokens("state-replacement/loot"
  "src/openwow/game/session/loot_session.cpp"
  "DecodeLootResponse pkt payload data pkt payload size"
  "CloseExistingLootWindowForIncomingLoot this"
  "loot_ SetLootWindow std move loot_window")
openwow_require_ordered_tokens("state-replacement/quest-list"
  "src/openwow/game/session/quest_session.cpp"
  "QuestListInfo quest_list"
  "last_quest_list_ std move quest_list"
  "FireQuestGreeting")

# One-shot request owners, authored effect lifetimes and blend-source endpoint
# sampling must stay separate from passive selector maintenance.
openwow_require_tokens("animation/request-ownership"
  "src/openwow/game/objects/unit/unit_animation_runtime.cpp"
  "ShouldPassivelyClaimStandSelector"
  "RequestPlayback static_cast std uint16_t requested_animation looping looping"
  "m2_system SetAnimationRequest instance_id animation_request")
openwow_require_tokens("animation/completion-ownership"
  "src/openwow/render/effects/spell_visuals/spell_visual_renderer.cpp"
  "BindAnimationCompletionCallback"
  "uses_authored_lifetime"
  "animation_completion_pending true"
  "ClearAnimationCompletionCallback instance_id")
openwow_require_tokens("animation/blend-endpoint"
  "src/openwow/render/m2/m2_animator.cpp"
  "ChannelClockRole kPrimary kBlendSource"
  "kM2SequenceFlagBlendSourceClampedAtEnd"
  "std min time_ms anim_duration")

# Descriptor changes map through the build-12340 slot table before the Lua
# dispatcher publishes per-unit and global events.
openwow_require_tokens("lua-events/slot-map"
  "src/openwow/game/update_field_event_mapper.cpp"
  "GetUnitFieldEventNameForUpdatedField"
  "emitted_direct_event_ids"
  "PLAYER_XP_UPDATE"
  "PLAYER_MONEY"
  "UPDATE_EXHAUSTION")
openwow_require_ordered_tokens("lua-events/publication"
  "src/openwow/game/world_session_object.cpp"
  "MapChangedFieldsToEvents obj GetTypeId obj GetGuid GetRawValue"
  "if events empty return"
  "for const auto evt events"
  "dispatch FirePerUnitEvent evt event_name evt guid_raw")

# UI diagnostics that resolve geometry say so in their API. Mutation-driven
# hover replay happens only after layout and traversal publication.
openwow_require_tokens("ui-publication/resolved-diagnostic"
  "src/openwow/ui/game/debug/ui_debug_snapshot.cpp"
  "BuildResolvedDebugSnapshot"
  "retained_layout_ SolveIfDirty")
openwow_forbid_tokens("ui-publication/no-ambiguous-diagnostic"
  "src/openwow/ui/game/game_ui_manager.h"
  "BuildDebugSnapshot")
openwow_require_ordered_tokens("ui-publication/hover-replay"
  "src/openwow/ui/game/runtime/render/ui_compositor.cpp"
  "retained_layout_ SolveIfDirty"
  "frame_traversal_index_ RefreshRectCache"
  "frame_input_router_ ReplayMouseFocusIfDirty")
openwow_require_tokens("ui-publication/mutation-invalidation"
  "src/openwow/ui/game/game_ui_runtime.cpp"
  "NotifyFrameInputCategoryMutation"
  "frame_input_router_ MarkMouseFocusDirty")

# Failure logs retain enough boundary context to locate the first broken
# stage, while renderer resource restores still attempt independent systems.
openwow_require_tokens("failure-diagnostics/object-parse"
  "src/openwow/game/update_object_parser.cpp"
  "LogUpdateObjectParseFailure"
  "declared"
  "stage"
  "offset"
  "remaining")
openwow_require_tokens("failure-diagnostics/interactions"
  "src/openwow/game/session/loot_session.cpp"
  "interaction reject malformed"
  "opcode"
  "bytes"
  "stage loot replication")
openwow_require_tokens("failure-diagnostics/quest"
  "src/openwow/game/session/quest_session.cpp"
  "quest replication reject malformed"
  "opcode"
  "bytes")
openwow_require_ordered_tokens("failure-isolation/renderer-restore"
  "src/openwow/game/game_loop.cpp"
  "post_process_ RestoreRendererDeviceResources"
  "floating_text_ RestoreRendererDeviceResources"
  "loading_screen_ RestoreRendererDeviceResources"
  "minimap_ RestoreRendererDeviceResources"
  "debug_draw_renderer_ Initialize"
  "renderer device resource restore failed")

get_property(failures GLOBAL PROPERTY OPENWOW_AUDIT_FAILURES)
if(failures)
  list(JOIN failures "\n  - " failure_text)
  message(FATAL_ERROR
    "OpenWoW compatibility audit failed:\n  - ${failure_text}")
endif()

message(STATUS
  "OpenWoW compatibility audit passed: provenance, replacement, animation, Lua, UI, diagnostics")
