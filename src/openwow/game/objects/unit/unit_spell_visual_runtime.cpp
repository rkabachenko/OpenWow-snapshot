#include "openwow/game/objects/cgunit.h"
#include "openwow/game/objects/unit/unit_spell_visual_runtime.h"

#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/core/display_settings.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/game/delayed_spell_visual_kit.h"
#include "openwow/game/ceffect_c.h"
#include "openwow/game/display_info_resolver.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/object_effect_system.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/objects/cggameobject.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/spell_c_internals.h"
#include "openwow/game/spell_visual_system.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/render/m2/m2_system.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>
namespace openwow::game {

void AddHardcodedOneShotEffect(const WorldSession &session, CGUnit_C &unit,
                               const HardcodedEffectId effect) {
  constexpr std::uint32_t kRetailMissingAttachmentFallback = 19u;

  const auto *const dbc = unit.dbc_loader();
  const auto effect_name_id = HardcodedEffectIdTable::GetEffectId(effect);
  auto attachment_point = HardcodedEffectIdTable::GetAttachmentPoint(effect);
  const auto *const effect_name =
      dbc != nullptr && effect_name_id != 0u
          ? dbc->spell_visual_effect_name().LookupEntry(effect_name_id)
          : nullptr;
  if (effect_name == nullptr || !attachment_point.has_value()) {
    return;
  }

  const std::uint32_t owner_instance = unit.GetPrimaryM2InstanceId();
  auto *const m2 = unit.m2_system();
  if (owner_instance == 0u || m2 == nullptr) {
    return;
  }
  const auto attachment =
      m2->QueryAttachmentInfo(owner_instance, *attachment_point);
  if (attachment.status == render::m2::M2ResultStatus::kUnsupported &&
      attachment.reason == render::m2::M2ResultReason::kMissingAttachment) {
    attachment_point = kRetailMissingAttachmentFallback;
  }

  CEffectCreateInfo create_info;
  create_info.owner = &unit;
  create_info.source_guid = unit.GetGuid();
  create_info.effect_name = effect_name;
  create_info.flags = CEffectFlags::kPendingDestroy;
  create_info.attachment_point = static_cast<std::int32_t>(*attachment_point);
  (void)CEffect_C::AddEffect(session, create_info);
}

namespace {

constexpr std::uint32_t kAlphaFadeDefaultDurationMs = 1000u;

[[nodiscard]] std::uint32_t RetailTruncateFloatToDword(
    const float value) noexcept {

  if (!std::isfinite(value) ||
      static_cast<double>(value) <
          static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
      static_cast<double>(value) >
          static_cast<double>(std::numeric_limits<std::int32_t>::max())) {
    return 0x80000000u;
  }
  return static_cast<std::uint32_t>(static_cast<std::int32_t>(value));
}

[[nodiscard]] std::uint32_t RetailTruncateNonNegativeFloatToDword(
    const float value) noexcept {
  if (value <= 0.0f) {
    return 0u;
  }
  if (std::isnan(value)) {
    return 0x80000000u;
  }
  if (static_cast<double>(value) >= 4294967296.0) {
    return std::numeric_limits<std::uint32_t>::max();
  }
  return static_cast<std::uint32_t>(value);
}

[[nodiscard]] bool IsRetailHostileSpellTargetType(
    const std::uint32_t target_type) noexcept {
  constexpr std::array<std::uint32_t, 19> kTypes = {
      3u, 4u, 5u, 20u, 21u, 27u, 29u, 30u, 31u, 33u,
      34u, 35u, 45u, 56u, 57u, 58u, 59u, 61u, 62u,
  };
  return std::find(kTypes.begin(), kTypes.end(), target_type) != kTypes.end();
}

[[nodiscard]] bool IsRetailFriendlySpellTargetType(
    const std::uint32_t target_type) noexcept {
  constexpr std::array<std::uint32_t, 9> kTypes = {
      2u, 6u, 15u, 16u, 24u, 28u, 53u, 54u, 93u,
  };
  return std::find(kTypes.begin(), kTypes.end(), target_type) != kTypes.end();
}

[[nodiscard]] std::uint32_t RetailSpellTargetDisposition(
    const data::dbc::SpellEntry& spell) noexcept {
  if ((spell.targets & 0x100u) != 0u) {
    return 1u;
  }
  if ((spell.targets & 0x80u) != 0u) {
    return 2u;
  }

  for (std::size_t index = 0; index < spell.effect.size(); ++index) {
    if (IsRetailFriendlySpellTargetType(spell.effect_implicit_target_a[index]) ||
        IsRetailFriendlySpellTargetType(spell.effect_implicit_target_b[index])) {
      return 2u;
    }
  }

  for (std::size_t index = 0; index < spell.effect.size(); ++index) {
    const auto classify = [&](const std::uint32_t target_type) {
      if (target_type == 1u) {
        return spell.effect_apply_aura[index] != 4u;
      }
      return IsRetailHostileSpellTargetType(target_type);
    };
    if (classify(spell.effect_implicit_target_a[index]) ||
        classify(spell.effect_implicit_target_b[index])) {
      return 1u;
    }
  }
  return 0u;
}

UnitSpellVisualRuntime::AttachedEffectNode DescribeAttachedEffect(
    const CEffectSnapshot &state) {
  return {.spell_id = state.spell_id,
          .flags = state.flags,
          .effect_record_id = state.effect_name_id,
          .spell_record_id = state.visual_kit_id,
          .attachment_point = state.attachment_point,
          .visual_kit_param = state.visual_kit_param,
          .group_param = state.transform_key};
}

template <typename Predicate>
void TeardownAttachedEffects(const WorldSession &session, CGUnit_C &unit,
                             Predicate &&predicate) {
  for (auto *node = *unit.GetEffectNodeListHeadSlot(); node != nullptr;) {
    auto *const next = node->GetNextAttachedEffect();
    const auto state = node->Snapshot();
    if (predicate(*node, state)) {
      unit.OnDestroyEffectNode(session, DescribeAttachedEffect(state));
      node->BeginTeardown();
    }
    node = next;
  }
}

}

UnitSpellVisualRuntime::UnitSpellVisualRuntime(CGUnit_C& owner) noexcept
    : owner_(owner) {}

UnitSpellVisualRuntime::~UnitSpellVisualRuntime() = default;

void UnitSpellVisualRuntime::SetCreatureInfoCallback(
    std::function<void(std::uint32_t, bool)> callback) {
  creature_info_callback_ = std::move(callback);
}

void UnitSpellVisualRuntime::SetFixedTargetPosition(
    const std::array<float, 3> *const position) noexcept {
  if (position != nullptr) {
    fixed_target_position_ = *position;
    fixed_target_present_ = true;
    return;
  }
  fixed_target_present_ = false;
}

void UnitSpellVisualRuntime::ClearDispatches() {
  dispatches_.clear();
}

DelayedSpellVisualKit &UnitSpellVisualRuntime::AddDelayedKit(
    const std::uint32_t spell_id, const std::uint32_t visual_record,
    const std::uint32_t effect_index,
    const DelayedVisualKitPosition *const position,
    const std::uint32_t position_parameter_a,
    const std::uint32_t position_parameter_b, const bool flag_a,
    const bool flag_b, const std::uint32_t timestamp,
    const std::uint32_t visual_type, const std::uint32_t expire_time) {
  return game::AddDelayedSpellVisualKit(
      delayed_kits_, spell_id, visual_record, effect_index,
      position, position_parameter_a, position_parameter_b, flag_a, flag_b,
      timestamp, visual_type, expire_time);
}

void UnitSpellVisualRuntime::ReleaseAlphaFadeForSpell(
    const std::uint32_t spell_id) {
  for (auto it = alpha_fade_effect_nodes_.begin();
       it != alpha_fade_effect_nodes_.end();) {
    if (it->spell_id == spell_id) {
      it = alpha_fade_effect_nodes_.erase(it);
    } else {
      ++it;
    }
  }
  has_looping_alpha_effect_ = std::any_of(
      alpha_fade_effect_nodes_.begin(), alpha_fade_effect_nodes_.end(),
      [](const AlphaFadeEffectNode& node) {
        return (node.kit_flags & 0x400u) != 0u;
      });
}

std::uint32_t UnitSpellVisualRuntime::RefreshOpacityFromAlphaFadeHead() {
  float target_alpha = owner_.Presentation().ModelOpacity();
  std::uint32_t duration_ms = kAlphaFadeDefaultDurationMs;
  if (!alpha_fade_effect_nodes_.empty()) {
    const auto& head = alpha_fade_effect_nodes_.front();
    target_alpha *= head.alpha_value;
    if (head.duration_ms != 0u) {
      duration_ms = head.duration_ms;
    }
  }
  owner_.SetOpacityTarget(target_alpha, duration_ms);
  return duration_ms;
}

void UnitSpellVisualRuntime::EndSpellVisualProcState(
    const std::uint32_t spell_id) {

  if (spell_id == 0u) {
    return;
  }
  ReleaseAlphaFadeForSpell(spell_id);
  RefreshOpacityFromAlphaFadeHead();
  body_tint_effect_nodes_.erase(
      std::remove_if(body_tint_effect_nodes_.begin(),
                     body_tint_effect_nodes_.end(),
                     [spell_id](const BodyTintEffectNode& node) {
                       return node.spell_id == spell_id;
                     }),
      body_tint_effect_nodes_.end());
}

void UnitSpellVisualRuntime::ClearCreatureInfo() {
  if (!creature_info_active_ || creature_info_effect_id_ == 0u) {
    creature_info_active_ = false;
    creature_info_effect_id_ = 0u;
    return;
  }
  const auto effect_id = creature_info_effect_id_;
  creature_info_active_ = false;
  creature_info_effect_id_ = 0u;
  if (creature_info_callback_) {
    creature_info_callback_(effect_id, false);
  }
}

void UnitSpellVisualRuntime::CleanupForLootEffect() {
  ClearCreatureInfo();
}

bool UnitSpellVisualRuntime::HasFixedTargetPosition() const noexcept {
  return fixed_target_present_;
}

const std::array<float, 3> &
UnitSpellVisualRuntime::FixedTargetPosition() const noexcept {
  return fixed_target_position_;
}

const std::vector<UnitSpellVisualRuntime::DispatchRecord> &
UnitSpellVisualRuntime::Dispatches() const {
  return dispatches_;
}

std::vector<DelayedSpellVisualKit> &UnitSpellVisualRuntime::DelayedKits() {
  return delayed_kits_;
}

const std::vector<DelayedSpellVisualKit> &
UnitSpellVisualRuntime::DelayedKits() const {
  return delayed_kits_;
}

bool UnitSpellVisualRuntime::ShouldApplyVisibleHumanoid(
    const std::uint32_t filter_flags) const {
  if (!owner_.IsPlayer() &&
      owner_.State().GetCreatureType() != CreatureTypeId::kHumanoid) {
    return false;
  }
  const auto *const objects = owner_.object_manager();
  const auto active_player_guid =
      objects != nullptr ? objects->GetActivePlayerGuid() : ObjectGuid{};
  if (owner_.GetGuid() == active_player_guid) {
    return false;
  }
  constexpr std::uint32_t kHostilePlayersOnly = 0x1u;
  if ((filter_flags & kHostilePlayersOnly) == 0u) {
    return true;
  }
  const auto *const active_player =
      objects != nullptr ? objects->GetActivePlayer() : nullptr;
  return owner_.IsPlayer() &&
         (active_player == nullptr ||
          active_player->Interaction().IsHostileTo(owner_));
}

bool UnitSpellVisualRuntime::UpdateVisibleHumanoidDisplay(
    const std::uint32_t creature_entry, const std::uint32_t filter_flags,
    const QueryCache *const query_cache) {
  if (!ShouldApplyVisibleHumanoid(filter_flags) ||
      creature_entry == 0u) {
    ClearVisibleHumanoidDisplay();
    return false;
  }
  return RefreshVisibleHumanoidDisplay(creature_entry, query_cache);
}

void UnitSpellVisualRuntime::ClearVisibleHumanoidDisplay() {
  if (visible_humanoid_creature_entry_ == 0u &&
      visible_humanoid_display_id_ == 0u) {
    return;
  }
  const auto previous_display_id = owner_.Presentation().CurrentDisplayId();
  visible_humanoid_creature_entry_ = 0u;
  visible_humanoid_display_id_ = 0u;
  if (previous_display_id != owner_.Presentation().CurrentDisplayId()) {
    owner_.Presentation().RefreshActiveDisplayRuntimeState();
    owner_.Presentation().OnDisplayIdChanged();
  }
}

bool UnitSpellVisualRuntime::RefreshVisibleHumanoidDisplay(
    const std::uint32_t creature_entry, const QueryCache *query_cache) {
  if (visible_humanoid_creature_entry_ == creature_entry) {
    return true;
  }
  if (query_cache == nullptr) {
    const auto *const objects = owner_.object_manager();
    query_cache = objects != nullptr ? &objects->query_cache() : nullptr;
  }
  const auto *const creature =
      query_cache != nullptr
          ? query_cache->GetCreatureTemplate(creature_entry)
          : nullptr;
  if (creature == nullptr) {
    return false;
  }
  std::array<std::uint32_t, 4> display_ids{};
  std::size_t display_count = 0u;
  for (const auto display_id : creature->display_ids) {
    if (display_id != 0u) {
      display_ids[display_count++] = display_id;
    }
  }
  if (display_count == 0u) {
    return false;
  }
  const auto previous_display_id = owner_.Presentation().CurrentDisplayId();
  visible_humanoid_creature_entry_ = creature_entry;
  visible_humanoid_display_id_ =
      display_ids[static_cast<std::size_t>(std::rand()) % display_count];
  if (previous_display_id != owner_.Presentation().CurrentDisplayId()) {
    owner_.Presentation().RefreshActiveDisplayRuntimeState();
    owner_.Presentation().OnDisplayIdChanged();
  }
  return true;
}

void UnitSpellVisualRuntime::TeardownChannelEffectsIfAuraMissing(
    const WorldSession &session, const std::uint32_t spell_id) {
  if (owner_.Auras().FindBySpellId(spell_id) != nullptr) {
    return;
  }
  TeardownAttachedEffects(
       session, owner_,
      [spell_id](const CEffect_C &, const CEffectSnapshot &state) {
        return state.spell_id == spell_id &&
               (state.flags & CEffectFlags::kChannelTeardownMask) != 0u;
      });
}

void UnitSpellVisualRuntime::DestroyLootEffects(const WorldSession &session) {
  TeardownAttachedEffects(
       session, owner_, [](const CEffect_C &, const CEffectSnapshot &state) {
        return (state.flags & 0x8u) != 0u;
      });
  ClearCreatureInfo();
}

void UnitSpellVisualRuntime::AddAttachedEffect(
    const WorldSession &session, const AttachedEffectNode &node) {
  CEffectSnapshot state;
  state.spell_id = node.spell_id;
  state.flags = node.flags;
  state.effect_name_id = node.effect_record_id;
  state.visual_kit_id = node.spell_record_id;
  state.attachment_point = node.attachment_point;
  state.visual_kit_param = node.visual_kit_param;
  state.transform_key = node.group_param;
  (void)CEffect_C::AddLogicalEffect(session, owner_, state);
}

std::size_t UnitSpellVisualRuntime::AttachedEffectCount() const {
  return CEffect_C::CountAttached(owner_);
}

void UnitSpellVisualRuntime::RemoveEffectsBySpellId(
    const WorldSession &session, const std::uint32_t spell_id,
    const bool force_remove_persistent) {
  if (spell_id == 0u) {
    return;
  }
  TeardownAttachedEffects(
       session, owner_,
      [spell_id, force_remove_persistent](const CEffect_C &,
                                           const CEffectSnapshot &state) {
        return state.spell_id == spell_id &&
               (state.flags & CEffectFlags::kPendingDestroy) == 0u &&
               (force_remove_persistent ||
                (state.flags & CEffectFlags::kPersistent) == 0u);
      });
}

void UnitSpellVisualRuntime::ResetMatchingNodes(
    const WorldSession &, const std::uint32_t spell_id,
    const std::uint32_t visual_kit_param) {
  for (auto *node = *owner_.GetEffectNodeListHeadSlot(); node != nullptr;
       node = node->GetNextAttachedEffect()) {
    const auto state = node->Snapshot();
    if (state.spell_id == spell_id &&
        (state.flags & CEffectFlags::kEmitterCountdown) != 0u &&
        state.visual_kit_param == visual_kit_param) {
      node->EnableEffectEmittersForTwoUpdates();
    }
  }
}

bool UnitSpellVisualRuntime::CreateFromKit(
    const WorldSession& session,
    const std::uint32_t kit_id,
    const std::uint32_t dispatch_type,
    const std::array<float, 3>* const world_position,
    const bool harmful,
    const ObjectGuid explicit_chain_source,
    const std::uint32_t spell_id,
    const std::uint32_t spell_visual_id,
    const SpellVisualPresentationPhase phase,
    const SpellVisualLifecycleAction action,
    const std::uint8_t aura_slot,
    const SpellVisualSpellBinding spell_binding) {
  const auto* const dbc = owner_.dbc_loader();
  if (dbc == nullptr) {
    return false;
  }

  const auto* const kit = dbc->spell_visual_kit().LookupEntry(kit_id);
  if (kit == nullptr) {
    return false;
  }

  DispatchRecord dispatch;
  dispatch.kit_id = kit_id;
  dispatch.spell_id = spell_id;
  if (dispatch.spell_id == 0u &&
      spell_binding == SpellVisualSpellBinding::kInferOwnerSpell) {
    dispatch.spell_id = owner_.Casts().GetCurrentCast().spell_id;
  }
  if (dispatch.spell_id == 0u &&
      spell_binding == SpellVisualSpellBinding::kInferOwnerSpell) {
    dispatch.spell_id = owner_.Casts().GetChannelSpellId(owner_);
  }
  if (dispatch.spell_id == 0u &&
      spell_binding == SpellVisualSpellBinding::kInferOwnerSpell) {
    dispatch.spell_id = owner_.Casts().GetChannelCast().spell_id;
  }
  dispatch.dispatch_type = dispatch_type;
  dispatch.sound_kit_id = kit->sound_id;
  dispatch.shake_id = kit->shake_id;
  dispatch.lifecycle_action = action;
  dispatch.phase = phase;
  dispatch.aura_slot = aura_slot;
  dispatch.spell_visual_id = spell_visual_id;
  dispatch.harmful = harmful;
  if (world_position != nullptr) {
    dispatch.world_position = *world_position;
  }

  if (kit->shake_id != 0u) {
    dispatch.raw_flags |= 0x10u;
  }
  if (owner_.sound_runtime().GetSoundKitData(kit->sound_id) != nullptr) {
    dispatch.raw_flags |= 0x1u;
  }

  const auto* const dispatch_spell =
      dispatch.spell_id != 0u ? dbc->spell().LookupEntry(dispatch.spell_id)
                              : nullptr;
  if (dispatch_spell != nullptr && dispatch_spell->spell_missile_id > 0u) {
    dispatch.raw_flags |= 0x400000u;
  }
  if ((kit->flags & 0xCu) != 0u) {
    dispatch.raw_flags |= 0x1000000u;
  }

  const auto* const dispatch_visual =
      dispatch_spell != nullptr && dispatch_spell->spell_visual[0] != 0u
          ? dbc->spell_visual().LookupEntry(dispatch_spell->spell_visual[0])
          : nullptr;
  if (dispatch_visual != nullptr && (dispatch_visual->flags & 0x10u) != 0u) {
    dispatch.raw_flags |= 0x200000u;
  }

  switch (dispatch_type) {
    case 0:
      dispatch.raw_flags |= 0x80u;
      [[fallthrough]];
    case 1:
    case 5:
    case 6:
    case 8:
      dispatch.raw_flags = (dispatch.raw_flags & ~0x1u) | 0x20u;
      break;
    case 2:
      dispatch.raw_flags |= 0x21000u;
      break;
    case 3:
      dispatch.raw_flags |= 0x22000u;
      break;
    case 4:
      dispatch.raw_flags |= 0x40u;
      break;
    case 7:
      dispatch.raw_flags |= 0x22000u;
      break;
    default:
      break;
  }

  owner_.Animation().ApplySpellVisualKitAnimation(
      session, kit_id, dispatch_type, dispatch.spell_id);

  const bool player_weapon_visuals =
      dynamic_cast<const CGPlayer_C*>(&owner_) != nullptr;
  const bool has_aura_visual_flag =
      action == SpellVisualLifecycleAction::kAuraStart;
  const bool base_uses_world_position =
      (dispatch.raw_flags & 0x1000u) != 0u && world_position != nullptr;

  const auto append_effect =
      [&dispatch, dbc, world_position](const std::uint32_t effect_name_id,
                                       const std::int32_t attachment_id,
                                       const std::uint32_t source_field_index,
                                       const bool world_space,
                                       const bool from_model_attach,
                                       const std::uintptr_t transform_key,
                                       const std::array<float, 3>& offset,
                                       const std::array<float, 3>& rotation) {
    if (effect_name_id == 0u) {
      return;
    }

    const auto* const effect =
        dbc->spell_visual_effect_name().LookupEntry(effect_name_id);
    if (effect == nullptr) {
      return;
    }

    DispatchEffect resolved;
    resolved.effect_name_id = effect_name_id;
    resolved.model_path = std::string(effect->file_path);
    resolved.resource_scale = effect->scale;
    resolved.attachment_id = attachment_id;
    resolved.source_field_index = source_field_index;
    resolved.transform_key = transform_key;
    resolved.world_space = world_space;
    resolved.uses_explicit_world_position =
        world_space && world_position != nullptr;
    resolved.from_model_attach = from_model_attach;
    resolved.offset = offset;
    resolved.rotation = rotation;
    dispatch.effects.push_back(std::move(resolved));

    if (world_space && world_position == nullptr) {
      dispatch.raw_flags |= 0x200u;
    }
  };

  append_effect(kit->head_effect, 20, 3, false, false, 0u, {}, {});
  append_effect(kit->chest_effect, 34, 4, false, false, 0u, {}, {});
  append_effect(kit->base_effect, base_uses_world_position ? -1 : 19, 5,
                base_uses_world_position, false, 0u, {}, {});
  append_effect(kit->left_hand_effect, 21, 6, false, false, 0u, {}, {});
  append_effect(kit->right_hand_effect, 22, 7, false, false, 0u, {}, {});
  append_effect(kit->breath_effect, 17, 8, false, false, 0u, {}, {});

  if (player_weapon_visuals) {
    const auto* const display_info =
        dbc->creature_display_info().LookupEntry(
             owner_.Presentation().CreatureModelLookupDisplayId());
    const auto* const model_data =
        display_info != nullptr
            ? dbc->creature_model_data().LookupEntry(display_info->model_id)
            : nullptr;
    static constexpr std::uint32_t kCreatureModelBlockWeaponEffects = 0x10u;
    const bool model_blocks =
        model_data != nullptr &&
        (model_data->flags & kCreatureModelBlockWeaponEffects) != 0u;

    if (!model_blocks) {

      if (!has_aura_visual_flag) {
        owner_.Animation().ChangeSheatheStateAndNotifyServer(0, true, false);
      }

      append_effect(kit->left_weapon_effect, 2, 9, false, false, 0u, {}, {});
      append_effect(kit->right_weapon_effect, 1, 10, false, false, 0u, {}, {});
    }
  }
  append_effect(kit->special1_effect, 23, 11, false, false, 0u, {}, {});
  append_effect(kit->special2_effect, 24, 12, false, false, 0u, {}, {});
  append_effect(kit->special3_effect, 25, 13, false, false, 0u, {}, {});
  append_effect(kit->world_effect, -1, 14, true, false, 0u, {}, {});

  for (const auto& model_attach : dbc->spell_visual_kit_model_attach().entries()) {
    if (model_attach.parent_spell_visual_kit_id != kit_id) {
      continue;
    }

    const bool model_attach_world_space =
        static_cast<std::int32_t>(model_attach.attachment_id) == -1;
    append_effect(
        model_attach.spell_visual_effect_name_id,
        model_attach_world_space
            ? -1
            : static_cast<std::int32_t>(model_attach.attachment_id),
        0,
        model_attach_world_space,
        true,
        model_attach.id,
        {model_attach.offset_x, model_attach.offset_y, model_attach.offset_z},
        {model_attach.yaw, model_attach.pitch, model_attach.roll});
  }

  const auto proc_effect_flags =
      action == SpellVisualLifecycleAction::kChannelStart
          ? dispatch.raw_flags | CEffectFlags::kChannelVisual
          : dispatch.raw_flags;
  ProcessChainProcs(session, *kit, dispatch.spell_id, proc_effect_flags,
                    explicit_chain_source, dispatch);

  dispatches_.push_back(std::move(dispatch));
  return true;
}

void UnitSpellVisualRuntime::QueueMissileVisual(
    const std::uint32_t spell_id, const std::uint32_t spell_visual_id,
    SpellMissilePresentationData missile,
    const std::uint64_t missile_caster_guid,
    const std::uint8_t missile_cast_count,
    const std::uint64_t target_guid,
    const std::array<float, 3>& source_position,
    const std::array<float, 3>& target_position, const float speed,
    const std::uint32_t impact_kit_id, const std::uint8_t impact_result,
    const std::uint8_t reflect_result) {
  DispatchRecord dispatch;
  dispatch.spell_id = spell_id;
  dispatch.spell_visual_id = spell_visual_id;
  dispatch.dispatch_type = 0u;
  dispatch.missile = std::move(missile);
  dispatch.missile_source_position = source_position;
  dispatch.missile_caster_guid = missile_caster_guid;
  dispatch.missile_cast_count = missile_cast_count;
  dispatch.missile_target_guid = target_guid;
  dispatch.missile_target_position = target_position;
  dispatch.missile_speed = speed;
  dispatch.missile_impact_result = impact_result;
  dispatch.missile_reflect_result = reflect_result;
  dispatch.deferred_impact_kit_id = impact_kit_id;
  dispatches_.push_back(std::move(dispatch));
}

void UnitSpellVisualRuntime::QueueAuraVisualStop(
    const std::uint8_t aura_slot, const std::uint32_t spell_id,
    const std::uint32_t spell_visual_id, const std::uint32_t state_kit_id) {
  DispatchRecord dispatch;
  dispatch.kit_id = state_kit_id;
  dispatch.spell_id = spell_id;
  dispatch.spell_visual_id = spell_visual_id;
  dispatch.aura_slot = aura_slot;
  dispatch.phase = SpellVisualPresentationPhase::kState;
  dispatch.lifecycle_action = SpellVisualLifecycleAction::kAuraStop;
  dispatches_.push_back(std::move(dispatch));
}

void UnitSpellVisualRuntime::QueueChannelVisualStop(
    const std::uint32_t spell_id) {
  DispatchRecord dispatch;
  dispatch.spell_id = spell_id;
  dispatch.lifecycle_action = SpellVisualLifecycleAction::kChannelStop;
  dispatches_.push_back(std::move(dispatch));
}

void UnitSpellVisualRuntime::QueueCastVisualStop(
    const std::uint32_t spell_id) {
  DispatchRecord dispatch;
  dispatch.spell_id = spell_id;
  dispatch.lifecycle_action = SpellVisualLifecycleAction::kCastStop;
  dispatches_.push_back(std::move(dispatch));
}

void UnitSpellVisualRuntime::ProcessChainProcs(
    const WorldSession& session, const data::dbc::SpellVisualKitEntry& kit,
    const std::uint32_t spell_id, const std::uint32_t effect_flags,
    const ObjectGuid explicit_chain_source, DispatchRecord& dispatch) {
  const auto* const dbc = owner_.dbc_loader();
  auto* const objects = owner_.object_manager();
  const auto* const spell = dbc != nullptr && spell_id != 0u
                                ? dbc->spell().LookupEntry(spell_id) : nullptr;

  const auto* const display =
      dbc != nullptr
          ? dbc->creature_display_info().LookupEntry(
                owner_.Presentation().CreatureModelLookupDisplayId())
          : nullptr;
  const auto* const model = dbc != nullptr && display != nullptr
      ? dbc->creature_model_data().LookupEntry(display->model_id) : nullptr;
  const bool suppress_tint_procs = model != nullptr &&
      (model->flags & 0x40u) != 0u && spell != nullptr &&
      RetailSpellTargetDisposition(*spell) == 2u;

  const auto add_logical_effect =
      [this, &kit, &session](const std::uint32_t logical_spell_id,
          const std::uint32_t flags, const std::int32_t attachment_point,
          const std::uint32_t visual_parameter, const std::uint32_t resource_id,
          const ObjectGuid source_guid) -> CMissileNode_C* {
    CEffectSnapshot state{};
    state.owner_guid = owner_.GetGuid();
    state.source_guid = source_guid;
    state.spell_id = logical_spell_id;
    state.flags = flags;
    state.visual_kit_id = kit.id;
    state.resource_id = resource_id;
    state.attachment_point = attachment_point;
    state.visual_kit_param = visual_parameter;
    return CEffect_C::AddLogicalEffect(session, owner_, state);
  };

  for (std::size_t proc_index = 0u; proc_index < std::size(kit.proc_type); ++proc_index) {
    ProcExecution execution{};
    execution.slot = static_cast<std::uint32_t>(proc_index);
    execution.type = kit.proc_type[proc_index];
    execution.parameters = {kit.proc_param_zero[proc_index],
        kit.proc_param_one[proc_index], kit.proc_param_two[proc_index],
        kit.proc_param_three[proc_index]};

    switch (execution.type) {
    case 0u:
    case 12u: {
      auto* const owner = add_logical_effect(
          spell_id, effect_flags & 0x00300401u, -1, 0u, 0u, owner_.GetGuid());
      if (owner != nullptr) execution.logical_effect_id = owner->Snapshot().effect_id;
      CGUnit_C* source = &owner_;
      std::vector<ObjectGuid> targets;
      const bool use_explicit_source = (effect_flags & 0x1000u) != 0u &&
          !explicit_chain_source.IsEmpty() &&
          explicit_chain_source != owner_.GetGuid();
      if (use_explicit_source) {
        source = objects != nullptr ? objects->GetMutableUnit(explicit_chain_source) : nullptr;
        if (source == nullptr) {
          execution.outcome = ProcOutcome::kSkippedUnresolvedSource;
          if (owner != nullptr) owner->BeginTeardown();
          dispatch.procs.push_back(std::move(execution));
          continue;
        }
        targets.push_back(owner_.GetGuid());
      } else if (!fixed_target_present_) {

        const auto descriptor_channel_target =
            owner_.Casts().GetChannelSpellId(owner_) == spell_id
                ? owner_.Casts().GetChannelObject(owner_)
                : ObjectGuid{};
        const auto &missile_targets = owner_.Casts().GetMissileHitOtherTargets();
        const auto tracked_target = owner_.Casts().GetTrackedSpellTarget();
        if (!descriptor_channel_target.IsEmpty()) {
          targets.push_back(descriptor_channel_target);
        } else if (owner_.Casts().GetTrackedSpellTargetSpellId() == spell_id &&
            !tracked_target.IsEmpty() && missile_targets.size() < 2u) {
          targets.push_back(tracked_target);
        } else if (missile_targets.empty()) {
          execution.outcome = ProcOutcome::kSkippedNoTarget;
          if (owner != nullptr) owner->BeginTeardown();
          dispatch.procs.push_back(std::move(execution));
          continue;
        } else {
          targets = missile_targets;
        }
      }
      execution.source_guid = source->GetGuid();
      execution.target_guids = targets;
      if (fixed_target_present_)
        execution.fixed_target_position = fixed_target_position_;
      execution.event_parameter = next_chain_event_parameter_;
      std::vector<std::uint64_t> raw_target_guids;
      raw_target_guids.reserve(targets.size());
      for (const auto target : targets) raw_target_guids.push_back(target.GetRawValue());
      std::array<std::uintptr_t, CMissileNode_C::kMaxLoopingLightningHandles>
          retained_handles{};
      if (owner != nullptr) {
        SpellVisual_CreateLightningEffect(
            RetailTruncateFloatToDword(kit.proc_param_zero[proc_index]),
            reinterpret_cast<std::uintptr_t>(source),
            reinterpret_cast<std::uintptr_t>(owner),
            raw_target_guids.empty() ? std::uintptr_t{0}
                : reinterpret_cast<std::uintptr_t>(raw_target_guids.data()),
            static_cast<std::uint32_t>(raw_target_guids.size()), spell_id,
            kit.proc_param_two[proc_index] != 0.0f,
            kit.proc_param_three[proc_index] != 0.0f,
            reinterpret_cast<std::uintptr_t>(retained_handles.data()),
            static_cast<std::uint32_t>(retained_handles.size()),
            fixed_target_present_ ? fixed_target_position_.data() : nullptr,
            static_cast<std::int32_t>(next_chain_event_parameter_));
      }
      owner_.Casts().ClearMissileHitOtherTargets();
      next_chain_event_parameter_ += 2u;
      execution.outcome = ProcOutcome::kApplied;
      bool retained_loop = false;
      if (owner != nullptr) {
        for (std::size_t slot = 0u; slot < retained_handles.size(); ++slot) {
          const LightningObjectHandle handle{retained_handles[slot]};
          if (!handle.IsValid()) continue;
          owner->SetLoopingLightningHandle(slot, handle);
          retained_loop = true;
        }
        if (!retained_loop && owner->GetOwnerRefCount() == 0u) owner->BeginTeardown();
      }
      break;
    }
    case 1u:
      if (suppress_tint_procs) {
        execution.outcome = ProcOutcome::kSuppressed;
        break;
      }
      body_tint_effect_nodes_.erase(std::remove_if(body_tint_effect_nodes_.begin(),
          body_tint_effect_nodes_.end(), [spell_id](const BodyTintEffectNode& node) {
            return node.spell_id == spell_id;
          }), body_tint_effect_nodes_.end());
      execution.packed_value = RetailTruncateNonNegativeFloatToDword(
          kit.proc_param_zero[proc_index]) | 0xFF000000u;
      body_tint_effect_nodes_.insert(body_tint_effect_nodes_.begin(),
          BodyTintEffectNode{spell_id, execution.packed_value});
      execution.outcome = ProcOutcome::kApplied;
      break;
    case 6u:
      execution.packed_value = RetailTruncateNonNegativeFloatToDword(
          kit.proc_param_zero[proc_index]) | 0xFF000000u;
      SpellVisuals_BeginLightingEnvelope(session, execution.packed_value,
          kit.proc_param_one[proc_index], spell_id);
      execution.outcome = ProcOutcome::kApplied;
      break;
    case 8u:
      execution.packed_value =
          (RetailTruncateNonNegativeFloatToDword(kit.proc_param_three[proc_index]) << 24u) |
          RetailTruncateNonNegativeFloatToDword(kit.proc_param_zero[proc_index]);
      execution.duration_ms = RetailTruncateNonNegativeFloatToDword(
          kit.proc_param_two[proc_index]);
      last_transient_model_color_proc_ =
          TransientModelColorProc{execution.packed_value, execution.duration_ms};
      execution.outcome = ProcOutcome::kApplied;
      break;
    case 11u: {
      const auto animation_window_ms = RetailTruncateNonNegativeFloatToDword(
          kit.proc_param_zero[proc_index] * 1000.0f);
      auto* const effect = add_logical_effect(spell_id,
          (effect_flags & 0x00300401u) | CEffectFlags::kRestoreOwnerAnimationOnEnd,
          -1, animation_window_ms, 0u, owner_.GetGuid());
      if (effect != nullptr) execution.logical_effect_id = effect->Snapshot().effect_id;
      execution.duration_ms = animation_window_ms;
      execution.outcome = ProcOutcome::kApplied;
      break;
    }
    case 13u:
      if (suppress_tint_procs) {
        execution.outcome = ProcOutcome::kSuppressed;
        break;
      }
      execution.packed_value = RetailTruncateNonNegativeFloatToDword(
          kit.proc_param_zero[proc_index]) | 0xFF000000u;
      execution.delay_ms = RetailTruncateFloatToDword(
          kit.proc_param_one[proc_index] * 1000.0f);
      execution.duration_ms = RetailTruncateFloatToDword(
          kit.proc_param_two[proc_index] * 1000.0f);
      owner_.Presentation().StartBodyColorFade(core::GameClock::GetTickCount32(),
                                               execution.packed_value,
                                               execution.delay_ms,
                                               execution.duration_ms);
      execution.outcome = ProcOutcome::kApplied;
      break;
    case 14u: {
      ReleaseAlphaFadeForSpell(spell_id);
      const float requested_alpha = kit.proc_param_zero[proc_index];
      if (!std::isnan(requested_alpha) && requested_alpha >= 0.0f && requested_alpha <= 1.0f) {
        execution.duration_ms = RetailTruncateFloatToDword(
            kit.proc_param_two[proc_index] * 1000.0f);
        alpha_fade_effect_nodes_.insert(alpha_fade_effect_nodes_.begin(),
            AlphaFadeEffectNode{spell_id, kit.id, kit.flags,
                                requested_alpha, execution.duration_ms});
        has_looping_alpha_effect_ = std::any_of(
            alpha_fade_effect_nodes_.begin(), alpha_fade_effect_nodes_.end(),
            [](const AlphaFadeEffectNode& node) {
              return (node.kit_flags & 0x400u) != 0u;
            });
      }
      execution.duration_ms = RefreshOpacityFromAlphaFadeHead();
      execution.outcome = ProcOutcome::kApplied;
      break;
    }
    case 15u: {
      const auto target_value = RetailTruncateFloatToDword(
          owner_.Presentation().ModelOpacity() * kit.proc_param_zero[proc_index]);
      const auto selector = RetailTruncateFloatToDword(kit.proc_param_one[proc_index]);
      if (selector >= 12u) {
        execution.outcome = ProcOutcome::kIgnored;
        break;
      }
      execution.selector = selector;
      execution.packed_value = target_value & 0xFFu;
      execution.duration_ms = RetailTruncateFloatToDword(kit.proc_param_two[proc_index]);
      execution.delay_ms = RetailTruncateFloatToDword(kit.proc_param_three[proc_index]) + 500u;
      CEffect_C::CancelOwnerAlphaRestore(owner_.GetGuid());
      owner_.SetOpacityTarget(static_cast<float>(execution.packed_value), 500u);
      auto* const scheduler =
          add_logical_effect(0u, 0u, -1, selector, 0u, owner_.GetGuid());
      if (scheduler != nullptr) {
        scheduler->ConfigureOwnerAlphaRestore(core::GameClock::GetTickCount32() +
            execution.delay_ms, execution.duration_ms);
        execution.logical_effect_id = scheduler->Snapshot().effect_id;
      }
      execution.outcome = ProcOutcome::kApplied;
      break;
    }
    case 16u: {
      const bool allowed_while_unmounted =
          (static_cast<std::int32_t>(owner_.Mount().DisplayId(owner_)) < 1 ||
           (owner_.State().GetSpellStateFlags() & 0x10000000u) != 0u) &&
          owner_.Mount().OverlayM2InstanceId() == 0u;
      if (!allowed_while_unmounted) {
        execution.outcome = ProcOutcome::kSuppressed;
        break;
      }
      auto* const effect = add_logical_effect(
          spell_id, effect_flags & 0xC0u, 19, 0u, 0u, owner_.GetGuid());
      if (effect != nullptr) execution.logical_effect_id = effect->Snapshot().effect_id;
      execution.outcome = ProcOutcome::kApplied;
      break;
    }
    case 17u: {
      const auto resource_id = RetailTruncateNonNegativeFloatToDword(
          kit.proc_param_one[proc_index]);
      execution.packed_value = resource_id;
      auto* const effect = add_logical_effect(spell_id,
          effect_flags | CEffectFlags::kObjectItemVisual, -1, 0u, resource_id,
          owner_.GetGuid());
      if (effect != nullptr) execution.logical_effect_id = effect->Snapshot().effect_id;
      execution.outcome = ProcOutcome::kApplied;
      break;
    }
    default:
      break;
    }
    dispatch.procs.push_back(std::move(execution));
  }

  if (last_transient_model_color_proc_.has_value() &&
      last_transient_model_color_proc_->duration_ms != 0u) {
    if (owner_.State().GetSheathState() == 1u) {
      const std::array instance_ids = {
          owner_.Presentation().PrimarySpellVisualModelInstanceId(),
          owner_.Presentation().SecondarySpellVisualModelInstanceId()};
      const std::uint32_t start_tick_ms = core::GameClock::GetTickCount32();
      for (const std::uint32_t instance_id : instance_ids) {
        if (instance_id != 0u) {
          (void)owner_.m2_system()->BeginTransientWeaponTrail(instance_id,
              last_transient_model_color_proc_->packed_argb,
              last_transient_model_color_proc_->duration_ms, start_tick_ms);
        }
      }
    }
    last_transient_model_color_proc_->duration_ms = 0u;
  }
  CEffect_C::ProcessTeardownList();
}

void UnitSpellVisualRuntime::RemoveMatchingEffects(
    const WorldSession &session, const std::uint32_t effect_record_id,
    const std::int32_t attachment_point, const std::uint32_t caster_id,
    const std::uint32_t spell_record_id, const std::uint32_t group_param) {
  if (attachment_point == -1) {
    return;
  }
  TeardownAttachedEffects(
       session, owner_,
      [=](const CEffect_C &effect, const CEffectSnapshot &) {
        return effect.MatchesReplacement(
            effect_record_id, attachment_point, caster_id, spell_record_id,
            static_cast<std::uintptr_t>(group_param));
      });
}

void UnitSpellVisualRuntime::DestroyByKitType(
    const WorldSession &session, const std::uint32_t spell_id,
    const std::uint32_t kit_type, const std::uint32_t visual_param,
    const bool filter_spell, const bool filter_param) {
  const auto *const dbc = owner_.dbc_loader();
  const auto *const spell =
      dbc != nullptr ? dbc->spell().LookupEntry(spell_id) : nullptr;
  if (spell == nullptr) {
    return;
  }
  data::dbc::SpellVisualEntry visual_buffer{};
  const auto *const visual =
      owner_.ResolveSpellVisualRecord(*spell, visual_buffer, 0u, 0u);
  if (visual == nullptr) {
    return;
  }
  std::uint32_t kit_id = 0u;
  switch (kit_type) {
  case 0u: kit_id = visual->cast_kit; break;
  case 1u: kit_id = visual->impact_kit; break;
  case 2u: kit_id = visual->state_kit; break;
  case 4u: kit_id = visual->precast_kit; break;
  case 5u: kit_id = visual->caster_impact_kit; break;
  case 6u: kit_id = visual->target_impact_kit; break;
  case 7u: kit_id = visual->missile_targeting_kit; break;
  case 8u: kit_id = visual->state_done_kit; break;
  default: return;
  }
  const auto *const kit = dbc->spell_visual_kit().LookupEntry(kit_id);
  if (kit == nullptr) {
    return;
  }
  TeardownAttachedEffects(
       session, owner_,
      [kit_id = kit->id, spell_id, visual_param, filter_spell,
       filter_param](const CEffect_C &, const CEffectSnapshot &state) {
        return state.visual_kit_id != 0u &&
               state.visual_kit_id == kit_id &&
               (!filter_spell || state.spell_id == spell_id) &&
               (!filter_param || state.visual_kit_param == visual_param);
      });
}

void UnitSpellVisualRuntime::RedirectSpellListForGuid(
    const std::uint8_t *const match_data) {
  if (match_data == nullptr) {
    return;
  }
  std::uint32_t low = 0u;
  std::uint32_t high = 0u;
  std::array<float, 3> position{};
  std::memcpy(&low, match_data, sizeof(low));
  std::memcpy(&high, match_data + 4, sizeof(high));
  std::memcpy(position.data(), match_data + 12, sizeof(position));
  const auto guid = ObjectGuid::FromHalves(low, high);
  const auto type = match_data[8];
  for (auto *node = spell_node_list_head_; node != nullptr; node = node->next) {
    if (node->guid == guid && node->type == type) {
      node->SetRedirectTarget(position.data());
    }
  }
}

void UnitSpellVisualRuntime::EnsureChainChannelNode() {
  if (chain_channel_node_) {
    return;
  }
  const auto target_guid = owner_.Casts().GetChannelObject(owner_);
  const auto *const objects = owner_.object_manager();
  const auto *const target =
      objects != nullptr ? objects->GetGameObject(target_guid) : nullptr;
  if (!target_guid.IsEmpty() &&
      owner_.Casts().GetChannelSpellId(owner_) != 0u &&
      target != nullptr && target->IsFishingNode()) {
    chain_channel_node_ =
        std::make_unique<ChainChannelVisualNode>(owner_.GetGuid(), target_guid);
  }
}

void UnitSpellVisualRuntime::ResetChainChannelNode() {
  chain_channel_node_.reset();
}

bool UnitSpellVisualRuntime::HasChainChannelNode() const noexcept {
  return chain_channel_node_ != nullptr;
}

void UnitSpellVisualRuntime::RefreshDescriptorChannelVisual(
    const WorldSession& session) {

  static constexpr std::uint32_t kSpellStateDescriptorChannelTarget = 0x8000u;
  owner_.State().ClearSpellStateFlags(kSpellStateDescriptorChannelTarget);

  const auto spell_id = owner_.Casts().GetChannelSpellId(owner_);
  const auto* const dbc = owner_.dbc_loader();
  const auto* const spell =
      dbc != nullptr && spell_id != 0u
          ? dbc->spell().LookupEntry(spell_id)
          : nullptr;
  if (spell == nullptr) {
    return;
  }

  if ((spell->attributes_ex & 0x4000u) != 0u) {
    owner_.State().AddSpellStateFlags(kSpellStateDescriptorChannelTarget);
  }

  auto visual_id = spell->spell_visual[0];
  const auto quality_level = static_cast<std::int32_t>(
      openwow::core::DisplaySettingsController::Instance().GetQualityLevel());
  if (quality_level < 2 && spell->spell_visual[1] != 0u) {
    visual_id = spell->spell_visual[1];
  }
  const auto* const visual =
      visual_id != 0u ? dbc->spell_visual().LookupEntry(visual_id) : nullptr;
  if (visual == nullptr || visual->channel_kit == 0u) {
    return;
  }

  const bool already_pending = std::any_of(
      dispatches_.begin(), dispatches_.end(),
      [spell_id, visual, visual_id](const DispatchRecord& dispatch) {
        return dispatch.lifecycle_action ==
                   SpellVisualLifecycleAction::kChannelStart &&
               dispatch.dispatch_type == 2u &&
               dispatch.spell_id == spell_id &&
               dispatch.spell_visual_id == visual_id &&
               dispatch.kit_id == visual->channel_kit;
      });
  if (already_pending) {
    EnsureChainChannelNode();
    return;
  }

  (void)CreateFromKit(
      session, visual->channel_kit, 2u, nullptr, false, {},
      spell_id, visual_id, SpellVisualPresentationPhase::kEffect,
      SpellVisualLifecycleAction::kChannelStart);
  EnsureChainChannelNode();
}

void UnitSpellVisualRuntime::RecordAnimHitPosition(const float *const position) {
  if (*owner_.GetEffectNodeListHeadSlot() == nullptr || position == nullptr) {
    return;
  }
  owner_.State().SetSpellStateFlags(owner_.State().GetSpellStateFlags() | 0x80000000u);
  std::copy_n(position, pending_anim_hit_position_.size(),
              pending_anim_hit_position_.begin());
}

void UnitSpellVisualRuntime::AdvanceFrame(const std::uint32_t tick_count) {
  if (*owner_.GetEffectNodeListHeadSlot() != nullptr) {
    if ((owner_.State().GetSpellStateFlags() & 0x80000000u) != 0u) {
      for (auto* node = *owner_.GetEffectNodeListHeadSlot(); node != nullptr;
           node = node->GetNextAttachedEffect()) {
        node->SetPosition(pending_anim_hit_position_);
      }
      last_spell_update_time_ = tick_count;
      pending_anim_hit_position_ = {0.0f, 0.0f, 0.0f};
    }
    if (last_spell_update_time_ != 0u &&
        static_cast<std::int32_t>(tick_count - last_spell_update_time_ - 2000u) >=
            0) {
      for (auto* node = *owner_.GetEffectNodeListHeadSlot(); node != nullptr;) {
        auto* const next = node->GetNextAttachedEffect();
        node->BeginTeardown();
        node = next;
      }
      last_spell_update_time_ = 0u;
    }
  }

  auto flags = owner_.State().GetSpellStateFlags() & 0x7FFFFFFFu;
  if ((flags & 0x800u) != 0u) {
    flags &= ~0x800u;
  }
  owner_.State().SetSpellStateFlags(flags);
}

void UnitSpellVisualRuntime::UpdateObjectEffect() {
  const auto *const dbc = owner_.dbc_loader();
  const auto *const display =
      dbc != nullptr
          ? dbc->creature_display_info().LookupEntry(
                 owner_.Presentation().CurrentDisplayId())
          : nullptr;
  const auto package_id =
      display != nullptr ? display->object_effect_package_id : 0u;
  if (package_id == 0u) {
    owner_.ClearObjectEffectPackage();
    return;
  }
  if (const auto *const current = owner_.GetObjectEffect();
      current != nullptr && current->GetBoundPackageId() == package_id) {
    return;
  }
  (void)owner_.BindObjectEffectPackage(package_id);
}

void UnitSpellVisualRuntime::CreateFromCreatureInfo() {
  if ((owner_.State().GetDynamicFlags() & kUnitDynFlagLootable) == 0u ||
      creature_info_active_) {
    return;
  }
  const auto effect_id =
      HardcodedEffectIdTable::GetEffectId(HardcodedEffectId::kLootArt);
  if (effect_id == 0u) {
    return;
  }
  creature_info_active_ = true;
  creature_info_effect_id_ = effect_id;
  if (creature_info_callback_) {
    creature_info_callback_(effect_id, true);
  }
}

void UnitSpellVisualRuntime::Cleanup() {
  ClearCreatureInfo();
  ClearDispatches();

  alpha_fade_effect_nodes_.clear();
  body_tint_effect_nodes_.clear();
  has_looping_alpha_effect_ = false;
  chain_channel_node_.reset();
}

}
