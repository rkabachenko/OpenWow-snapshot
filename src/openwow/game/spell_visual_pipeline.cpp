
#include "openwow/game/spell_visual_pipeline.h"

#include "openwow/data/formats/m2/model_path.h"
#include "openwow/render/m2/m2_system.h"
#include "openwow/vfs/sfile_core.h"

#include <algorithm>
#include <array>

namespace openwow::game {

namespace {

constexpr std::array<std::uint32_t, 11> kImplementedUnitProcTypes = {
    0u, 1u, 6u, 8u, 11u, 12u, 13u, 14u, 15u, 16u, 17u,
};

[[nodiscard]] constexpr bool IsImplementedUnitProcType(
    const std::uint32_t proc_type) noexcept {
  return std::find(kImplementedUnitProcTypes.begin(),
                   kImplementedUnitProcTypes.end(), proc_type) !=
         kImplementedUnitProcTypes.end();
}

[[nodiscard]] bool RequestSpellVisualEffectModelPreloadImpl(
    const openwow::data::dbc::SpellVisualEffectNameEntry &effect, const int queue_index) {
  const std::string model_path =
      openwow::data::m2::NormalizeModelPath(std::string(effect.file_path));
  if (model_path.empty()) {
    return false;
  }

  bool requested_any =
      openwow::vfs::RequestDataPreloadPathAvailability(model_path.c_str(), queue_index, false);

  for (int skin_index = 0; skin_index < 4; ++skin_index) {
    const std::string skin_path = openwow::render::m2::M2System::BuildSkinProfilePath(
        model_path, static_cast<std::uint32_t>(skin_index));

    const bool requested_skin =
        openwow::vfs::RequestDataPreloadPathAvailability(skin_path.c_str(), queue_index, false);
    requested_any |= requested_skin;
    if (!requested_skin) {
      break;
    }
  }

  return requested_any;
}

}

bool RequestSpellVisualEffectModelPreload(
    const openwow::data::dbc::SpellVisualEffectNameEntry &effect, const int queue_index) {
  return RequestSpellVisualEffectModelPreloadImpl(effect, queue_index);
}

bool SpellVisualKitHasKnownProcTypeLayout(const openwow::data::dbc::SpellVisualKitEntry &kit) {
  for (const std::uint32_t proc_type : kit.proc_type) {
    if (IsImplementedUnitProcType(proc_type)) {
      return true;
    }
  }

  return false;
}

bool SpellVisualKitHasProcType(const openwow::data::dbc::SpellVisualKitEntry &kit,
                               const std::uint32_t proc_type) {
  for (const std::uint32_t current_proc_type : kit.proc_type) {
    if (current_proc_type == proc_type) {
      return true;
    }
  }

  return false;
}

bool SpellVisualHasAnyKnownProcTypeKit(
    const openwow::data::dbc::SpellVisualEntry &visual,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::SpellVisualKitEntry> &kit_store) {

  const std::uint32_t kit_ids[] = {
      visual.cast_kit,
      visual.impact_kit,
      visual.state_kit,
      visual.channel_kit,
      visual.caster_impact_kit,
      visual.target_impact_kit,
  };

  for (const std::uint32_t kit_id : kit_ids) {
    const auto *const kit = kit_store.LookupEntry(kit_id);
    if (kit != nullptr && SpellVisualKitHasKnownProcTypeLayout(*kit)) {
      return true;
    }
  }

  return false;
}

const char *VisualAttachmentPointToString(VisualAttachmentPoint pt) {
  switch (pt) {
  case VisualAttachmentPoint::kHead:
    return "Head";
  case VisualAttachmentPoint::kChest:
    return "Chest";
  case VisualAttachmentPoint::kBase:
    return "Base";
  case VisualAttachmentPoint::kLeftHand:
    return "LeftHand";
  case VisualAttachmentPoint::kRightHand:
    return "RightHand";
  case VisualAttachmentPoint::kBreath:
    return "Breath";
  case VisualAttachmentPoint::kLeftWeapon:
    return "LeftWeapon";
  case VisualAttachmentPoint::kRightWeapon:
    return "RightWeapon";
  case VisualAttachmentPoint::kSpecial1:
    return "Special1";
  case VisualAttachmentPoint::kSpecial2:
    return "Special2";
  case VisualAttachmentPoint::kSpecial3:
    return "Special3";
  case VisualAttachmentPoint::kWorld:
    return "World";
  }
  return "Unknown";
}

void SpellVisualPipeline::LoadData(
    const std::vector<openwow::data::dbc::SpellVisualEntry> &visuals,
    const std::vector<openwow::data::dbc::SpellVisualKitEntry> &kits,
    const std::vector<openwow::data::dbc::SpellVisualEffectNameEntry> &effects) {
  visuals_.clear();
  kits_.clear();
  effects_.clear();
  visuals_.reserve(visuals.size());
  kits_.reserve(kits.size());
  effects_.reserve(effects.size());
  for (const auto &v : visuals)
    visuals_[v.id] = v;
  for (const auto &k : kits)
    kits_[k.id] = k;
  for (const auto &e : effects)
    effects_[e.id] = e;
}

std::optional<openwow::data::dbc::SpellVisualEntry>
SpellVisualPipeline::GetVisual(std::uint32_t id) const {
  auto it = visuals_.find(id);
  if (it != visuals_.end())
    return it->second;
  return std::nullopt;
}

std::optional<openwow::data::dbc::SpellVisualKitEntry>
SpellVisualPipeline::GetKit(std::uint32_t id) const {
  auto it = kits_.find(id);
  if (it != kits_.end())
    return it->second;
  return std::nullopt;
}

std::optional<openwow::data::dbc::SpellVisualEffectNameEntry>
SpellVisualPipeline::GetEffectName(std::uint32_t id) const {
  auto it = effects_.find(id);
  if (it != effects_.end())
    return it->second;
  return std::nullopt;
}

std::uint32_t SpellVisualPipeline::GetKitIdForPhase(const openwow::data::dbc::SpellVisualEntry &vis,
                                                    VisualPhase phase) {
  switch (phase) {
  case VisualPhase::kPrecast:
    return vis.precast_kit;
  case VisualPhase::kCast:
    return vis.cast_kit;
  case VisualPhase::kImpact:
    return vis.impact_kit;
  case VisualPhase::kState:
    return vis.state_kit;
  case VisualPhase::kStateDone:
    return vis.state_done_kit;
  case VisualPhase::kChannel:
    return vis.channel_kit;

  case VisualPhase::kCasterImpact:
    return vis.caster_impact_kit;
  case VisualPhase::kTargetImpact:
    return vis.target_impact_kit;
  case VisualPhase::kEffect:
    return vis.persistent_area_kit;
  }
  return 0;
}

std::optional<ResolvedVisualEffect>
SpellVisualPipeline::ResolveEffect(std::uint32_t effect_name_id, std::uint32_t spell_visual_id,
                                   std::uint32_t kit_id, VisualPhase phase,
                                   VisualAttachmentPoint attachment) const {
  if (effect_name_id == 0)
    return std::nullopt;

  auto it = effects_.find(effect_name_id);
  if (it == effects_.end())
    return std::nullopt;

  const auto &eff = it->second;

  ResolvedVisualEffect r;
  r.spell_visual_id = spell_visual_id;
  r.phase = phase;
  r.attachment = attachment;
  r.kit_id = kit_id;
  r.effect_name_id = effect_name_id;

  r.model_path = openwow::data::m2::NormalizeModelPath(std::string(eff.file_path));
  r.texture_path.clear();
  r.scale = eff.scale;

  return r;
}

void SpellVisualPipeline::ResolveKit(std::uint32_t kit_id, std::uint32_t spell_visual_id,
                                     VisualPhase phase,
                                     std::vector<ResolvedVisualEffect> &out) const {
  if (kit_id == 0)
    return;

  auto kit_it = kits_.find(kit_id);
  if (kit_it == kits_.end())
    return;

  const auto &kit = kit_it->second;

  struct AttachmentMapping {
    VisualAttachmentPoint point;
    std::uint32_t effect_id;
  };

  const AttachmentMapping mappings[] = {
      {VisualAttachmentPoint::kHead, kit.head_effect},
      {VisualAttachmentPoint::kChest, kit.chest_effect},
      {VisualAttachmentPoint::kBase, kit.base_effect},
      {VisualAttachmentPoint::kLeftHand, kit.left_hand_effect},
      {VisualAttachmentPoint::kRightHand, kit.right_hand_effect},
      {VisualAttachmentPoint::kBreath, kit.breath_effect},
      {VisualAttachmentPoint::kLeftWeapon, kit.left_weapon_effect},
      {VisualAttachmentPoint::kRightWeapon, kit.right_weapon_effect},

      {VisualAttachmentPoint::kSpecial1, kit.special1_effect},
      {VisualAttachmentPoint::kSpecial2, kit.special2_effect},
      {VisualAttachmentPoint::kSpecial3, kit.special3_effect},
      {VisualAttachmentPoint::kWorld, kit.world_effect},
  };

  for (const auto &m : mappings) {
    if (m.effect_id == 0)
      continue;
    auto resolved = ResolveEffect(m.effect_id, spell_visual_id, kit_id, phase, m.point);
    if (resolved) {
      out.push_back(std::move(*resolved));
    }
  }
}

ResolvedVisualSet SpellVisualPipeline::ResolvePhase(std::uint32_t spell_visual_id,
                                                    VisualPhase phase) const {
  ResolvedVisualSet result;
  result.phase = phase;

  auto vis_it = visuals_.find(spell_visual_id);
  if (vis_it == visuals_.end())
    return result;

  const auto &vis = vis_it->second;
  result.spell_id = 0;

  const std::uint32_t kit_id = GetKitIdForPhase(vis, phase);
  ResolveKit(kit_id, spell_visual_id, phase, result.effects);

  return result;
}

std::vector<ResolvedVisualSet>
SpellVisualPipeline::ResolveAll(std::uint32_t spell_visual_id) const {
  std::vector<ResolvedVisualSet> result;

  static constexpr VisualPhase kAllPhases[] = {
      VisualPhase::kPrecast,      VisualPhase::kCast,         VisualPhase::kImpact,
      VisualPhase::kState,        VisualPhase::kStateDone,    VisualPhase::kChannel,
      VisualPhase::kCasterImpact, VisualPhase::kTargetImpact, VisualPhase::kEffect,
  };

  for (auto phase : kAllPhases) {
    auto set = ResolvePhase(spell_visual_id, phase);
    if (set.HasEffects()) {
      result.push_back(std::move(set));
    }
  }

  return result;
}

std::optional<ResolvedVisualEffect>
SpellVisualPipeline::ResolveMissileVisual(std::uint32_t spell_visual_id) const {
  auto vis_it = visuals_.find(spell_visual_id);
  if (vis_it == visuals_.end())
    return std::nullopt;

  const auto &vis = vis_it->second;
  if (vis.has_missile == 0u || vis.missile_model <= 0) {
    return std::nullopt;
  }

  return ResolveEffect(static_cast<std::uint32_t>(vis.missile_model), spell_visual_id,
                       0u, VisualPhase::kCast, VisualAttachmentPoint::kBase);
}

bool SpellVisualPipeline::RequestKitEffectModelPreloads(const std::uint32_t kit_id,
                                                        const int queue_index) const {
  if (kit_id == 0) {
    return false;
  }

  const auto kit_it = kits_.find(kit_id);
  if (kit_it == kits_.end()) {
    return false;
  }

  const auto &kit = kit_it->second;
  const std::array<std::uint32_t, 12> effect_ids = {
      kit.head_effect,       kit.chest_effect,    kit.base_effect,        kit.left_hand_effect,
      kit.right_hand_effect, kit.breath_effect,   kit.left_weapon_effect, kit.right_weapon_effect,
      kit.special1_effect,   kit.special2_effect, kit.special3_effect,    kit.world_effect,
  };

  bool requested_any = false;
  std::array<std::uint32_t, effect_ids.size()> requested_effect_ids{};
  std::size_t requested_effect_count = 0;

  for (const std::uint32_t effect_id : effect_ids) {
    if (effect_id == 0) {
      continue;
    }

    bool already_requested = false;
    for (std::size_t i = 0; i < requested_effect_count; ++i) {
      if (requested_effect_ids[i] == effect_id) {
        already_requested = true;
        break;
      }
    }
    if (already_requested) {
      continue;
    }
    requested_effect_ids[requested_effect_count++] = effect_id;

    const auto effect_it = effects_.find(effect_id);
    if (effect_it == effects_.end()) {
      continue;
    }

    requested_any |= RequestSpellVisualEffectModelPreloadImpl(effect_it->second, queue_index);
  }

  return requested_any;
}

void SpellVisualPipeline::Clear() {
  visuals_.clear();
  kits_.clear();
  effects_.clear();
}

}
