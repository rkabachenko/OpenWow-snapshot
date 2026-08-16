#pragma once

#include "openwow/data/formats/dbc/dbc_store.h"
#include "openwow/data/formats/dbc/dbc_structures.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::game {

enum class VisualPhase : std::uint8_t {
  kPrecast      = 0,
  kCast         = 1,
  kImpact       = 2,
  kState        = 3,
  kStateDone    = 4,
  kChannel      = 5,

  kCasterImpact = 6,
  kTargetImpact = 7,

  kEffect       = 8,
};

enum class VisualAttachmentPoint : std::uint8_t {
  kHead         = 0,
  kChest        = 1,
  kBase         = 2,
  kLeftHand     = 3,
  kRightHand    = 4,
  kBreath       = 5,
  kLeftWeapon   = 6,
  kRightWeapon  = 7,
  kSpecial1     = 8,
  kSpecial2     = 9,
  kSpecial3     = 10,

  kWorld        = 11,
};

[[nodiscard]] const char* VisualAttachmentPointToString(VisualAttachmentPoint pt);

[[nodiscard]] bool RequestSpellVisualEffectModelPreload(
    const openwow::data::dbc::SpellVisualEffectNameEntry& effect,
    int queue_index = 1);

[[nodiscard]] bool SpellVisualKitHasKnownProcTypeLayout(
    const openwow::data::dbc::SpellVisualKitEntry& kit);

[[nodiscard]] bool SpellVisualKitHasProcType(
    const openwow::data::dbc::SpellVisualKitEntry& kit,
    std::uint32_t proc_type);

[[nodiscard]] bool SpellVisualHasAnyKnownProcTypeKit(
    const openwow::data::dbc::SpellVisualEntry& visual,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::SpellVisualKitEntry>&
        kit_store);

struct ResolvedVisualEffect {
  std::uint32_t spell_id       = 0;
  VisualPhase   phase          = VisualPhase::kCast;
  VisualAttachmentPoint attachment = VisualAttachmentPoint::kBase;

  std::uint32_t spell_visual_id = 0;
  std::uint32_t kit_id          = 0;
  std::uint32_t effect_name_id  = 0;

  std::string   model_path;
  std::string   texture_path;
  float         scale           = 1.0f;
  std::uint32_t model_file_id   = 0;

};

struct ResolvedVisualSet {
  std::uint32_t spell_id = 0;
  VisualPhase   phase    = VisualPhase::kCast;
  std::vector<ResolvedVisualEffect> effects;

  [[nodiscard]] bool HasEffects() const { return !effects.empty(); }

  [[nodiscard]] std::optional<ResolvedVisualEffect> GetEffect(
      VisualAttachmentPoint pt) const {
    for (const auto& e : effects) {
      if (e.attachment == pt) return e;
    }
    return std::nullopt;
  }
};

class SpellVisualPipeline {
 public:

  void LoadData(
      const std::vector<openwow::data::dbc::SpellVisualEntry>& visuals,
      const std::vector<openwow::data::dbc::SpellVisualKitEntry>& kits,
      const std::vector<openwow::data::dbc::SpellVisualEffectNameEntry>& effects);

  [[nodiscard]] ResolvedVisualSet ResolvePhase(
      std::uint32_t spell_visual_id,
      VisualPhase phase) const;

  [[nodiscard]] std::vector<ResolvedVisualSet> ResolveAll(
      std::uint32_t spell_visual_id) const;

  [[nodiscard]] std::optional<openwow::data::dbc::SpellVisualEntry>
  GetVisual(std::uint32_t id) const;

  [[nodiscard]] std::optional<openwow::data::dbc::SpellVisualKitEntry>
  GetKit(std::uint32_t id) const;

  [[nodiscard]] std::optional<openwow::data::dbc::SpellVisualEffectNameEntry>
  GetEffectName(std::uint32_t id) const;

  [[nodiscard]] std::optional<ResolvedVisualEffect> ResolveMissileVisual(
      std::uint32_t spell_visual_id) const;

  [[nodiscard]] bool RequestKitEffectModelPreloads(
      std::uint32_t kit_id,
      int queue_index = 1) const;

  [[nodiscard]] static std::uint32_t GetKitIdForPhase(
      const openwow::data::dbc::SpellVisualEntry& vis,
      VisualPhase phase);

  [[nodiscard]] std::size_t GetVisualCount() const { return visuals_.size(); }
  [[nodiscard]] std::size_t GetKitCount() const { return kits_.size(); }
  [[nodiscard]] std::size_t GetEffectCount() const { return effects_.size(); }

  void Clear();

 private:

  void ResolveKit(std::uint32_t kit_id,
                  std::uint32_t spell_visual_id,
                  VisualPhase phase,
                  std::vector<ResolvedVisualEffect>& out) const;

  [[nodiscard]] std::optional<ResolvedVisualEffect> ResolveEffect(
      std::uint32_t effect_name_id,
      std::uint32_t spell_visual_id,
      std::uint32_t kit_id,
      VisualPhase phase,
      VisualAttachmentPoint attachment) const;

  std::unordered_map<std::uint32_t, openwow::data::dbc::SpellVisualEntry> visuals_;
  std::unordered_map<std::uint32_t, openwow::data::dbc::SpellVisualKitEntry> kits_;
  std::unordered_map<std::uint32_t, openwow::data::dbc::SpellVisualEffectNameEntry> effects_;
};

}
