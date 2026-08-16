
#include "spell_text_formatter.h"

#include "openwow/core/localized_format.h"
#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/game/chat_display.h"
#include "openwow/game/conditional_text_tag.h"
#include "openwow/game/honor_system.h"
#include "openwow/game/localization.h"
#include "openwow/game/object_guid.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/power_lua_bridge.h"
#include "openwow/game/quest_text_parser.h"
#include "openwow/game/spell_c_internals.h"
#include "openwow/game/spell_effective_variant.h"
#include "openwow/game/spellbook_system.h"
#include "openwow/game/update_fields.h"
#include "openwow/game/world_scene_state.h"
#include "openwow/game/world_session.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/objects/cgplayer.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::game {
namespace {

const openwow::data::dbc::DbcLoader* g_spell_text_dbc = nullptr;
const WorldSession* g_spell_text_session = nullptr;

const openwow::data::dbc::DbcLoader* ResolveSpellTextDbc();
const ObjectManager* ResolveSpellTextObjectManager() {
  return g_spell_text_session != nullptr ? &g_spell_text_session->objects()
                                         : nullptr;
}
std::uint32_t ResolveUnitSpellCastTimeDivided(
    const std::uint32_t spell_id, const bool use_pet,
    const bool use_target) {
  const auto* const objects = ResolveSpellTextObjectManager();
  return objects != nullptr
             ? GetUnitSpellCastTimeDivided(
                   *objects, spell_id, use_pet, use_target)
             : 0;
}
std::uint32_t ResolveActiveSpellModifierFamily();
void ApplyTooltipFloatSpellModifier(
    const openwow::data::dbc::SpellEntry& spell,
    SpellModOp op,
    float& value);
bool TryLookupTooltipNamedTag(std::string_view name,
                              std::string_view* replacement,
                              float* expression_value,
                              std::uint32_t* color);

void StrCat(char* dst, std::uint32_t dst_size, const char* src) {
  if (!dst || !src || dst_size == 0) return;
  std::size_t dst_len = std::strlen(dst);
  if (dst_len >= dst_size - 1) return;
  std::strncat(dst, src, dst_size - dst_len - 1);
}

void AppendChar(char* dst, std::uint32_t dst_size, char c) {
  std::size_t len = std::strlen(dst);
  if (len + 1 < dst_size) {
    dst[len] = c;
    dst[len + 1] = '\0';
  }
}

void AppendChars(char* dst, std::uint32_t dst_size, const char* src,
                 std::size_t count) {
  std::size_t len = std::strlen(dst);
  std::size_t space = dst_size - len - 1;
  std::size_t to_copy = std::min(count, space);
  std::memcpy(dst + len, src, to_copy);
  dst[len + to_copy] = '\0';
}

class BoundedCharWriter {
 public:
  BoundedCharWriter(char* buffer, const std::uint32_t capacity)
      : buffer_(buffer), cursor_(buffer), capacity_(capacity) {
    if (buffer_ != nullptr && capacity_ != 0) {
      buffer_[0] = '\0';
    }
  }

  [[nodiscard]] bool CanAppend() const {
    return buffer_ != nullptr && capacity_ > 1 &&
           static_cast<std::uint32_t>(cursor_ - buffer_) < capacity_ - 1;
  }

  void Append(const char ch) {
    if (!CanAppend()) {
      return;
    }

    *cursor_++ = ch;
    *cursor_ = '\0';
  }

  void Append(std::string_view text) {
    if (buffer_ == nullptr || capacity_ == 0 || text.empty()) {
      return;
    }

    const auto written = static_cast<std::uint32_t>(cursor_ - buffer_);
    if (written >= capacity_ - 1) {
      return;
    }

    const std::size_t to_copy =
        std::min<std::size_t>(text.size(), capacity_ - written - 1);
    std::memcpy(cursor_, text.data(), to_copy);
    cursor_ += to_copy;
    *cursor_ = '\0';
  }

  [[nodiscard]] std::string_view View() const {
    if (buffer_ == nullptr) {
      return {};
    }

    return {buffer_, static_cast<std::size_t>(cursor_ - buffer_)};
  }

 private:
  char* buffer_{nullptr};
  char* cursor_{nullptr};
  std::uint32_t capacity_{0};
};

std::string BuildDrunkSpeechCandidate(const char* word_tail) {
  std::string candidate("sh");
  if (word_tail == nullptr) {
    return candidate;
  }

  for (const char* cursor = word_tail; *cursor != '\0' && *cursor != ' '; ++cursor) {
    candidate.push_back(*cursor);
  }

  return candidate;
}

std::string LowercaseAscii(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char ch) {
                   return static_cast<char>(std::tolower(ch));
                 });
  return text;
}

std::string FormatLocalizedInt(const std::string& key,
                               const char* fallback_format,
                               int value) {
  const auto format = Localization::Get().GetString(key, fallback_format);
  std::array<char, 64> buffer{};
  core::FormatLocalized(buffer.data(), buffer.size(), format.c_str(), value);
  return buffer.data();
}

std::string FormatQuestDurationMinutes(int minutes) {
  if (minutes < 1440) {
    if (minutes < 60) {
      return FormatLocalizedInt("INT_GENERAL_DURATION_MIN", "%d min", minutes);
    }

    const auto hours = minutes / 60 + ((minutes % 60) >= 30 ? 1 : 0);
    return FormatLocalizedInt("INT_GENERAL_DURATION_HOURS", "%d hours", hours);
  }

  const auto days = ((minutes - 1) / 1440) + 1;
  return FormatLocalizedInt("INT_GENERAL_DURATION_DAYS", "%d days", days);
}

float ResolveActivePlayerSpellBonusDamage(std::uint8_t school) {
  const auto* const objects = ResolveSpellTextObjectManager();
  const auto* active_player =
      objects != nullptr ? objects->GetActivePlayer() : nullptr;
  if (active_player == nullptr || school > 6) {
    return 0.0f;
  }

  return static_cast<float>(active_player->GetSpellBonusDamage(school));
}

float ResolveMinimumActivePlayerSpellBonusDamage() {
  const auto* const objects = ResolveSpellTextObjectManager();
  const auto* active_player =
      objects != nullptr ? objects->GetActivePlayer() : nullptr;
  if (active_player == nullptr) {
    return 0.0f;
  }

  std::int32_t minimum_bonus = active_player->GetSpellBonusDamage(1);
  for (std::uint8_t school = 2; school <= 6; ++school) {
    minimum_bonus = std::min(
        minimum_bonus,
        active_player->GetSpellBonusDamage(school));
  }
  return static_cast<float>(minimum_bonus);
}

float ResolveActivePlayerModDamageDonePercent(std::uint8_t school) {
  const auto* const objects = ResolveSpellTextObjectManager();
  const auto* active_player =
      objects != nullptr ? objects->GetActivePlayer() : nullptr;
  if (active_player == nullptr || school > 6) {
    return 0.0f;
  }

  return active_player->GetModDamageDonePercent(school);
}

struct TooltipVariableContext {
  const openwow::data::dbc::SpellEntry* spell = nullptr;
  std::int32_t level_override = 0;
  std::int32_t stack_multiplier = 0;
  bool use_target = false;
  bool use_pet = false;
  bool suppress_spell_modifiers = false;
};

struct ExpressionEvalStack {
  std::array<float, 32> values{};
  std::int32_t top = static_cast<std::int32_t>(values.size());
};

constexpr SpellModOp kTooltipEffect0Op = static_cast<SpellModOp>(3);
constexpr SpellModOp kTooltipEffect1Op = static_cast<SpellModOp>(12);
constexpr SpellModOp kTooltipEffect2Op = static_cast<SpellModOp>(23);
constexpr SpellModOp kTooltipAuraBonusOp = static_cast<SpellModOp>(22);
constexpr SpellModOp kTooltipAuraPrimaryOp = static_cast<SpellModOp>(0);
constexpr SpellModOp kTooltipAuraDotOp = static_cast<SpellModOp>(2);
constexpr SpellModOp kTooltipChainTargetOp = static_cast<SpellModOp>(17);
constexpr SpellModOp kTooltipAmplitudeOp = static_cast<SpellModOp>(19);
constexpr SpellModOp kTooltipMultipleValueOp = static_cast<SpellModOp>(27);
constexpr SpellModOp kTooltipBonusCoefficientOp = static_cast<SpellModOp>(24);

struct TooltipEffectScaleFlags {
  bool applies_bonus_modifier = false;
  bool uses_periodic_bonus = false;
};

const CGUnit_C* ResolveTooltipUnit(const CGPlayer_C& active_player,
                                   const TooltipVariableContext& context) {
  const auto* const object_manager = active_player.object_manager();
  if (object_manager == nullptr) {
    return context.use_target || context.use_pet ? nullptr : &active_player;
  }
  if (context.use_target) {
    const auto target_guid = object_manager->GetTargetGuid();
    if (!target_guid.IsEmpty()) {
      return object_manager->GetUnit(target_guid);
    }
    return nullptr;
  }

  if (context.use_pet) {
    const auto pet_guid = active_player.State().GetPetGUID();
    if (!pet_guid.IsEmpty()) {
      return object_manager->GetUnit(pet_guid);
    }
    return nullptr;
  }

  return &active_player;
}

void PushTooltipValue(ExpressionEvalStack& eval_stack, const float value) {
  if (eval_stack.top <= 0) {
    return;
  }

  --eval_stack.top;
  eval_stack.values[static_cast<std::size_t>(eval_stack.top)] = value;
}

std::int32_t ReadSignedField(const CGObject_C& object, const std::uint16_t field) {
  const std::uint32_t raw = object.GetUInt32(field);
  std::int32_t value = 0;
  std::memcpy(&value, &raw, sizeof(value));
  return value;
}

std::int32_t ReadPackedPowerModifierLow(std::uint32_t packed_value) {
  return static_cast<std::int16_t>(packed_value & 0xFFFFu);
}

std::int32_t ReadPackedPowerModifierHigh(std::uint32_t packed_value) {
  return static_cast<std::int16_t>((packed_value >> 16) & 0xFFFFu);
}

void ApplyTooltipIntSpellModifier(
    const openwow::data::dbc::SpellEntry& spell,
    const SpellModOp op,
    std::int32_t& value) {
  if (g_spell_text_session == nullptr) {
    return;
  }

  (void)g_spell_text_session->aura().ApplySpellModifierDeltas(
      ResolveActiveSpellModifierFamily(), spell, op, &value);
}

TooltipEffectScaleFlags ClassifyTooltipEffectScale(
    const openwow::data::dbc::SpellEntry& spell,
    const std::size_t effect_index) {
  TooltipEffectScaleFlags flags;
  if (effect_index >= openwow::data::dbc::kMaxSpellEffects) {
    return flags;
  }

  const auto effect = spell.effect[effect_index];
  const auto aura = spell.effect_apply_aura[effect_index];

  switch (effect) {

    case 2:
    case 9:
    case 10:
    case 17:
    case 31:
    case 58:
    case 67:
    case 75:
    case 121:
      flags.applies_bonus_modifier = true;
      break;

    case 6:
    case 27:
    case 35:
    case 65:
    case 119:
    case 128:
    case 129:
    case 143:
      switch (aura) {

        case 3:
        case 8:
        case 20:
        case 53:
        case 62:
        case 89:
          flags.applies_bonus_modifier = true;
          flags.uses_periodic_bonus = true;
          break;

        case 15:
        case 43:
          flags.applies_bonus_modifier = true;
          break;

        default:
          break;
      }
      break;

    default:
      break;
  }

  return flags;
}

SpellModOp ResolveTooltipEffectOp(const std::size_t effect_index) {
  switch (effect_index) {
    case 0: return kTooltipEffect0Op;
    case 1: return kTooltipEffect1Op;
    default: return kTooltipEffect2Op;
  }
}

bool EffectUsesDotSpellModifier(const openwow::data::dbc::SpellEntry& spell,
                                const std::size_t effect_index) {
  if (effect_index >= openwow::data::dbc::kMaxSpellEffects) {
    return false;
  }

  switch (spell.effect_apply_aura[effect_index]) {
    case 10:
    case 103:
    case 183:
      return true;
    default:
      return false;
  }
}

void ComputeTooltipEffectRange(const openwow::data::dbc::SpellEntry& spell,
                               const std::size_t effect_index,
                               const TooltipVariableContext& context,
                               float& out_min,
                               float& out_max) {
  out_min = 0.0f;
  out_max = 0.0f;
  if (effect_index >= openwow::data::dbc::kMaxSpellEffects) {
    return;
  }

  const auto flags = ClassifyTooltipEffectScale(spell, effect_index);
  std::int32_t scaling_level = context.level_override;
  if (scaling_level == 0) {
    scaling_level = static_cast<std::int32_t>(
        ResolveUnitSpellCastTimeDivided(
            spell.id, context.use_pet, context.use_target));
  }

  if (spell.base_level > 0) {
    scaling_level -= static_cast<std::int32_t>(spell.base_level);
  }
  if (scaling_level < 0) {
    scaling_level = 0;
  }

  out_min = static_cast<float>(spell.effect_base_points[effect_index] + 1)
      + spell.effect_real_points_per_lvl[effect_index]
          * static_cast<float>(scaling_level);
  out_max = static_cast<float>(
                spell.effect_base_points[effect_index]
                + std::max(1, spell.effect_die_sides[effect_index]))
      + spell.effect_real_points_per_lvl[effect_index]
          * static_cast<float>(scaling_level);

  if (!context.suppress_spell_modifiers) {
    ApplyTooltipFloatSpellModifier(spell, SpellModOp::kAllEffects, out_min);
    ApplyTooltipFloatSpellModifier(spell, SpellModOp::kAllEffects, out_max);

    const auto effect_op = ResolveTooltipEffectOp(effect_index);
    ApplyTooltipFloatSpellModifier(spell, effect_op, out_min);
    ApplyTooltipFloatSpellModifier(spell, effect_op, out_max);

    if (flags.applies_bonus_modifier) {
      ApplyTooltipFloatSpellModifier(
          spell,
          flags.uses_periodic_bonus ? kTooltipAuraBonusOp
                                    : kTooltipAuraPrimaryOp,
          out_min);
      ApplyTooltipFloatSpellModifier(
          spell,
          flags.uses_periodic_bonus ? kTooltipAuraBonusOp
                                    : kTooltipAuraPrimaryOp,
          out_max);
    }

    if (EffectUsesDotSpellModifier(spell, effect_index)) {
      ApplyTooltipFloatSpellModifier(spell, kTooltipAuraDotOp, out_min);
      ApplyTooltipFloatSpellModifier(spell, kTooltipAuraDotOp, out_max);
    }
  }
}

std::int32_t ComputeTooltipSpellDurationMs(
    const openwow::data::dbc::SpellEntry& spell,
    const TooltipVariableContext& context) {
  const auto* dbc = ResolveSpellTextDbc();
  if (dbc == nullptr) {
    return 0;
  }

  const auto* entry = dbc->spell_duration().LookupEntry(spell.duration_index);
  if (entry == nullptr) {
    return 0;
  }

  std::int32_t duration = entry->duration;
  const auto scaled_level = static_cast<std::int32_t>(
      ResolveUnitSpellCastTimeDivided(
          spell.id, context.use_pet, context.use_target));
  duration += entry->duration_per_level
      * (scaled_level - static_cast<std::int32_t>(spell.base_level));

  std::int32_t clamped_duration = entry->max_duration;
  if (duration < clamped_duration) {
    clamped_duration = duration;
  }

  if (!context.suppress_spell_modifiers) {
    ApplyTooltipIntSpellModifier(
        spell, SpellModOp::kDuration, clamped_duration);
  }

  return clamped_duration;
}

std::int32_t ComputeTooltipSpellPower(
    const openwow::data::dbc::SpellEntry& spell,
    const TooltipVariableContext& context,
    const CGPlayer_C& active_player) {
  const auto* unit = ResolveTooltipUnit(active_player, context);
  if (unit == nullptr) {
    return 0;
  }

  std::int32_t value = static_cast<std::int32_t>(spell.mana_cost)
      + static_cast<std::int32_t>(spell.mana_cost_per_level)
          * (static_cast<std::int32_t>(
                 ResolveUnitSpellCastTimeDivided(
                     spell.id, context.use_pet, context.use_target))
             / 5
             - static_cast<std::int32_t>(spell.base_level));

  if (spell.school_mask != 0 && active_player.IsActivePlayer()) {
    value += static_cast<std::int32_t>(
        static_cast<float>(
            GetMinimumSpellPowerBonusForSchoolMask(active_player, spell.school_mask))
        * spell.effect_damage_multiplier[0] / 100.0f);
  }

  value = std::max(value, 0);
  return value;
}

std::int32_t ComputeTooltipAttackPower(
    const openwow::data::dbc::SpellEntry& spell,
    const TooltipVariableContext& context,
    const CGPlayer_C& active_player,
    const bool ranged) {
  (void)ranged;
  const auto* unit = ResolveTooltipUnit(active_player, context);
  if (unit == nullptr) {
    return 0;
  }

  std::int32_t value = static_cast<std::int32_t>(spell.mana_cost)
      + static_cast<std::int32_t>(spell.mana_cost_per_level)
          * (static_cast<std::int32_t>(
                 ResolveUnitSpellCastTimeDivided(
                     spell.id, context.use_pet, context.use_target))
             / 5
             - static_cast<std::int32_t>(spell.base_level));
  if (value > 0) {
    ApplyTooltipIntSpellModifier(spell, SpellModOp::kCost, value);
  }

  return value;
}

float GetTooltipWeaponTemplateDamage(const CGPlayer_C& player,
                                     const std::uint8_t slot,
                                     const bool min_damage) {
  const auto entry = player.GetVisibleItemTemplateEntry(slot);
  if (!entry.has_value()) {
    return 0.0f;
  }

  const auto* item = g_spell_text_session != nullptr
                         ? g_spell_text_session->item_definitions().GetItem(*entry)
                         : nullptr;
  if (item == nullptr) {
    return 0.0f;
  }

  return min_damage ? item->damage[0].min_damage : item->damage[0].max_damage;
}

float ResolveTooltipVariableValue(const std::uint32_t var_id,
                                  const TooltipVariableContext& context,
                                  const CGPlayer_C& active_player) {
  const auto* spell = context.spell;

  switch (var_id) {
    case 0x16:
    case 0x17:
    case 0x18:
    case 0x19:
    case 0x1A:
      return static_cast<float>(
          active_player.State().GetNonNegativeStat(static_cast<std::uint8_t>(var_id - 0x16)));
    case 0x1B:
    case 0x1C:
    case 0x1D:
    case 0x1E:
    case 0x1F: {
      const auto stat_index = static_cast<std::uint8_t>(var_id - 0x1B);
      return static_cast<float>(
          active_player.State().GetStat(stat_index)
          - active_player.State().GetPosStat(stat_index)
          - active_player.State().GetNegStat(stat_index));
    }
    case 0x20:
    case 0x21:
    case 0x22:
    case 0x23:
    case 0x24:
    case 0x25: {
      if (spell == nullptr) {
        return 0.0f;
      }

      float min_value = 0.0f;
      float max_value = 0.0f;
      ComputeTooltipEffectRange(
          *spell,
          static_cast<std::size_t>(var_id < 0x23 ? var_id - 0x20 : var_id - 0x23),
          context,
          min_value,
          max_value);
      const float multiplier = static_cast<float>(context.stack_multiplier);
      return (var_id < 0x23 ? min_value : max_value) * multiplier;
    }
    case 0x26:
    case 0x27:
    case 0x28:
    case 0x29:
    case 0x2A:
    case 0x2B: {
      if (spell == nullptr) {
        return 0.0f;
      }

      const auto* dbc = ResolveSpellTextDbc();
      if (dbc == nullptr) {
        return 0.0f;
      }

      const std::size_t effect_index =
          static_cast<std::size_t>(var_id < 0x29 ? var_id - 0x26 : var_id - 0x29);
      float value = 0.0f;
      if (const auto* radius =
              dbc->spell_radius().LookupEntry(spell->effect_radius_index[effect_index])) {
        value = radius->radius;
      }
      ApplyTooltipFloatSpellModifier(*spell, SpellModOp::kRadius, value);
      return value;
    }
    case 0x2C:
    case 0x2D:
      return spell == nullptr
          ? 0.0f
          : static_cast<float>(ComputeTooltipSpellDurationMs(*spell, context)) * 0.001f;
    case 0x2E:
    case 0x2F:
      return spell == nullptr
          ? 0.0f
          : static_cast<float>(ComputeTooltipSpellPower(*spell, context, active_player));
    case 0x30:
    case 0x31:
      return spell == nullptr
          ? 0.0f
          : static_cast<float>(
                ComputeTooltipAttackPower(*spell, context, active_player, false));
    case 0x32:
    case 0x33:
    case 0x34:
    case 0x35:
    case 0x36:
    case 0x37: {
      if (spell == nullptr) {
        return 0.0f;
      }

      const std::size_t effect_index =
          static_cast<std::size_t>(var_id < 0x35 ? var_id - 0x32 : var_id - 0x35);
      std::int32_t value = static_cast<std::int32_t>(spell->effect_chain_target[effect_index]);
      ApplyTooltipIntSpellModifier(*spell, kTooltipChainTargetOp, value);
      return static_cast<float>(value);
    }
    case 0x38:
    case 0x39:
    case 0x3A:
    case 0x3B:
    case 0x3C:
    case 0x3D: {
      if (spell == nullptr) {
        return 0.0f;
      }

      if ((spell->proc_flags & 0x1u) != 0u) {
        return 5000.0f;
      }

      const std::size_t effect_index =
          static_cast<std::size_t>(var_id < 0x3B ? var_id - 0x38 : var_id - 0x3B);
      std::int32_t value = static_cast<std::int32_t>(spell->effect_amplitude[effect_index]);
      ApplyTooltipIntSpellModifier(*spell, kTooltipAmplitudeOp, value);
      return static_cast<float>(value);
    }
    case 0x3E:
    case 0x3F: {
      if (spell == nullptr) {
        return 0.0f;
      }

      std::int32_t value = static_cast<std::int32_t>(spell->proc_chance);

      ApplyTooltipIntSpellModifier(*spell, SpellModOp::kChanceOfSuccess, value);
      return static_cast<float>(value);
    }
    case 0x40:
    case 0x41: {
      if (spell == nullptr) {
        return 0.0f;
      }

      std::int32_t value = static_cast<std::int32_t>(spell->proc_charges);
      ApplyTooltipIntSpellModifier(*spell, SpellModOp::kCharges, value);
      return static_cast<float>(value);
    }
    case 0x42:
    case 0x43:
    case 0x44:
    case 0x45:
    case 0x46:
    case 0x47:
      return spell == nullptr
          ? 0.0f
          : spell->effect_points_per_combo[
                static_cast<std::size_t>(var_id < 0x45 ? var_id - 0x42 : var_id - 0x45)];
    case 0x48:
    case 0x49:
      return spell == nullptr ? 0.0f : static_cast<float>(spell->stack_amount);
    case 0x4A:
    case 0x4B:
      return spell == nullptr ? 0.0f : static_cast<float>(spell->max_affected_targets);
    case 0x4C:
    case 0x4D:
    case 0x4E:
    case 0x4F:
    case 0x50:
    case 0x51: {
      if (spell == nullptr) {
        return 0.0f;
      }

      const std::size_t effect_index =
          static_cast<std::size_t>(var_id < 0x4F ? var_id - 0x4C : var_id - 0x4F);
      float value = spell->effect_value_multiplier[effect_index];
      ApplyTooltipFloatSpellModifier(*spell, kTooltipMultipleValueOp, value);
      return value;
    }
    case 0x52:
    case 0x53:
      return spell == nullptr ? 0.0f : static_cast<float>(spell->max_target_level);
    case 0x54:
    case 0x55:
    case 0x56:
    case 0x57:
    case 0x58:
    case 0x59:
      return spell == nullptr
          ? 0.0f
          : spell->effect_bonus_multiplier[
                static_cast<std::size_t>(var_id < 0x57 ? var_id - 0x54 : var_id - 0x57)];
    case 0x5A:
    case 0x5B:
    case 0x5C:
    case 0x5D:
    case 0x5E:
    case 0x5F:
      return spell == nullptr
          ? 0.0f
          : static_cast<float>(spell->effect_misc_value[
                static_cast<std::size_t>(var_id < 0x5D ? var_id - 0x5A : var_id - 0x5D)]);
    case 0x60:
      return static_cast<float>(
          std::max(ReadSignedField(active_player, UNIT_FIELD_ATTACK_POWER), 0));
    case 0x61: {
      const auto base = static_cast<std::int32_t>(active_player.GetUInt32(UNIT_FIELD_ATTACK_POWER));
      const auto mods = active_player.GetUInt32(UNIT_FIELD_ATTACK_POWER_MODS);
      const auto total = static_cast<float>(
                             base
                             + ReadPackedPowerModifierLow(mods)
                             + ReadPackedPowerModifierHigh(mods))
          * (active_player.GetFloat(UNIT_FIELD_ATTACK_POWER_MULTIPLIER) + 1.0f);
      return std::max(total, 0.0f);
    }
    case 0x62:
      return static_cast<float>(
          std::max(ReadSignedField(active_player, UNIT_FIELD_RANGED_ATTACK_POWER), 0));
    case 0x63: {
      const auto base =
          static_cast<std::int32_t>(active_player.GetUInt32(UNIT_FIELD_RANGED_ATTACK_POWER));
      const auto mods = active_player.GetUInt32(UNIT_FIELD_RANGED_ATTACK_POWER_MODS);
      const auto total = static_cast<float>(
                             base
                             + ReadPackedPowerModifierLow(mods)
                             + ReadPackedPowerModifierHigh(mods))
          * (active_player.GetFloat(UNIT_FIELD_RANGED_ATTACK_POWER_MULTIPLIER) + 1.0f);
      return std::max(total, 0.0f);
    }
    case 0x64: return GetTooltipWeaponTemplateDamage(active_player, 15, true);
    case 0x65: return GetTooltipWeaponTemplateDamage(active_player, 15, false);
    case 0x66: return GetTooltipWeaponTemplateDamage(active_player, 16, true);
    case 0x67: return GetTooltipWeaponTemplateDamage(active_player, 16, false);
    case 0x68: return GetTooltipWeaponTemplateDamage(active_player, 17, true);
    case 0x69: return GetTooltipWeaponTemplateDamage(active_player, 17, false);
    case 0x6A: return std::max(std::floor(active_player.State().GetMinDamage()), 1.0f);
    case 0x6B: return std::max(std::ceil(active_player.State().GetMaxDamage()), 1.0f);
    case 0x6C: return std::max(std::floor(active_player.State().GetMinOffHandDamage()), 1.0f);
    case 0x6D: return std::max(std::ceil(active_player.State().GetMaxOffHandDamage()), 1.0f);
    case 0x6E: return std::max(std::floor(active_player.State().GetMinRangedDamage()), 1.0f);
    case 0x6F: return std::max(std::ceil(active_player.State().GetMaxRangedDamage()), 1.0f);
    case 0x70: {
      const auto armor = active_player.State().GetResistanceDisplayValues(0);
      return static_cast<float>(armor.base_value);
    }
    case 0x71:
      return static_cast<float>(active_player.State().GetResistanceDisplayValues(0).clamped_total);
    case 0x72:
    case 0x73:
        return static_cast<float>(active_player.State().GetAttackSpeed(kAttackSlotMainHand))
          * 0.001f;
    case 0x74:
    case 0x75:
        return static_cast<float>(active_player.State().GetAttackSpeed(kAttackSlotOffHand))
          * 0.001f;
    case 0x76:
    case 0x77:
        return static_cast<float>(active_player.State().GetAttackSpeed(kAttackSlotRanged))
          * 0.001f;
    case 0x78:
    case 0x79:
      return static_cast<float>(active_player.State().GetLevel());
    case 0x7A:
    case 0x7B:
      return active_player.IsVisibleWeaponSlotTwoHandWeapon(15u) ? 2.0f : 1.0f;
    case 0x7C:
    case 0x7D:
      return ResolveMinimumActivePlayerSpellBonusDamage();
    case 0x7E:
    case 0x7F:
    case 0x80:
    case 0x81:
    case 0x82:
    case 0x83:
    case 0x84:
    case 0x85:
    case 0x86:
    case 0x87:
    case 0x88:
    case 0x89:
      return ResolveActivePlayerSpellBonusDamage(
          static_cast<std::uint8_t>(var_id < 0x84 ? var_id - 0x7D : var_id - 0x83));
    case 0x8A:
    case 0x8B:
      return static_cast<float>(active_player.GetUInt32(PLAYER_SHIELD_BLOCK));
    case 0x8C:
    case 0x8D:
    case 0x8E:
    case 0x8F:
    case 0x90:
    case 0x91:
    case 0x92:
    case 0x93:
    case 0x94:
    case 0x95:
    case 0x96:
    case 0x97:
      return ResolveActivePlayerModDamageDonePercent(
          static_cast<std::uint8_t>(var_id < 0x92 ? var_id - 0x8B : var_id - 0x91));
    case 0x98:
    case 0x99:
      return static_cast<float>(
          ReadSignedField(active_player, PLAYER_FIELD_MOD_TARGET_PHYSICAL_RESISTANCE));
    case 0x9A:
    case 0x9B:
      return static_cast<float>(
          ReadSignedField(active_player, PLAYER_FIELD_MOD_TARGET_RESISTANCE));
    case 0x9C:
    case 0x9D:
    case 0x9E:
    case 0x9F:
    case 0xA0:
    case 0xA1: {
      if (spell == nullptr) {
        return 0.0f;
      }

      const std::size_t effect_index =
          static_cast<std::size_t>(var_id < 0x9F ? var_id - 0x9C : var_id - 0x9F);
      float value = spell->effect_damage_multiplier[effect_index];
      ApplyTooltipFloatSpellModifier(*spell, kTooltipBonusCoefficientOp, value);
      return value;
    }
    default:
      return 0.0f;
  }
}

struct ObjectTextContext {
  const CGUnit_C* unit = nullptr;
  const ObjectManager::NameCacheEntry* name_entry = nullptr;
  std::string live_name;
  std::string resolved_name;
  std::string class_name;
  std::string race_name;
  std::uint8_t race_id = 0;
  std::uint8_t class_id = 0;
  std::uint8_t gender = 0;
  int gender_selector = 0;
  int class_selector = 0;
  int race_selector = 0;
  int pvp_rank_faction_selector = -1;
  bool has_object_or_cache = false;
  bool is_player_type = false;
};

int ResolveUnitGenderSelector(const CGUnit_C* unit) {
  if (unit == nullptr) {
    return 0;
  }

  int selector = unit->State().GetGender();
  if (selector == 2 && unit->IsPlayer()) {
    const auto* player = static_cast<const CGPlayer_C*>(unit);
    if (player->IsActivePlayer()) {
      selector = player->GetGenderFromBytes();
    }
  }
  return selector;
}

ConditionalTextTagContext ResolveConditionalTextTagContext(
    const CGUnit_C* unit) {
  ConditionalTextTagContext context;
  context.selector = ResolveUnitGenderSelector(unit);
  context.class_selector = context.selector;
  context.race_selector = context.selector;

  if (unit == nullptr || g_spell_text_dbc == nullptr) {
    return context;
  }

  if (const auto* class_entry =
          g_spell_text_dbc->chr_classes().LookupEntry(unit->State().GetClass())) {
    context.class_selector = static_cast<int>(
        class_entry->ResolveDisplaySex(
            static_cast<std::uint32_t>(context.selector)));
  }

  if (const auto* race_entry =
          g_spell_text_dbc->chr_races().LookupEntry(unit->State().GetRace())) {
    context.race_selector = static_cast<int>(
        race_entry->ResolveDisplaySex(
            static_cast<std::uint32_t>(context.selector)));
  }

  return context;
}

ObjectTextContext ResolveObjectTextContext(std::uint64_t guid,
                                           const char* name_buf) {
  ObjectTextContext context;
  if (guid != 0) {
    const ObjectGuid object_guid(guid);
    if (const auto* const objects = ResolveSpellTextObjectManager();
        objects != nullptr) {
      context.unit = objects->GetUnit(object_guid);
      context.name_entry = objects->GetNameEntry(object_guid);
    }
  }

  context.has_object_or_cache =
      context.unit != nullptr || context.name_entry != nullptr;
  context.is_player_type =
      context.unit != nullptr && (context.unit->GetTypeMask() & 0x10u) != 0;

  if (context.unit != nullptr) {
    context.live_name = context.unit->GetName();
    context.resolved_name = context.live_name;
    context.class_id = context.unit->State().GetClass();
    context.race_id = context.unit->State().GetRace();
    context.gender = context.unit->State().GetGender();
    if (context.unit->IsPlayer() && context.gender == 2) {
      const auto* player = static_cast<const CGPlayer_C*>(context.unit);
      if (player->IsActivePlayer()) {
        context.gender = player->GetGenderFromBytes();
      }
    }
  }

  if (context.name_entry != nullptr) {
    if (context.resolved_name.empty()) {
      context.resolved_name = context.name_entry->name;
    }
    if (context.class_id == 0) {
      context.class_id = context.name_entry->class_id;
    }
    if (context.race_id == 0) {
      context.race_id = context.name_entry->race;
    }
    if (context.gender == 0) {
      context.gender = context.name_entry->gender;
    }
  }

  context.gender_selector = context.gender;
  context.class_selector = context.gender_selector;
  context.race_selector = context.gender_selector;

  if (g_spell_text_dbc != nullptr) {
    if (context.class_id != 0) {
      if (const auto* class_entry =
              g_spell_text_dbc->chr_classes().LookupEntry(context.class_id)) {
        context.class_name = std::string(
            class_entry->DisplayNameForSex(
                static_cast<std::uint32_t>(context.gender_selector)));
        context.class_selector = static_cast<int>(
            class_entry->ResolveDisplaySex(
                static_cast<std::uint32_t>(context.gender_selector)));
      }
    }

    if (context.race_id != 0) {
      if (const auto* race_entry =
              g_spell_text_dbc->chr_races().LookupEntry(context.race_id)) {
        context.race_name = std::string(
            race_entry->DisplayNameForSex(
                static_cast<std::uint32_t>(context.gender_selector)));
        context.race_selector = static_cast<int>(
            race_entry->ResolveDisplaySex(
                static_cast<std::uint32_t>(context.gender_selector)));
        if (const auto* faction_template =
                g_spell_text_dbc->faction_template().LookupEntry(race_entry->faction_id)) {
          if ((faction_template->faction_group & 2u) != 0u) {
            context.pvp_rank_faction_selector = 0;
          } else if ((faction_template->faction_group & 4u) != 0u) {
            context.pvp_rank_faction_selector = 1;
          }
        }
      }
    }
  }

  if (context.class_name.empty() && context.class_id != 0) {
    context.class_name = PowerLuaBridge::ClassNameFromId(context.class_id);
  }
  if (context.race_name.empty() && context.race_id != 0) {
    context.race_name = PowerLuaBridge::RaceNameFromId(context.race_id);
  }
  if (context.resolved_name.empty() && name_buf != nullptr) {
    context.resolved_name = name_buf;
  }

  return context;
}

std::int32_t ResolveWorldStateValue(
    const SpellTextFormatter::WorldStateValueResolver& resolver,
    std::int32_t variable_id) {
  if (!resolver) {
    return 0;
  }
  return resolver(variable_id);
}

bool TryAppendAchievementLink(
    char* output,
    std::uint32_t output_size,
    std::uint64_t guid,
    const SpellTextFormatter::WorldStateValueResolver& resolve_world_state,
    std::int32_t current_time_seconds,
    std::int32_t achievement_id) {
  if (g_spell_text_dbc == nullptr || achievement_id <= 0) {
    return false;
  }

  const auto* achievement =
      g_spell_text_dbc->achievement().LookupEntry(
          static_cast<std::uint32_t>(achievement_id));
  if (achievement == nullptr || achievement->name.empty()) {
    return false;
  }

  std::array<char, 800> expanded_name{};
  SpellTextFormatter::ExpandObjectTextVariables(
      achievement->name.data(),
      expanded_name.data(),
      static_cast<std::uint32_t>(expanded_name.size()),
      guid,
      nullptr,
      0,
      resolve_world_state,
      current_time_seconds,
      0);

  std::array<char, 1024> link{};
  std::snprintf(
      link.data(),
      link.size(),
      "|cffffff00|Hachievement:%d:%016llX:%d:%d:%d:%d:%u:%u:%u:%u|h[%s]|h|r",
      achievement_id,
      static_cast<unsigned long long>(guid),
      1,
      0,
      0,
      -1,
      0xFFFFFFFFu,
      0xFFFFFFFFu,
      0xFFFFFFFFu,
      0xFFFFFFFFu,
      expanded_name.data());
  StrCat(output, output_size, link.data());
  return true;
}

bool TryAppendPvPRankTitle(const ObjectTextContext& context,
                           bool lowercase,
                           char* output,
                           std::uint32_t output_size) {
  if (!context.is_player_type || context.unit == nullptr || !context.unit->IsPlayer()) {
    return false;
  }

  const auto* player = static_cast<const CGPlayer_C*>(context.unit);
  if (!player->IsActivePlayer() || context.pvp_rank_faction_selector < 0) {
    return false;
  }

  const auto rank = HonorSystem::Get().GetHighestPvPRank();
  if (rank == 0 || rank > 14) {
    return false;
  }

  std::array<char, 32> key{};
  std::snprintf(key.data(), key.size(), "PVP_RANK_%u_%d", rank,
                context.pvp_rank_faction_selector);
  if (!Localization::Get().HasString(key.data())) {
    return false;
  }

  auto title = Localization::Get().GetString(key.data());
  if (title.empty()) {
    return false;
  }
  if (lowercase) {
    title = LowercaseAscii(std::move(title));
  }
  StrCat(output, output_size, title.c_str());
  return true;
}

void AppendTrimmedBranch(char* output,
                         std::uint32_t output_size,
                         const char* begin,
                         const char* end) {
  while (begin < end && *begin == ' ') {
    ++begin;
  }
  while (end > begin && end[-1] == ' ') {
    --end;
  }
  AppendChars(output, output_size, begin,
              static_cast<std::size_t>(end - begin));
}

static constexpr int kMaxOpcodes = 120;
static constexpr int kMaxFloats = 64;
static constexpr int kMaxRefs = 32;

static constexpr int kMaxRecordIds = 64;

static constexpr std::uint8_t kOpPushFloat = 0;
static constexpr std::uint8_t kOpDerefRecord = 1;
static constexpr std::uint8_t kOpPushVarRef = 2;
static constexpr std::uint8_t kOpPower = 3;

static constexpr std::uint8_t kOpNegate = 4;

static constexpr std::uint8_t kOpMultiply = 5;

static constexpr std::uint8_t kOpDivide = 6;

static constexpr std::uint8_t kOpModulo = 7;

static constexpr std::uint8_t kOpAdd = 8;

static constexpr std::uint8_t kOpSubtract = 9;

static constexpr std::uint8_t kOpFuncAbs = 0xA;

static constexpr std::uint8_t kOpFuncCeil = 0xB;

static constexpr std::uint8_t kOpFuncFloor = 0xC;

static constexpr std::uint8_t kOpFuncMin = 0xD;

static constexpr std::uint8_t kOpFuncMax = 0xE;

static constexpr std::uint8_t kOpFuncGt = 0xF;

static constexpr std::uint8_t kOpFuncLt = 0x10;

static constexpr std::uint8_t kOpFuncGte = 0x11;

static constexpr std::uint8_t kOpFuncLte = 0x12;

static constexpr std::uint8_t kOpFuncEq = 0x13;

static constexpr std::uint8_t kOpFuncCond = 0x14;

static constexpr std::uint8_t kOpFuncClamp = 0x15;

struct ExpressionParseState {
  std::uint32_t status = 0;
  std::uint32_t opcode_count = 0;
  std::uint32_t float_count = 0;
  std::uint32_t record_count = 0;
  std::uint32_t tag_count = 0;
  std::uint8_t opcodes[kMaxOpcodes] = {};
  float floats[kMaxFloats] = {};
  std::uint32_t record_ids[kMaxRecordIds] = {};
  float tag_values[kMaxRefs] = {};

  void EmitOp(std::uint8_t op) {
    if (opcode_count < kMaxOpcodes) opcodes[opcode_count++] = op;
  }

  void EmitFloat(float val) {
    EmitOp(kOpPushFloat);
    if (float_count < kMaxFloats) floats[float_count++] = val;
  }

  void EmitTagValue(float value) {
    EmitOp(kOpPushVarRef);
    if (tag_count < kMaxRefs) tag_values[tag_count++] = value;
  }
};

static float ApplyBinaryOp(std::uint8_t op, float lhs, float rhs) {
  switch (op) {
    case kOpPower:
      return static_cast<float>(std::pow(lhs, rhs));
    case kOpMultiply:
      return lhs * rhs;
    case kOpDivide:
      return lhs / rhs;
    case kOpModulo:
      return lhs - std::floor(lhs / rhs) * rhs;
    case kOpAdd:
      return lhs + rhs;
    case kOpSubtract:
      return lhs - rhs;
    default: return 0.0f;
  }
}

static float ApplyBuiltinFunc(std::uint8_t op, float a, float b,
                              float c = 0.0f) {
  switch (op) {
    case kOpFuncAbs:
      return std::fabs(a);
    case kOpFuncCeil:
      return static_cast<float>(std::ceil(a));
    case kOpFuncFloor:
      return static_cast<float>(std::floor(a));
    case kOpFuncMin:
      return (a < b) ? a : b;
    case kOpFuncMax:
      return (a > b) ? a : b;
    case kOpFuncGt:
      return (a > b) ? 1.0f : 0.0f;
    case kOpFuncLt:
      return (a < b) ? 1.0f : 0.0f;
    case kOpFuncGte:
      return (a >= b) ? 1.0f : 0.0f;
    case kOpFuncLte:
      return (a <= b) ? 1.0f : 0.0f;
    case kOpFuncEq:
      return (std::fabs(a - b) < 0.000099999997f) ? 1.0f : 0.0f;
    case kOpFuncCond:
      return (a != 0.0f) ? b : c;
    case kOpFuncClamp: {
      float v5 = a;
      float v6 = (b <= a) ? a : b;
      if (c <= v6) {
        v5 = c;
      } else if (b > v5) {
        v5 = b;
      }
      return v5;
    }
    default: return 0.0f;
  }
}

struct BuiltinExpressionSpec {
  std::string_view name;
  std::uint8_t opcode;
  int arity;
};

struct TooltipVariableSpec {
  std::string_view token;
  std::uint8_t opcode;
};

constexpr std::array<BuiltinExpressionSpec, 12> kBuiltinExpressionSpecs{{
    {"abs", kOpFuncAbs, 1},
    {"ceil", kOpFuncCeil, 1},
    {"floor", kOpFuncFloor, 1},
    {"min", kOpFuncMin, 2},
    {"max", kOpFuncMax, 2},
    {"gt", kOpFuncGt, 2},
    {"lt", kOpFuncLt, 2},
    {"gte", kOpFuncGte, 2},
    {"lte", kOpFuncLte, 2},
    {"eq", kOpFuncEq, 2},
    {"cond", kOpFuncCond, 3},
    {"clamp", kOpFuncClamp, 3},
}};

constexpr std::array<TooltipVariableSpec, 0x8C> kTooltipVariableSpecs{{

    {"STR", 0x16}, {"AGI", 0x17}, {"STA", 0x18}, {"INT", 0x19},
    {"SPI", 0x1A}, {"str", 0x1B}, {"agi", 0x1C}, {"sta", 0x1D},
    {"int", 0x1E}, {"spi", 0x1F}, {"m1", 0x20},  {"m2", 0x21},
    {"m3", 0x22},  {"M1", 0x23},  {"M2", 0x24},  {"M3", 0x25},
    {"a1", 0x26},  {"a2", 0x27},  {"a3", 0x28},  {"A1", 0x29},
    {"A2", 0x2A},  {"A3", 0x2B},  {"d", 0x2C},   {"D", 0x2D},
    {"c", 0x2E},   {"C", 0x2F},   {"p", 0x30},   {"P", 0x31},
    {"x1", 0x32},  {"x2", 0x33},  {"x3", 0x34},  {"X1", 0x35},
    {"X2", 0x36},  {"X3", 0x37},  {"t1", 0x38},  {"t2", 0x39},
    {"t3", 0x3A},  {"T1", 0x3B},  {"T2", 0x3C},  {"T3", 0x3D},
    {"h", 0x3E},   {"H", 0x3F},   {"n", 0x40},   {"N", 0x41},
    {"b1", 0x42},  {"b2", 0x43},  {"b3", 0x44},  {"B1", 0x45},
    {"B2", 0x46},  {"B3", 0x47},  {"u", 0x48},   {"U", 0x49},
    {"v", 0x4A},   {"V", 0x4B},   {"e1", 0x4C},  {"e2", 0x4D},
    {"e3", 0x4E},  {"E1", 0x4F},  {"E2", 0x50},  {"E3", 0x51},
    {"i", 0x52},   {"I", 0x53},   {"f1", 0x54},  {"f2", 0x55},
    {"f3", 0x56},  {"F1", 0x57},  {"F2", 0x58},  {"F3", 0x59},
    {"q1", 0x5A},  {"q2", 0x5B},  {"q3", 0x5C},  {"Q1", 0x5D},
    {"Q2", 0x5E},  {"Q3", 0x5F},  {"ap", 0x60},  {"AP", 0x61},
    {"rap", 0x62}, {"RAP", 0x63}, {"mwb", 0x64}, {"MWB", 0x65},
    {"owb", 0x66}, {"OWB", 0x67}, {"rwb", 0x68}, {"RWB", 0x69},
    {"mw", 0x6A},  {"MW", 0x6B},  {"ow", 0x6C},  {"OW", 0x6D},
    {"rw", 0x6E},  {"RW", 0x6F},  {"ar", 0x70},  {"AR", 0x71},
    {"mws", 0x72}, {"MWS", 0x73}, {"ows", 0x74}, {"OWS", 0x75},
    {"rws", 0x76}, {"RWS", 0x77}, {"pl", 0x78},  {"PL", 0x79},
    {"hnd", 0x7A}, {"HND", 0x7B}, {"sp", 0x7C},  {"SP", 0x7D},
    {"sph", 0x7E}, {"spfi", 0x7F}, {"spn", 0x80}, {"spfr", 0x81},
    {"sps", 0x82}, {"spa", 0x83}, {"SPH", 0x84}, {"SPFI", 0x85},
    {"SPN", 0x86}, {"SPFR", 0x87}, {"SPS", 0x88}, {"SPA", 0x89},
    {"bh", 0x8A},  {"BH", 0x8B},  {"ph", 0x8C},  {"pfi", 0x8D},
    {"pn", 0x8E},  {"pfr", 0x8F}, {"ps", 0x90},  {"pa", 0x91},
    {"PH", 0x92},  {"PFI", 0x93}, {"PN", 0x94},  {"PFR", 0x95},
    {"PS", 0x96},  {"PA", 0x97},  {"pbh", 0x98}, {"pBH", 0x99},
    {"pbhd", 0x9A}, {"pBHD", 0x9B}, {"bc1", 0x9C}, {"bc2", 0x9D},
    {"bc3", 0x9E}, {"BC1", 0x9F}, {"BC2", 0xA0}, {"BC3", 0xA1},
}};

bool EqualsIgnoreCase(std::string_view lhs, std::string_view rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }

  for (std::size_t i = 0; i < lhs.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(lhs[i]))
        != std::tolower(static_cast<unsigned char>(rhs[i]))) {
      return false;
    }
  }

  return true;
}

const BuiltinExpressionSpec* FindBuiltinExpressionSpec(std::string_view name) {
  for (const auto& spec : kBuiltinExpressionSpecs) {
    if (EqualsIgnoreCase(name, spec.name)) {
      return &spec;
    }
  }

  return nullptr;
}

const TooltipVariableSpec* FindTooltipVariableSpec(std::string_view token) {
  for (const auto& spec : kTooltipVariableSpecs) {
    if (EqualsIgnoreCase(token, spec.token)) {
      return &spec;
    }
  }

  return nullptr;
}

struct InlineExpressionContext {
  const void* spell_data = nullptr;
  std::int32_t effect_index = 0;
  std::int32_t combo_points = 0;
  std::int32_t stack_count = 0;
  std::int32_t is_periodic = 0;
  std::int32_t is_pet = 0;
};

constexpr std::string_view kExpressionTokenSeparators = "()[]^*/%+-#,> ";

bool IsExpressionTokenSeparator(char c) {
  return kExpressionTokenSeparators.find(c) != std::string_view::npos;
}

const openwow::data::dbc::DbcLoader* ResolveSpellTextDbc() {
  if (g_spell_text_dbc != nullptr) {
    return g_spell_text_dbc;
  }

  return g_spell_text_session != nullptr ? g_spell_text_session->GetDbcLoader()
                                         : nullptr;
}

std::uint32_t ResolveActiveSpellModifierFamily() {
  const auto* dbc = ResolveSpellTextDbc();
  const auto* const objects = ResolveSpellTextObjectManager();
  const auto* active_player =
      objects != nullptr ? objects->GetActivePlayer() : nullptr;
  if (dbc == nullptr || active_player == nullptr) {
    return 0;
  }

  const auto* chr_class = dbc->chr_classes().LookupEntry(active_player->State().GetClass());
  return chr_class != nullptr ? chr_class->spell_family : 0;
}

void ApplyTooltipFloatSpellModifier(const openwow::data::dbc::SpellEntry& spell,
                                    const SpellModOp op,
                                    float& value) {
  if (g_spell_text_session == nullptr) {
    return;
  }

  (void)g_spell_text_session->aura().ApplySpellModifierDeltas(
      ResolveActiveSpellModifierFamily(), spell, op, &value);
}

bool TryResolveSpellBackedTooltipVariable(std::uint8_t opcode,
                                          const InlineExpressionContext& context,
                                          float& value) {
  const auto* const objects = ResolveSpellTextObjectManager();
  const auto* active_player =
      objects != nullptr ? objects->GetActivePlayer() : nullptr;
  if (active_player == nullptr || opcode < 0x16 || opcode > 0xA1) {
    return false;
  }

  TooltipVariableContext variable_context;
  variable_context.spell =
      static_cast<const openwow::data::dbc::SpellEntry*>(context.spell_data);
  variable_context.level_override = context.effect_index;
  variable_context.stack_multiplier = context.stack_count;
  variable_context.use_target = context.combo_points != 0;
  variable_context.use_pet = context.is_pet != 0;
  variable_context.suppress_spell_modifiers = context.is_periodic != 0;
  value = ResolveTooltipVariableValue(opcode, variable_context, *active_player);
  return true;
}

bool TryParseUnsigned(std::string_view text, std::uint32_t& value) {
  if (text.empty()) {
    return false;
  }

  std::uint32_t parsed = 0;
  for (const char ch : text) {
    if (!std::isdigit(static_cast<unsigned char>(ch))) {
      return false;
    }
    parsed = parsed * 10u + static_cast<std::uint32_t>(ch - '0');
  }

  value = parsed;
  return true;
}

const openwow::data::dbc::SpellEntry* ResolveExpressionSpellRecord(
    std::uint32_t spell_id) {
  const auto* dbc = ResolveSpellTextDbc();
  if (dbc == nullptr) {
    return nullptr;
  }

  const auto* spell = dbc->spell().LookupEntry(spell_id);
  if (spell == nullptr || g_spell_text_session == nullptr) {
    return spell;
  }
  const auto* resolved = ResolveEffectiveSpell(*g_spell_text_session, spell_id);
  return resolved != nullptr ? resolved : spell;
}

bool TryResolveTooltipExpressionVariable(std::string_view token,
                                         const InlineExpressionContext& context,
                                         float& value) {
  if (token.empty()) {
    return false;
  }

  const auto* spell =
      static_cast<const openwow::data::dbc::SpellEntry*>(context.spell_data);
  std::size_t digit_count = 0;
  while (digit_count < token.size()
         && std::isdigit(static_cast<unsigned char>(token[digit_count]))) {
    ++digit_count;
  }

  if (digit_count != 0) {

    if (digit_count == token.size()) {
      return false;
    }

    std::uint32_t spell_id = 0;
    if (!TryParseUnsigned(token.substr(0, digit_count), spell_id)) {
      return false;
    }

    spell = ResolveExpressionSpellRecord(spell_id);
    if (spell == nullptr) {
      return false;
    }

    token.remove_prefix(digit_count);
  }

  const std::uint8_t opcode =
      SpellTextFormatter::LookupTooltipVariableOpcode(token);
  if (opcode == 0) {
    return false;
  }

  InlineExpressionContext resolved_context = context;
  resolved_context.spell_data = spell;
  return TryResolveSpellBackedTooltipVariable(opcode, resolved_context, value);
}

bool TryResolveTooltipExpressionTagValue(std::string_view token, float& value) {
  if (!TryLookupTooltipNamedTag(token, nullptr, &value, nullptr)) {
    return false;
  }
  return true;
}

struct ExpressionToken {
  std::string_view text;
  std::size_t next_pos = 0;
  bool valid = false;
};

class TooltipExpressionTokenizer {
 public:
  explicit TooltipExpressionTokenizer(std::string_view input) : input_(input) {}

  ExpressionToken Peek() {
    if (!peek_ready_) {
      peek_token_ = ReadToken(pos_);
      peek_ready_ = true;
    }

    return peek_token_;
  }

  ExpressionToken Consume() {
    const ExpressionToken token = Peek();
    if (token.valid) {
      pos_ = token.next_pos;
    }
    peek_ready_ = false;
    return token;
  }

 private:
  ExpressionToken ReadToken(std::size_t start) const {
    while (start < input_.size() && input_[start] == ' ') {
      ++start;
    }

    if (start >= input_.size()) {
      return {{}, start, false};
    }

    if (IsExpressionTokenSeparator(input_[start])) {
      return {input_.substr(start, 1), start + 1, true};
    }

    std::size_t end = start;
    while (end < input_.size() && !IsExpressionTokenSeparator(input_[end])) {
      ++end;
    }

    return {input_.substr(start, end - start), end, true};
  }

  std::string_view input_;
  std::size_t pos_ = 0;
  bool peek_ready_ = false;
  ExpressionToken peek_token_{};
};

class InlineNumericExpressionParser {
 public:
  InlineNumericExpressionParser(std::string_view input,
                                const InlineExpressionContext& context)
      : tokenizer_(input), context_(context) {}

  bool Evaluate(float& result) {
    ok_ = true;
    result = ParseAdditive();
    return ok_ && !tokenizer_.Peek().valid;
  }

 private:
  float ParseAdditive() {
    float value = ParseMultiplicative();

    while (ok_) {
      if (ConsumeToken("+")) {
        value = ApplyBinaryOp(kOpAdd, value, ParseMultiplicative());
      } else if (ConsumeToken("-")) {
        value = ApplyBinaryOp(kOpSubtract, value, ParseMultiplicative());
      } else {
        break;
      }
    }

    return value;
  }

  float ParseMultiplicative() {
    float value = ParsePower();

    while (ok_) {
      if (ConsumeToken("*")) {
        value = ApplyBinaryOp(kOpMultiply, value, ParsePower());
      } else if (ConsumeToken("/")) {
        value = ApplyBinaryOp(kOpDivide, value, ParsePower());
      } else if (ConsumeToken("%")) {
        value = ApplyBinaryOp(kOpModulo, value, ParsePower());
      } else {
        break;
      }
    }

    return value;
  }

  float ParsePower() {
    float value = ParsePrimary();
    while (ok_ && ConsumeToken("^")) {
      value = ApplyBinaryOp(kOpPower, value, ParsePrimary());
    }
    return value;
  }

  float ParsePrimary() {
    const ExpressionToken token = tokenizer_.Consume();
    if (!token.valid || token.text.empty()) {
      ok_ = false;
      return 0.0f;
    }

    if (token.text == "(") {
      float value = ParseAdditive();
      if (!ConsumeToken(")")) {
        ok_ = false;
      }
      return value;
    }

    if (token.text == "-") {
      return -ParsePrimary();
    }

    const char current = token.text.front();
    if (std::isdigit(static_cast<unsigned char>(current)) || current == '.') {
      return ParseNumberToken(token.text);
    }

    if (current == '$') {
      return ParseDollarToken(token.text.substr(1));
    }

    ok_ = false;
    return 0.0f;
  }

  float ParseNumberToken(std::string_view token) {
    std::string token_copy(token);
    return static_cast<float>(std::atof(token_copy.c_str()));
  }

  float ParseDollarToken(std::string_view token) {
    if (token.empty()) {
      ok_ = false;
      return 0.0f;
    }

    if (token.front() == '<') {
      float value = 0.0f;
      if (!TryResolveTooltipExpressionTagValue(token.substr(1), value)
          || !ConsumeToken(">")) {
        ok_ = false;
        return 0.0f;
      }
      return value;
    }

    const auto* spec = FindBuiltinExpressionSpec(token);
    if (spec != nullptr) {
      if (!ConsumeToken("(")) {
        ok_ = false;
        return 0.0f;
      }

      float args[3] = {};
      for (int i = 0; i < spec->arity; ++i) {
        args[i] = ParseAdditive();
        if (i + 1 < spec->arity && !ConsumeToken(",")) {
          ok_ = false;
          return 0.0f;
        }
      }

      if (!ConsumeToken(")")) {
        ok_ = false;
        return 0.0f;
      }

      return ApplyBuiltinFunc(spec->opcode, args[0], args[1], args[2]);
    }

    float value = 0.0f;
    if (!TryResolveTooltipExpressionVariable(token, context_, value)) {
      ok_ = false;
      return 0.0f;
    }

    return value;
  }

  bool ConsumeToken(std::string_view expected) {
    const ExpressionToken token = tokenizer_.Peek();
    if (!token.valid || token.text != expected) {
      return false;
    }
    tokenizer_.Consume();
    return true;
  }

  TooltipExpressionTokenizer tokenizer_;
  InlineExpressionContext context_;
  bool ok_ = true;
};

struct EvaluatedInlineExpression {
  std::string formatted;
  float float_value = 0.0f;
  std::int32_t integer_value = 0;
};

bool TryEvaluateInlineExpression(const InlineExpressionContext& context,
                                 const char*& cursor,
                                 EvaluatedInlineExpression& result,
                                 const bool advance_on_parse_failure) {
  if (!cursor || *cursor != '{') {
    return false;
  }

  const char* expression_start = cursor;
  const char* close = std::strchr(expression_start + 1, '}');
  if (!close) {
    return false;
  }

  char format[8] = "%.0f";
  const char* next = close + 1;
  if (*next == '.' && next[1] >= '1' && next[1] <= '9') {
    std::snprintf(format, sizeof(format), "%%.%cf", next[1]);
    next += 2;
  }

  if (advance_on_parse_failure) {
    cursor = next;
  }

  InlineNumericExpressionParser parser(
      std::string_view(expression_start + 1,
                       static_cast<std::size_t>(close - expression_start - 1)),
      context);
  float value = 0.0f;
  if (!parser.Evaluate(value)) {
    return false;
  }

  char buffer[32] = {};
  core::FormatLocalized(buffer, sizeof(buffer), format, value);
  result.formatted = buffer;
  result.float_value = value;
  result.integer_value = static_cast<std::int32_t>(value);

  if (!advance_on_parse_failure) {
    cursor = next;
  }

  return true;
}

struct SpellTextRuntimeState {
  std::int32_t last_value = 0;
};

struct SpellTextExpansionContext {
  const void* spell_data = nullptr;
  std::int32_t level_override = 0;
  std::int32_t use_target = 0;
  std::int32_t stack_count = 0;
  std::int32_t suppress_spell_modifiers = 0;
  std::int32_t use_pet = 0;
  SpellTextRuntimeState* runtime = nullptr;
};

struct ConditionalBranchParseResult {
  const char* true_open = nullptr;
  const char* true_close = nullptr;
  const char* next_condition = nullptr;
  const char* false_open = nullptr;
  const char* false_close = nullptr;
  const char* chain_end_close = nullptr;
};

bool ExpandSpellTextSegment(const SpellTextExpansionContext& context,
                            char* output,
                            std::uint32_t output_size,
                            const char*& cursor,
                            bool expand_inner);

InlineExpressionContext MakeInlineExpressionContext(
    const SpellTextExpansionContext& context) {
  return InlineExpressionContext{
      context.spell_data,
      context.level_override,
      context.use_target,
      context.stack_count,
      context.suppress_spell_modifiers,
      context.use_pet};
}

TooltipVariableContext MakeTooltipVariableContext(
    const SpellTextExpansionContext& context,
    const openwow::data::dbc::SpellEntry& spell,
    const std::int32_t level_override) {
  TooltipVariableContext variable_context;
  variable_context.spell = &spell;
  variable_context.level_override = level_override;
  variable_context.stack_multiplier = context.stack_count;
  variable_context.use_target = context.use_target != 0;
  variable_context.use_pet = context.use_pet != 0;
  variable_context.suppress_spell_modifiers =
      context.suppress_spell_modifiers != 0;
  return variable_context;
}

std::int32_t GetSpellTextLastValue(const SpellTextExpansionContext& context) {
  return context.runtime != nullptr ? context.runtime->last_value : 0;
}

void SetSpellTextLastValue(const SpellTextExpansionContext& context,
                           const std::int32_t value) {
  if (context.runtime != nullptr) {
    context.runtime->last_value = value;
  }
}

bool TryExpandInlineExpression(const SpellTextExpansionContext& context,
                               const char*& cursor,
                               char* output,
                               const std::uint32_t output_size) {
  EvaluatedInlineExpression result;
  const auto expression_context = MakeInlineExpressionContext(context);
  if (!TryEvaluateInlineExpression(
          expression_context,
          cursor,
          result,
          false)) {
    return false;
  }

  SetSpellTextLastValue(context, result.integer_value);
  StrCat(output, output_size, result.formatted.c_str());
  return true;
}

bool TryParseUnsignedDecimal(std::string_view text, std::uint32_t& value) {
  if (text.empty()) {
    return false;
  }

  std::uint32_t parsed = 0;
  for (const char ch : text) {
    if (!std::isdigit(static_cast<unsigned char>(ch))) {
      return false;
    }
    parsed = parsed * 10u + static_cast<std::uint32_t>(ch - '0');
  }

  value = parsed;
  return true;
}

std::int32_t ParseSignedDecimalLike(std::string_view text) {
  if (text.empty()) {
    return 0;
  }

  std::size_t index = 0;
  bool negative = false;
  if (text[index] == '-') {
    negative = true;
    ++index;
  }

  if (index >= text.size()) {
    return 0;
  }

  std::uint32_t value = 0;
  for (; index < text.size(); ++index) {
    const char ch = text[index];
    if (!std::isdigit(static_cast<unsigned char>(ch))) {
      return 0;
    }
    value = value * 10u + static_cast<std::uint32_t>(ch - '0');
  }

  std::int32_t signed_value = 0;
  std::memcpy(&signed_value, &value, sizeof(signed_value));
  return negative ? -signed_value : signed_value;
}

struct ParsedSpellTextToken {
  const openwow::data::dbc::SpellEntry* spell = nullptr;
  const char* token_cursor = nullptr;
  std::int32_t level_override = 0;
  float scale = 1.0f;
  std::size_t effect_index = 0;
  bool consumed_effect_digit = false;
};

bool ParseSpellTextToken(
    const SpellTextExpansionContext& context,
    const char*& cursor,
    ParsedSpellTextToken& parsed) {
  if (cursor == nullptr || *cursor == '\0') {
    return false;
  }

  parsed.token_cursor = cursor;
  parsed.scale = 1.0f;
  parsed.level_override = context.level_override;
  parsed.spell = static_cast<const openwow::data::dbc::SpellEntry*>(
      context.spell_data);

  if (*parsed.token_cursor == '*' || *parsed.token_cursor == '/') {
    const char prefix = *parsed.token_cursor;
    const char* const semicolon = std::strchr(parsed.token_cursor, ';');
    if (semicolon != nullptr) {
      const auto length = std::min<std::size_t>(
          static_cast<std::size_t>(semicolon - (parsed.token_cursor + 1)),
          7u);
      const auto value = ParseSignedDecimalLike(
          std::string_view(parsed.token_cursor + 1, length));
      if (value != 0) {
        parsed.scale = static_cast<float>(value);
        if (prefix == '/') {
          parsed.scale = 1.0f / parsed.scale;
        }
      }
      parsed.token_cursor = semicolon + 1;
    }
  }

  const char* spell_digits = parsed.token_cursor;
  while (std::isdigit(static_cast<unsigned char>(*parsed.token_cursor))) {
    ++parsed.token_cursor;
  }

  if (parsed.token_cursor != spell_digits) {
    std::uint32_t spell_id = 0;
    if (!TryParseUnsignedDecimal(
            std::string_view(
                spell_digits,
                static_cast<std::size_t>(parsed.token_cursor - spell_digits)),
            spell_id)) {
      return false;
    }

    parsed.spell = ResolveExpressionSpellRecord(spell_id);
    if (parsed.spell == nullptr) {
      return false;
    }

    if ((parsed.spell->attributes_ex5 & 0x01000000u) != 0u) {
      if (const auto* const objects = ResolveSpellTextObjectManager();
          objects != nullptr) {
        if (const auto* const active_player = objects->GetActivePlayer();
            active_player != nullptr) {
          parsed.level_override =
              static_cast<std::int32_t>(active_player->State().GetLevel());
        }
      }
    }
    if (parsed.spell->max_level > 0 &&
        parsed.level_override >=
            static_cast<std::int32_t>(parsed.spell->max_level)) {
      parsed.level_override = static_cast<std::int32_t>(parsed.spell->max_level);
    }
  }

  if (*parsed.token_cursor == '\0') {
    return false;
  }

  parsed.effect_index = 0;
  parsed.consumed_effect_digit = false;
  if (std::isdigit(static_cast<unsigned char>(parsed.token_cursor[1])) != 0 &&
      parsed.token_cursor[1] >= '1' && parsed.token_cursor[1] <= '9') {
    parsed.consumed_effect_digit = true;
    const auto digit =
        static_cast<std::size_t>(parsed.token_cursor[1] - '1');
    parsed.effect_index = digit < 3 ? digit : 0;
  }

  cursor = parsed.token_cursor;
  return true;
}

void AdvanceParsedSpellTextTokenCursor(const ParsedSpellTextToken& parsed,
                                       const char*& cursor) {
  cursor = parsed.token_cursor + 1;
  if (parsed.consumed_effect_digit) {
    ++cursor;
  }
}

void AppendFormattedString(char* output,
                           const std::uint32_t output_size,
                           const char* format,
                           const float first,
                           const float second) {
  std::array<char, 64> buffer{};
  core::FormatLocalized(buffer.data(), buffer.size(), format, first, second);
  StrCat(output, output_size, buffer.data());
}

void AppendFormattedString(char* output,
                           const std::uint32_t output_size,
                           const char* format,
                           const std::int32_t first,
                           const std::int32_t second) {
  std::array<char, 64> buffer{};
  core::FormatLocalized(buffer.data(), buffer.size(), format, first, second);
  StrCat(output, output_size, buffer.data());
}

std::int32_t RoundHalfAwayFromZero(const float value) {
  return value <= 0.0f
      ? static_cast<std::int32_t>(value - 0.5f)
      : static_cast<std::int32_t>(value + 0.5f);
}

std::int32_t CeilToInt(const float value) {
  return static_cast<std::int32_t>(std::ceil(value));
}

bool IsNearWholeDisplayDuration(const float duration_ms, const float epsilon) {
  double converted = 0.0;
  if (duration_ms >= 86400000.0f) {
    converted = static_cast<double>(duration_ms) * 0.000000011574074;
  } else if (duration_ms >= 3600000.0f) {
    converted = static_cast<double>(duration_ms) * 0.00000027777779;
  } else if (duration_ms >= 60000.0f) {
    converted = static_cast<double>(duration_ms) * 0.000016666667;
  } else {
    converted = static_cast<double>(duration_ms) * 0.001;
  }
  return static_cast<float>(converted - std::floor(converted)) < epsilon;
}

std::string LookupLocalizedWithFallback(std::string_view key,
                                        std::string_view fallback) {
  const auto localized =
      Localization::Get().GetString(std::string(key), std::string(fallback));
  return localized.empty() ? std::string(fallback) : localized;
}

void FormatDurationTextRounded(char* output,
                               const std::uint32_t output_size,
                               const std::uint64_t duration,
                               std::string_view prefix_key,
                               const std::int32_t count_param,
                               const bool round_up,
                               const bool use_second_units) {
  if (output == nullptr || output_size == 0 || prefix_key.empty()) {
    return;
  }

  output[0] = '\0';
  const std::uint64_t base_unit = use_second_units ? 60u : 60000u;

  std::uint64_t value = duration;
  std::string suffix = "_SEC";
  if (duration >= 1440u * base_unit) {
    const auto divisor = 1440u * base_unit;
    value = round_up ? ((duration - 1u) / divisor) + 1u : duration / divisor;
    suffix = "_DAYS";
  } else if (duration >= 60u * base_unit) {
    const auto divisor = 60u * base_unit;
    value = round_up ? ((duration - 1u) / divisor) + 1u : duration / divisor;
    if (value == 24u) {
      value = 1u;
      suffix = "_DAYS";
    } else {
      suffix = "_HOURS";
    }
  } else if (duration >= base_unit) {
    value = round_up ? ((duration - 1u) / base_unit) + 1u : duration / base_unit;
    if (value == 60u) {
      value = 1u;
      suffix = "_HOURS";
    } else {
      suffix = "_MIN";
    }
  } else if (!use_second_units) {
    value = duration / 1000u;
  }

  const std::string key = std::string(prefix_key) + suffix;
  const auto format = LookupLocalizedWithFallback(
      key,
      suffix == "_DAYS"
          ? "%u"
          : suffix == "_HOURS" ? "%u" : suffix == "_MIN" ? "%u" : "%u");
  if (count_param != 0) {
    core::FormatLocalized(output, output_size, format.c_str(), count_param,
                          static_cast<std::uint32_t>(value));
  } else {
    core::FormatLocalized(output, output_size, format.c_str(),
                          static_cast<std::uint32_t>(value));
  }
}

void FormatDurationTextWithFraction(char* output,
                                    const std::uint32_t output_size,
                                    float duration_ms,
                                    std::string_view prefix_key,
                                    const std::int32_t count_param) {
  if (output == nullptr || output_size == 0 || prefix_key.empty()) {
    return;
  }

  output[0] = '\0';
  const float hours_ms = 3600000.0f;
  const float days_ms = 24.0f * hours_ms;

  float value = duration_ms;
  std::string suffix = "_SEC";
  if (duration_ms >= days_ms) {
    value = duration_ms / days_ms;
    suffix = "_DAYS";
  } else if (duration_ms >= hours_ms) {
    value = duration_ms / hours_ms;
    suffix = "_HOURS";
  } else if (duration_ms >= 60000.0f) {
    value = duration_ms * 0.000016666667f;
    suffix = "_MIN";
  } else {
    value = duration_ms * 0.001f;
  }

  if (value < 0.0f) {
    value = 0.0f;
  }

  const std::string key = std::string(prefix_key) + suffix;
  const auto format = LookupLocalizedWithFallback(
      key,
      suffix == "_DAYS"
          ? "%.1f"
          : suffix == "_HOURS" ? "%.1f" : suffix == "_MIN" ? "%.1f" : "%.1f");
  if (count_param != 0) {
    core::FormatLocalized(output, output_size, format.c_str(), count_param, value);
  } else {
    core::FormatLocalized(output, output_size, format.c_str(), value);
  }
}

std::int32_t ComputeTooltipManaPerSecond(
    const openwow::data::dbc::SpellEntry& spell,
    const SpellTextExpansionContext& context,
    const CGPlayer_C& active_player) {
  (void)active_player;
  std::int32_t value =
      static_cast<std::int32_t>(spell.mana_per_second) +
      static_cast<std::int32_t>(spell.mana_per_second_per_level) *
          (static_cast<std::int32_t>(
               ResolveUnitSpellCastTimeDivided(
                   spell.id, context.use_pet != 0, context.use_target != 0)) /
               5 -
           static_cast<std::int32_t>(spell.base_level));

  if (value > 0 && g_spell_text_session != nullptr) {
    std::int32_t flat_delta = 0;
    std::int32_t pct_total = 100;
    if (g_spell_text_session->aura().AccumulateSpellModifierDeltas(
            ResolveActiveSpellModifierFamily(),
            spell,
            SpellModOp::kCost,
            &flat_delta,
            &pct_total)) {
      value = static_cast<std::int32_t>(
          (static_cast<std::int64_t>(value) * pct_total) / 100);
    }
  }

  return value;
}

std::string ResolveSpellTextBindLocationText() {
  const auto* dbc = ResolveSpellTextDbc();
  if (g_spell_text_session != nullptr && dbc != nullptr) {
    const auto area_id = g_spell_text_session->misc().bind_point().area_id;
    if (const auto* area = dbc->area_table().LookupEntry(area_id);
        area != nullptr) {
      return std::string(area->name);
    }
  }

  return Localization::Get().GetString("HOME_INN");
}

bool TryExpandEffectRangeToken(const SpellTextExpansionContext& context,
                               const ParsedSpellTextToken& parsed,
                               char* output,
                               const std::uint32_t output_size) {
  if (parsed.spell == nullptr) {
    return false;
  }

  const auto* dbc = ResolveSpellTextDbc();
  if (dbc == nullptr) {
    return false;
  }

  std::int32_t value = 0;
  if (const auto* radius =
          dbc->spell_radius().LookupEntry(
              parsed.spell->effect_radius_index[parsed.effect_index]);
      radius != nullptr) {
    value = static_cast<std::int32_t>(radius->radius);
    ApplyTooltipIntSpellModifier(*parsed.spell, SpellModOp::kRadius, value);
  }

  SetSpellTextLastValue(context, value);
  std::array<char, 32> buffer{};
  std::snprintf(buffer.data(), buffer.size(), "%d", value);
  StrCat(output, output_size, buffer.data());
  return true;
}

bool TryExpandPeriodicValueToken(const SpellTextExpansionContext& context,
                                 const ParsedSpellTextToken& parsed,
                                 const char token,
                                 char* output,
                                 const std::uint32_t output_size) {
  if (parsed.spell == nullptr) {
    return false;
  }

  auto variable_context =
      MakeTooltipVariableContext(context, *parsed.spell, parsed.level_override);
  float minimum = 0.0f;
  float maximum = 0.0f;
  ComputeTooltipEffectRange(
      *parsed.spell, parsed.effect_index, variable_context, minimum, maximum);

  minimum *= static_cast<float>(context.stack_count);
  maximum *= static_cast<float>(context.stack_count);

  if (token == 'o' || token == 'O') {
    std::int32_t amplitude =
        static_cast<std::int32_t>(parsed.spell->effect_amplitude[parsed.effect_index]);
    if (amplitude == 0) {
      amplitude = 5000;
    } else if (amplitude < 0) {
      amplitude = 0;
    }

    if (amplitude > 0) {
      const auto duration = ComputeTooltipSpellDurationMs(
          *parsed.spell, variable_context);
      if (duration > 0) {
        std::int32_t adjusted_duration = duration;
        if ((parsed.spell->attributes_ex5 & 0x200u) != 0u) {
          adjusted_duration += amplitude;
        }
        minimum = minimum * static_cast<float>(adjusted_duration) /
                  static_cast<float>(amplitude);
        maximum = maximum * static_cast<float>(adjusted_duration) /
                  static_cast<float>(amplitude);
      } else {
        minimum = 0.0f;
        maximum = 0.0f;
      }
    } else {
      minimum = 0.0f;
      maximum = 0.0f;
    }
  }

  const float scaled_minimum = std::fabs(minimum) * parsed.scale;
  const float scaled_maximum = std::fabs(maximum) * parsed.scale;

  auto classify_value =
      [](const float value, std::int32_t& rounded, bool& integral) {
        const float floor_value = std::floor(value);
        const float floor_delta = value - floor_value;
        rounded = static_cast<std::int32_t>(floor_value);
        integral = false;
        if (floor_delta < 0.001f) {
          integral = true;
          return;
        }

        const float ceil_value = std::ceil(value);
        if (ceil_value - value < 0.001f) {
          integral = true;
          if (rounded != static_cast<std::int32_t>(ceil_value)) {
            ++rounded;
          }
        }
      };

  std::int32_t rounded_minimum = 0;
  std::int32_t rounded_maximum = 0;
  bool minimum_integral = false;
  bool maximum_integral = false;
  classify_value(scaled_minimum, rounded_minimum, minimum_integral);
  classify_value(scaled_maximum, rounded_maximum, maximum_integral);

  if (token == 'm') {
    SetSpellTextLastValue(
        context, minimum_integral ? rounded_minimum : 2);
    std::array<char, 32> buffer{};
    if (minimum_integral) {
      std::snprintf(buffer.data(), buffer.size(), "%d", rounded_minimum);
    } else {
      std::snprintf(buffer.data(), buffer.size(), "%.1f", scaled_minimum);
    }
    StrCat(output, output_size, buffer.data());
    return true;
  }

  if (token == 'M') {
    SetSpellTextLastValue(
        context, maximum_integral ? rounded_maximum : 2);
    std::array<char, 32> buffer{};
    if (maximum_integral) {
      std::snprintf(buffer.data(), buffer.size(), "%d", rounded_maximum);
    } else {
      std::snprintf(buffer.data(), buffer.size(), "%.1f", scaled_maximum);
    }
    StrCat(output, output_size, buffer.data());
    return true;
  }

  if (scaled_minimum == scaled_maximum) {
    SetSpellTextLastValue(
        context, minimum_integral ? rounded_minimum : 2);
    std::array<char, 32> buffer{};
    if (minimum_integral || token != 'S') {
      if (minimum_integral) {
        std::snprintf(buffer.data(), buffer.size(), "%d", rounded_minimum);
      } else {
        std::snprintf(buffer.data(), buffer.size(), "%.1f", scaled_minimum);
      }
    } else {
      std::snprintf(buffer.data(), buffer.size(), "%.1f", scaled_minimum);
    }
    StrCat(output, output_size, buffer.data());
    return true;
  }

  if (minimum_integral && maximum_integral) {
    SetSpellTextLastValue(context, rounded_maximum);
    const auto format = LookupLocalizedWithFallback(
        "INT_SPELL_POINTS_SPREAD_TEMPLATE", "%d to %d");
    AppendFormattedString(
        output, output_size, format.c_str(), rounded_minimum, rounded_maximum);
    return true;
  }

  if (token == 'S') {
    SetSpellTextLastValue(context, 2);
    const auto format = LookupLocalizedWithFallback(
        "SPELL_POINTS_SPREAD_TEMPLATE", "%.1f to %.1f");
    AppendFormattedString(
        output, output_size, format.c_str(), scaled_minimum, scaled_maximum);
    return true;
  }

  SetSpellTextLastValue(context, 2);
  const auto format = LookupLocalizedWithFallback(
      "INT_SPELL_POINTS_SPREAD_TEMPLATE", "%d to %d");
  const auto upper_bound =
      maximum_integral ? rounded_maximum
                       : static_cast<std::int32_t>(scaled_maximum) + 1;
  AppendFormattedString(
      output,
      output_size,
      format.c_str(),
      static_cast<std::int32_t>(scaled_minimum),
      upper_bound);
  return true;
}

bool ParseConditionalBranchChain(const char* input,
                                 ConditionalBranchParseResult& result) {
  if (input == nullptr) {
    return false;
  }

  result.true_open = std::strchr(input, '[');
  if (result.true_open == nullptr) {
    return false;
  }

  result.true_close = std::strchr(result.true_open, ']');
  if (result.true_close == nullptr) {
    return false;
  }

  if (result.true_close[1] == '?') {
    result.next_condition = result.true_close + 2;
    ConditionalBranchParseResult nested_result;
    if (!ParseConditionalBranchChain(result.next_condition, nested_result)) {
      return false;
    }
    result.chain_end_close = nested_result.chain_end_close;
    return true;
  }

  result.false_open = std::strchr(result.true_close, '[');
  if (result.false_open == nullptr) {
    return false;
  }

  result.false_close = std::strchr(result.false_open, ']');
  if (result.false_close == nullptr) {
    return false;
  }

  result.chain_end_close = result.false_close;
  return true;
}

bool EvaluateConditionalAtom(char opcode,
                             const char*& cursor,
                             const char* end) {
  const auto* const objects = ResolveSpellTextObjectManager();
  const auto* active_player =
      objects != nullptr ? objects->GetActivePlayer() : nullptr;
  if (active_player == nullptr) {
    return false;
  }

  std::uint32_t spell_id = 0;
  const char* digits = cursor;
  while (digits < end && *digits != '\0'
         && std::isdigit(static_cast<unsigned char>(*digits))) {
    spell_id = spell_id * 10u + static_cast<std::uint32_t>(*digits - '0');
    ++digits;
  }

  if (digits == cursor) {
    return false;
  }

  cursor = digits;
  switch (opcode) {
    case 'a':
    case 'A':
      return active_player->Auras().FindBySpellId(spell_id) != nullptr;

    case 's':
    case 'S':
      return SpellbookSystem::Get().HasSpell(spell_id);

    default:
      return false;
  }
}

void SkipConditionalParenGroup(const char*& cursor, const char* end) {
  int depth = 1;
  while (cursor < end && *cursor != '\0') {
    if (*cursor == '(') {
      ++depth;
    } else if (*cursor == ')') {
      --depth;
      ++cursor;
      if (depth == 0) {
        return;
      }
      continue;
    }
    ++cursor;
  }
}

bool EvaluateConditionalExpression(const char*& cursor, const char* end) {
  bool value = false;

  while (cursor < end && *cursor != '\0') {
    switch (*cursor) {
      case '!': {
        ++cursor;
        if (cursor >= end || *cursor == '\0') {
          return value;
        }

        while (cursor < end && *cursor != '\0') {
          if (*cursor == 's' || *cursor == 'S' || *cursor == 'a'
              || *cursor == 'A') {
            const char opcode = *cursor++;
            value = !EvaluateConditionalAtom(opcode, cursor, end);
            break;
          }

          if (*cursor == '(') {
            ++cursor;
            value = !EvaluateConditionalExpression(cursor, end);
            SkipConditionalParenGroup(cursor, end);
            break;
          }

          ++cursor;
        }
        break;
      }

      case '&':
        if (!value) {
          return false;
        }
        ++cursor;
        value = EvaluateConditionalExpression(cursor, end);
        break;

      case '(':
        ++cursor;
        value = EvaluateConditionalExpression(cursor, end);
        SkipConditionalParenGroup(cursor, end);
        break;

      case ')':
        return value;

      case 'a':
      case 'A':
      case 's':
      case 'S': {
        const char opcode = *cursor++;
        value = EvaluateConditionalAtom(opcode, cursor, end);
        break;
      }

      case '|':
        if (value) {
          return true;
        }
        ++cursor;
        value = EvaluateConditionalExpression(cursor, end);
        break;

      default:
        ++cursor;
        break;
    }
  }

  return value;
}

void CopyConditionalBranchText(const char* begin,
                               const char* end,
                               std::array<char, 1024>& buffer) {
  std::size_t count = 0;
  if (begin != nullptr && end != nullptr && end >= begin) {
    count = static_cast<std::size_t>(end - begin);
    count = std::min(count, buffer.size() - 1);
    std::memcpy(buffer.data(), begin, count);
  }
  buffer[count] = '\0';
}

bool ExpandBracketConditionalToken(const SpellTextExpansionContext& context,
                                   char* output,
                                   std::uint32_t output_size,
                                   const char*& cursor,
                                   bool expand_inner) {
  while (*cursor == ' ') {
    ++cursor;
  }

  if (*cursor == '\0') {
    return true;
  }

  ConditionalBranchParseResult parse_result;
  if (!ParseConditionalBranchChain(cursor, parse_result)) {
    return true;
  }

  const char* condition_cursor = cursor;
  const bool condition_true =
      EvaluateConditionalExpression(condition_cursor, parse_result.true_open);

  if (!condition_true && parse_result.next_condition != nullptr) {
    const bool resolved = ExpandBracketConditionalToken(
        context, output, output_size, parse_result.next_condition, expand_inner);
    cursor = parse_result.chain_end_close != nullptr
                 ? parse_result.chain_end_close + 1
                 : parse_result.next_condition;
    return resolved;
  }

  const char* branch_begin = nullptr;
  const char* branch_end = nullptr;
  if (condition_true) {
    branch_begin = parse_result.true_open + 1;
    branch_end = parse_result.true_close;
  } else if (parse_result.false_open != nullptr && parse_result.false_close != nullptr) {
    branch_begin = parse_result.false_open + 1;
    branch_end = parse_result.false_close;
  }

  std::array<char, 1024> branch_buffer{};
  CopyConditionalBranchText(branch_begin, branch_end, branch_buffer);

  bool resolved = true;
  if (expand_inner) {
    const char* dollar = std::strchr(branch_buffer.data(), '$');
    if (dollar != nullptr) {
      const char* brace = std::strchr(dollar, '{');
      if (brace != nullptr && std::strchr(brace, '}') != nullptr) {
        const char* expression_cursor = brace;
        TryExpandInlineExpression(context, expression_cursor, output, output_size);
      }
    }
  } else {
    const char* branch_cursor = branch_buffer.data();
    resolved = ExpandSpellTextSegment(
        context, output, output_size, branch_cursor, false);
    if (!resolved) {
      StrCat(output, output_size, "$");
    }
  }

  cursor = parse_result.chain_end_close != nullptr
               ? parse_result.chain_end_close + 1
               : cursor;
  return resolved;
}

bool TryExpandSpellTextToken(const SpellTextExpansionContext& context,
                             char* output,
                             std::uint32_t output_size,
                             const char*& cursor,
                             bool expand_inner) {
  if (cursor == nullptr || *cursor == '\0') {
    return false;
  }

  const char* token_start = cursor;

  if (*cursor == '{') {
    if (TryExpandInlineExpression(context, cursor, output, output_size)) {
      return true;
    }
    cursor = token_start;
    return false;
  }

  if (*cursor == '<') {
    const char* close = std::strchr(cursor + 1, '>');
    if (close == nullptr) {
      cursor = token_start;
      return false;
    }

    std::uint32_t color = 0;
    if (SpellTextFormatter::LookupColorTag(
            close, output, output_size, &cursor, &color)) {
      SetSpellTextLastValue(context, static_cast<std::int32_t>(color));
      return true;
    }

    cursor = token_start;
    return false;
  }

  if (*cursor == '?') {
    ++cursor;
    return ExpandBracketConditionalToken(
        context, output, output_size, cursor, expand_inner);
  }

  ParsedSpellTextToken parsed;
  if (!ParseSpellTextToken(context, cursor, parsed)) {
    return false;
  }

  const char var_char = *parsed.token_cursor;
  switch (var_char) {
    case 'a':
    case 'B':
    case 'C':
    case 'D':
    case 'o':
    case 'e':
    case 'F':
    case 'm':
    case 'P':
    case 's':
    case 'A':
    case 'E':
    case 'H':
    case 'I':
    case 'M':
    case 'N':
    case 'O':
    case 'Q':
    case 'R':
    case 'S':
    case 'T':
    case 'U':
    case 'V':
    case 'X':
    case 'b':
    case 'c':
    case 'd':
    case 'f':
    case 'h':
    case 'i':
    case 'n':
    case 'p':
    case 'q':
    case 'r':
    case 't':
    case 'u':
    case 'v':
    case 'x':
      break;
    case 'g':
    case 'G': {
      cursor = parsed.token_cursor + 1;
      const auto* const objects = ResolveSpellTextObjectManager();
      const auto* active_player =
          objects != nullptr ? objects->GetActivePlayer() : nullptr;
      if (active_player == nullptr) {
        return false;
      }

      ConditionalTextTagSelection selection;
      if (!TrySelectConditionalTextTag(
              std::string_view(cursor),
              ResolveConditionalTextTagContext(active_player),
              &selection)) {
        return false;
      }

      AppendChars(
          output, output_size, selection.text.data(), selection.text.size());
      cursor += selection.consumed;
      return true;
    }

    case 'l':
    case 'L': {
      cursor = parsed.token_cursor + 1;
      return QuestTextParser::ParsePluralFormField(
          &cursor,
          output,
          output_size,
          static_cast<std::int32_t>(GetSpellTextLastValue(context)));
    }

    case 'z':
    case 'Z':
      StrCat(output, output_size, ResolveSpellTextBindLocationText().c_str());
      AdvanceParsedSpellTextTokenCursor(parsed, cursor);
      return true;

    default:
      return false;
  }

  if (parsed.spell == nullptr) {
    return false;
  }

  const auto* const objects = ResolveSpellTextObjectManager();
  const auto* active_player =
      objects != nullptr ? objects->GetActivePlayer() : nullptr;
  const auto variable_context =
      MakeTooltipVariableContext(context, *parsed.spell, parsed.level_override);

  switch (var_char) {
    case 'A':
    case 'a':
      if (!TryExpandEffectRangeToken(context, parsed, output, output_size)) {
        return false;
      }
      break;

    case 'B':
    case 'b': {
      const auto value = static_cast<std::int32_t>(
          parsed.spell->effect_points_per_combo[parsed.effect_index]);
      SetSpellTextLastValue(context, value);
      std::array<char, 32> buffer{};
      std::snprintf(buffer.data(), buffer.size(), "%d", value);
      StrCat(output, output_size, buffer.data());
      break;
    }

    case 'C':
    case 'c': {
      if (active_player == nullptr) {
        return false;
      }
      const auto value =
          ComputeTooltipSpellPower(*parsed.spell, variable_context, *active_player);
      std::array<char, 32> buffer{};
      std::snprintf(buffer.data(), buffer.size(), "%d", value);
      StrCat(output, output_size, buffer.data());
      break;
    }

    case 'D':
    case 'd': {
      const auto duration_ms =
          ComputeTooltipSpellDurationMs(*parsed.spell, variable_context);
      std::array<char, 64> buffer{};
      if (duration_ms <= 0) {
        const auto until_cancelled =
            Localization::Get().GetString("SPELL_DURATION_UNTIL_CANCELLED");
        StrCat(output, output_size, until_cancelled.c_str());
      } else {
        const auto duration_ms_float = static_cast<float>(duration_ms);
        if (IsNearWholeDisplayDuration(duration_ms_float, 0.01f)) {
          FormatDurationTextRounded(
              buffer.data(),
              static_cast<std::uint32_t>(buffer.size()),
              static_cast<std::uint64_t>(duration_ms),
              "INT_SPELL_DURATION",
              0,
              false,
              false);
        } else {
          FormatDurationTextWithFraction(
              buffer.data(),
              static_cast<std::uint32_t>(buffer.size()),
              duration_ms_float,
              "SPELL_DURATION",
              0);
        }
        StrCat(output, output_size, buffer.data());
      }
      break;
    }

    case 'E':
    case 'e': {
      float value =
          parsed.spell->effect_value_multiplier[parsed.effect_index];
      ApplyTooltipFloatSpellModifier(
          *parsed.spell, kTooltipMultipleValueOp, value);
      value *= parsed.scale;
      std::array<char, 32> buffer{};
      std::snprintf(buffer.data(), buffer.size(), "%.1f", value);
      StrCat(output, output_size, buffer.data());
      break;
    }

    case 'F':
    case 'f': {
      const float value =
          parsed.spell->effect_bonus_multiplier[parsed.effect_index] * parsed.scale;
      std::array<char, 32> buffer{};
      if (var_char == 'F') {
        std::snprintf(
            buffer.data(), buffer.size(), "%d", RoundHalfAwayFromZero(value));
      } else {
        std::snprintf(buffer.data(), buffer.size(), "%.1f", value);
      }
      StrCat(output, output_size, buffer.data());
      break;
    }

    case 'H':
    case 'h': {
      std::int32_t value = static_cast<std::int32_t>(parsed.spell->proc_chance);
      ApplyTooltipIntSpellModifier(*parsed.spell, SpellModOp::kChanceOfSuccess,
                                   value);
      SetSpellTextLastValue(context, value);
      std::array<char, 32> buffer{};
      std::snprintf(buffer.data(), buffer.size(), "%d", value);
      StrCat(output, output_size, buffer.data());
      break;
    }

    case 'I':
    case 'i': {
      const auto value =
          static_cast<std::int32_t>(parsed.spell->max_affected_targets);
      SetSpellTextLastValue(context, value);
      std::array<char, 32> buffer{};
      std::snprintf(buffer.data(), buffer.size(), "%d", value);
      StrCat(output, output_size, buffer.data());
      break;
    }

    case 'M':
    case 'O':
    case 'S':
    case 'm':
    case 'o':
    case 's':
      if (!TryExpandPeriodicValueToken(
              context, parsed, var_char, output, output_size)) {
        return false;
      }
      break;

    case 'N':
    case 'n': {
      std::int32_t value = static_cast<std::int32_t>(parsed.spell->proc_charges);
      ApplyTooltipIntSpellModifier(*parsed.spell, SpellModOp::kCharges, value);
      SetSpellTextLastValue(context, value);
      std::array<char, 32> buffer{};
      std::snprintf(buffer.data(), buffer.size(), "%d", value);
      StrCat(output, output_size, buffer.data());
      break;
    }

    case 'P':
    case 'p': {
      if (active_player == nullptr) {
        return false;
      }
      const auto value =
          ComputeTooltipManaPerSecond(*parsed.spell, context, *active_player);
      std::array<char, 32> buffer{};
      std::snprintf(buffer.data(), buffer.size(), "%d", value);
      StrCat(output, output_size, buffer.data());
      break;
    }

    case 'Q':
    case 'q': {
      const auto value = parsed.spell->effect_misc_value[parsed.effect_index];
      SetSpellTextLastValue(context, value);
      std::array<char, 32> buffer{};
      std::snprintf(buffer.data(), buffer.size(), "%d", value);
      StrCat(output, output_size, buffer.data());
      break;
    }

    case 'R':
    case 'r': {
      const auto* dbc = ResolveSpellTextDbc();
      if (dbc == nullptr) {
        return false;
      }
      std::uint32_t range_index = parsed.spell->range_index;
      if (range_index <= 1u) {
        range_index = 1u;
      }
      float value = 0.0f;
      if (const auto* range = dbc->spell_range().LookupEntry(range_index);
          range != nullptr) {
        value = range->range_max;
        ApplyTooltipFloatSpellModifier(*parsed.spell, SpellModOp::kRange, value);
      }
      SetSpellTextLastValue(context, CeilToInt(value));
      std::array<char, 32> buffer{};
      std::snprintf(buffer.data(), buffer.size(), "%d",
                    static_cast<std::int32_t>(value));
      StrCat(output, output_size, buffer.data());
      break;
    }

    case 'T':
    case 't': {
      std::int32_t amplitude = 0;
      if ((parsed.spell->proc_flags & 0x1u) != 0u) {
        amplitude = 5000;
      } else {
        amplitude = static_cast<std::int32_t>(
            parsed.spell->effect_amplitude[parsed.effect_index]);
        ApplyTooltipIntSpellModifier(
            *parsed.spell, kTooltipAmplitudeOp, amplitude);

        bool has_any_amplitude = false;
        for (const auto effect_amplitude : parsed.spell->effect_amplitude) {
          if (effect_amplitude != 0u) {
            has_any_amplitude = true;
            break;
          }
        }

        if (has_any_amplitude &&
            (parsed.spell->attributes_ex2 & 0x2000u) != 0u &&
            (parsed.spell->attributes_ex3 & 0x20000000u) == 0u &&
            active_player != nullptr) {
          const float spell_haste = active_player->State().GetSpellHaste();
          if (spell_haste >= 0.001f) {
            amplitude =
                static_cast<std::int32_t>(static_cast<float>(amplitude) * spell_haste);
          }
        }
      }

      std::array<char, 32> buffer{};
      if (IsNearWholeDisplayDuration(
              static_cast<float>(amplitude), 0.01f)) {
        std::snprintf(buffer.data(), buffer.size(), "%d", amplitude / 1000);
      } else {
        std::snprintf(
            buffer.data(),
            buffer.size(),
            "%.2f",
            static_cast<double>(amplitude) * 0.001);
      }
      StrCat(output, output_size, buffer.data());
      break;
    }

    case 'U':
    case 'u': {
      const auto value = static_cast<std::int32_t>(parsed.spell->stack_amount);
      SetSpellTextLastValue(context, value);
      std::array<char, 32> buffer{};
      std::snprintf(buffer.data(), buffer.size(), "%d", value);
      StrCat(output, output_size, buffer.data());
      break;
    }

    case 'V':
    case 'v': {
      const auto value = static_cast<std::int32_t>(parsed.spell->max_target_level);
      SetSpellTextLastValue(context, value);
      std::array<char, 32> buffer{};
      std::snprintf(buffer.data(), buffer.size(), "%d", value);
      StrCat(output, output_size, buffer.data());
      break;
    }

    case 'X':
    case 'x': {
      std::int32_t value = static_cast<std::int32_t>(
          parsed.spell->effect_chain_target[parsed.effect_index]);
      ApplyTooltipIntSpellModifier(*parsed.spell, kTooltipChainTargetOp, value);
      SetSpellTextLastValue(context, value);
      std::array<char, 32> buffer{};
      std::snprintf(buffer.data(), buffer.size(), "%d", value);
      StrCat(output, output_size, buffer.data());
      break;
    }

    default:
      return false;
  }

  AdvanceParsedSpellTextTokenCursor(parsed, cursor);
  (void)expand_inner;
  return true;
}

bool ExpandSpellTextSegment(const SpellTextExpansionContext& context,
                            char* output,
                            std::uint32_t output_size,
                            const char*& cursor,
                            bool expand_inner) {
  if (cursor == nullptr) {
    return false;
  }

  bool all_resolved = true;
  const char* segment_start = cursor;

  while (const char* dollar = std::strchr(segment_start, '$')) {
    if (dollar != segment_start) {
      AppendChars(
          output,
          output_size,
          segment_start,
          static_cast<std::size_t>(dollar - segment_start));
    }

    const char* token_cursor = dollar + 1;
    if (!TryExpandSpellTextToken(
            context, output, output_size, token_cursor, expand_inner)) {
      AppendChar(output, output_size, '$');
      all_resolved = false;
    }

    segment_start = token_cursor;
  }

  StrCat(output, output_size, segment_start);
  cursor = segment_start + std::strlen(segment_start);
  return all_resolved;
}

}

std::uint8_t SpellTextFormatter::LookupTooltipVariableOpcode(
    std::string_view token) {
  if (token.empty()) {
    return 0;
  }

  if (const auto* spec = FindTooltipVariableSpec(token)) {
    return spec->opcode;
  }

  return 0;
}

void BindSpellTextFormatterDbcLoader(
    const openwow::data::dbc::DbcLoader* dbc) {
  g_spell_text_dbc = dbc;
}

void BindSpellTextFormatterWorldSession(const WorldSession* session) {
  g_spell_text_session = session;
}

namespace {

struct TooltipNamedTagEntry {
  const char* name;
  const char* replacement;
  float expression_value;
  std::uint32_t color;
};

struct RuntimeTooltipNamedTagEntry {
  std::string name;
  std::string replacement;
  float expression_value = 0.0f;
  std::uint32_t color = 0;
};

static constexpr float TagExpressionValue(std::uint32_t color) {
  return static_cast<float>(color);
}

static const TooltipNamedTagEntry kTooltipNamedTags[] = {
    {"Red", "|cffff2020", TagExpressionValue(0xFFFF2020u), 0xFFFF2020u},
    {"Green", "|cff20ff20", TagExpressionValue(0xFF20FF20u), 0xFF20FF20u},
    {"Blue", "|cff2020ff", TagExpressionValue(0xFF2020FFu), 0xFF2020FFu},
    {"White", "|cffffffff", TagExpressionValue(0xFFFFFFFFu), 0xFFFFFFFFu},
    {"Yellow", "|cffffff00", TagExpressionValue(0xFFFFFF00u), 0xFFFFFF00u},
    {"Orange", "|cffff8000", TagExpressionValue(0xFFFF8000u), 0xFFFF8000u},
    {"Purple", "|cffb048f8", TagExpressionValue(0xFFB048F8u), 0xFFB048F8u},
    {"Gray", "|cff808080", TagExpressionValue(0xFF808080u), 0xFF808080u},
    {"Grey", "|cff808080", TagExpressionValue(0xFF808080u), 0xFF808080u},
};

std::vector<RuntimeTooltipNamedTagEntry>& ActiveTooltipNamedTags() {
  static std::vector<RuntimeTooltipNamedTagEntry> tags;
  return tags;
}

const RuntimeTooltipNamedTagEntry* FindRuntimeTooltipNamedTag(
    std::string_view name) {
  if (name.empty()) {
    return nullptr;
  }

  for (const auto& entry : ActiveTooltipNamedTags()) {
    if (EqualsIgnoreCase(name, entry.name)) {
      return &entry;
    }
  }

  return nullptr;
}

const TooltipNamedTagEntry* FindStaticTooltipNamedTag(std::string_view name) {
  if (name.empty()) {
    return nullptr;
  }

  for (const auto& entry : kTooltipNamedTags) {
    if (EqualsIgnoreCase(name, entry.name)) {
      return &entry;
    }
  }

  return nullptr;
}

bool TryLookupTooltipNamedTag(std::string_view name,
                              std::string_view* replacement,
                              float* expression_value,
                              std::uint32_t* color) {
  if (const auto* runtime_entry = FindRuntimeTooltipNamedTag(name)) {
    if (replacement != nullptr) {
      *replacement = runtime_entry->replacement;
    }
    if (expression_value != nullptr) {
      *expression_value = runtime_entry->expression_value;
    }
    if (color != nullptr) {
      *color = runtime_entry->color;
    }
    return true;
  }

  const auto* static_entry = FindStaticTooltipNamedTag(name);
  if (static_entry == nullptr) {
    return false;
  }

  if (replacement != nullptr) {
    *replacement = static_entry->replacement;
  }
  if (expression_value != nullptr) {
    *expression_value = static_entry->expression_value;
  }
  if (color != nullptr) {
    *color = static_entry->color;
  }
  return true;
}

std::string_view TruncateTooltipNamedTag(std::string_view name) {
  constexpr std::size_t kMaxTooltipTagLength = 14;
  return name.substr(0, std::min(name.size(), kMaxTooltipTagLength));
}

void RegisterTooltipNamedTag(std::vector<RuntimeTooltipNamedTagEntry>& entries,
                             std::string_view tag_name,
                             const EvaluatedInlineExpression& expression) {
  const std::string_view truncated_name = TruncateTooltipNamedTag(tag_name);
  if (truncated_name.empty()) {
    return;
  }

  RuntimeTooltipNamedTagEntry entry;
  entry.name.assign(truncated_name.data(), truncated_name.size());
  entry.replacement = expression.formatted;
  entry.expression_value = expression.float_value;
  entry.color = static_cast<std::uint32_t>(expression.integer_value);
  entries.push_back(std::move(entry));
}

void RegisterTooltipNamedTagFromInlineExpression(
    const InlineExpressionContext& expression_context,
    const std::string_view tag_name,
    const char*& cursor,
    std::vector<RuntimeTooltipNamedTagEntry>& entries) {
  EvaluatedInlineExpression expression;
  if (!TryEvaluateInlineExpression(
          expression_context,
          cursor,
          expression,
          true)) {
    return;
  }

  RegisterTooltipNamedTag(entries, tag_name, expression);
}

void RegisterTooltipNamedTagFromConditionalBranch(
    const SpellTextExpansionContext& context,
    const std::string_view tag_name,
    const char*& cursor,
    std::vector<RuntimeTooltipNamedTagEntry>& entries) {
  while (*cursor == ' ') {
    ++cursor;
  }

  if (*cursor == '\0') {
    return;
  }

  ConditionalBranchParseResult parse_result;
  if (!ParseConditionalBranchChain(cursor, parse_result)) {
    return;
  }

  const char* condition_cursor = cursor;
  const bool condition_true =
      EvaluateConditionalExpression(condition_cursor, parse_result.true_open);

  if (!condition_true && parse_result.next_condition != nullptr) {
    const char* next_condition = parse_result.next_condition;
    RegisterTooltipNamedTagFromConditionalBranch(
        context, tag_name, next_condition, entries);
    cursor = parse_result.chain_end_close != nullptr
                 ? parse_result.chain_end_close + 1
                 : next_condition;
    return;
  }

  const char* branch_begin = nullptr;
  const char* branch_end = nullptr;
  if (condition_true) {
    branch_begin = parse_result.true_open + 1;
    branch_end = parse_result.true_close;
  } else if (parse_result.false_open != nullptr
             && parse_result.false_close != nullptr) {
    branch_begin = parse_result.false_open + 1;
    branch_end = parse_result.false_close;
  }

  std::array<char, 1024> branch_buffer{};
  CopyConditionalBranchText(branch_begin, branch_end, branch_buffer);
  if (const char* dollar = std::strchr(branch_buffer.data(), '$')) {
    if (const char* brace = std::strchr(dollar, '{')) {
      const InlineExpressionContext expression_context =
          MakeInlineExpressionContext(context);
      const char* expression_cursor = brace;
      RegisterTooltipNamedTagFromInlineExpression(
          expression_context, tag_name, expression_cursor, entries);
    }
  }

  cursor = parse_result.chain_end_close != nullptr
               ? parse_result.chain_end_close + 1
               : cursor;
}

std::vector<RuntimeTooltipNamedTagEntry> BuildTooltipNamedTagDefinitions(
    const SpellTextExpansionContext& context,
    const char* named_tag_definitions) {
  std::vector<RuntimeTooltipNamedTagEntry> entries;
  if (named_tag_definitions == nullptr) {
    return entries;
  }

  const InlineExpressionContext expression_context =
      MakeInlineExpressionContext(context);
  const char* cursor = std::strchr(named_tag_definitions, '$');
  while (cursor != nullptr && *cursor != '\0') {
    if (cursor[1] == '\0') {
      break;
    }

    const char* name_start = cursor + 1;
    cursor = name_start;
    const char* equals = std::strchr(name_start, '=');
    if (equals != nullptr) {
      const std::string_view tag_name(
          name_start, static_cast<std::size_t>(equals - name_start));
      const char* value_dollar = std::strchr(name_start, '$');
      cursor = value_dollar;
      if (value_dollar != nullptr) {
        cursor = value_dollar + 1;
        if (*cursor == '?') {
          ++cursor;
          RegisterTooltipNamedTagFromConditionalBranch(
              context, tag_name, cursor, entries);
        } else if (*cursor == '{') {
          RegisterTooltipNamedTagFromInlineExpression(
              expression_context, tag_name, cursor, entries);
        }
      }
    }

    if (cursor != nullptr && *cursor != '\0') {
      cursor = std::strchr(cursor, '$');
    }
  }

  return entries;
}

class ScopedTooltipNamedTagDefinitions {
 public:
  explicit ScopedTooltipNamedTagDefinitions(
      std::vector<RuntimeTooltipNamedTagEntry> entries)
      : previous_(ActiveTooltipNamedTags()) {
    ActiveTooltipNamedTags() = std::move(entries);
  }

  ~ScopedTooltipNamedTagDefinitions() {
    ActiveTooltipNamedTags() = std::move(previous_);
  }

 private:
  std::vector<RuntimeTooltipNamedTagEntry> previous_;
};

}

bool SpellTextFormatter::ExpandSimpleIntegerVariable(
    const void* data,
    char* output,
    std::uint32_t output_size,
    std::int32_t int_value) {
  if (!data || !output || output_size == 0) return false;

  const char* format =
      *reinterpret_cast<const char* const*>(
          reinterpret_cast<const std::uint8_t*>(data) + 56);

  if (!format) {
    output[0] = '\0';
    return false;
  }

  output[0] = '\0';
  bool all_resolved = true;
  const char* p = format;

  while (*p) {

    const char* dollar = std::strchr(p, '$');
    if (!dollar) {

      StrCat(output, output_size, p);
      break;
    }

    if (dollar > p) {
      AppendChars(output, output_size, p, static_cast<std::size_t>(dollar - p));
    }

    if (dollar[1] == 'i') {

      char num_buf[32];
      std::snprintf(num_buf, sizeof(num_buf), "%d", int_value);
      StrCat(output, output_size, num_buf);
      p = dollar + 2;
    } else {

      AppendChar(output, output_size, '$');
      p = dollar + 1;
      all_resolved = false;
    }
  }

  return all_resolved;
}

int SpellTextFormatter::LookupColorTag(
    const char* end_pos,
    char* output,
    std::uint32_t output_size,
    const char** pos_ptr,
    std::uint32_t* color_out) {
  if (!end_pos || !output || !pos_ptr || !*pos_ptr) return 0;

  const char* const tag_begin = *pos_ptr;
  const auto raw_length = end_pos - tag_begin;
  if (raw_length <= 0) return 0;

  constexpr std::size_t kLookupBufferSize = 16;
  const std::size_t copy_size =
      std::min<std::size_t>(static_cast<std::size_t>(raw_length),
                            kLookupBufferSize - 1);

  char tag_name[kLookupBufferSize]{};
  if (copy_size > 1) {
    std::memcpy(tag_name, tag_begin + 1, copy_size - 1);
  }

  std::string_view replacement;
  std::uint32_t color = 0;
  if (!TryLookupTooltipNamedTag(tag_name, &replacement, nullptr, &color)) {
    return 0;
  }

  const std::string replacement_text(replacement);
  StrCat(output, output_size, replacement_text.c_str());
  if (color_out) {
    *color_out = color;
  }
  *pos_ptr = end_pos + 1;
  return 1;
}

int SpellTextFormatter::ApplyDrunkSpeechFilter(
    openwow::audio::SoundRuntime& sound_runtime,
    const char* input,
    char* output,
    std::uint32_t output_size,
    float drunk_level) {
  if (input == nullptr || output == nullptr || output_size == 0) {
    return 0;
  }

  BoundedCharWriter writer(output, output_size);
  auto& sound_interface = sound_runtime;
  bool at_word_start = true;

  for (const char* cursor = input; *cursor != '\0' && writer.CanAppend();) {
    const char current = *cursor++;
    writer.Append(current);

    switch (current) {
    case ' ':
      at_word_start = true;
      continue;
    case '|':
      if (*cursor == 'c') {
        if (const char* color_end = std::strstr(cursor, "|r"); color_end != nullptr) {
          writer.Append(
              std::string_view(cursor, static_cast<std::size_t>(color_end - cursor + 2)));
          cursor = color_end + 2;
        }
      }
      at_word_start = false;
      continue;
    case 'S':
    case 's':
      break;
    default:
      at_word_start = false;
      continue;
    }

    if (!writer.CanAppend()) {
      at_word_start = false;
      continue;
    }

    if (!at_word_start) {
      if (*cursor == ' ' &&
          sound_interface.ConsumePlaybackRandomUnitFloat() < drunk_level) {
        writer.Append('h');
      }
      at_word_start = false;
      continue;
    }

    if (*cursor == '\0' || *cursor == ' ' || *cursor == 'S' || *cursor == 's') {
      at_word_start = false;
      continue;
    }

    if (sound_interface.ConsumePlaybackRandomUnitFloat() >= drunk_level) {
      at_word_start = false;
      continue;
    }

    std::string slurred_word = BuildDrunkSpeechCandidate(cursor);
    if (!ChatFrame_MatureLanguageFilter(slurred_word, false, true)) {
      writer.Append('h');
    }

    at_word_start = false;
  }

  if (drunk_level * 0.25f > sound_interface.ConsumePlaybackRandomUnitFloat()) {
    const std::string wrapped = Localization::Get().FormatString(
        Localization::Get().GetString("SLURRED_SPEECH", "SLURRED_SPEECH"),
        {std::string(writer.View())});
    BoundedCharWriter wrapped_writer(output, output_size);
    wrapped_writer.Append(wrapped);
  }

  return 1;
}

double SpellTextFormatter::EvaluateTooltipExpression(
    void* parser_state_ptr,
    const void* spell_data,
    std::int32_t effect_index,
    std::int32_t combo_points,
    std::int32_t stack_count,
    std::int32_t is_periodic,
    std::int32_t is_pet) {
  if (!parser_state_ptr) return 0.0;

  auto* state = static_cast<ExpressionParseState*>(parser_state_ptr);
  state->status = 0;

  ExpressionEvalStack eval_stack;
  int float_idx = 0;
  int record_idx = 0;
  int tag_idx = 0;
  const void* active_spell_data = spell_data;
  bool has_dereferenced_record = false;

  for (std::uint32_t i = 0; i < state->opcode_count; ++i) {
    std::uint8_t op = state->opcodes[i];

    switch (op) {
      case kOpPushFloat:

        if (eval_stack.top > 0 && float_idx < static_cast<int>(state->float_count)) {
          PushTooltipValue(eval_stack, state->floats[float_idx++]);
        }
        break;

      case kOpDerefRecord:

        if (record_idx >= static_cast<int>(state->record_count)) {
          state->status = 1;
          return 0.0;
        }
        active_spell_data =
            ResolveExpressionSpellRecord(state->record_ids[record_idx++]);
        if (active_spell_data == nullptr) {
          state->status = 1;
          return 0.0;
        }
        has_dereferenced_record = true;
        break;

      case kOpPushVarRef:

        if (eval_stack.top > 0 && tag_idx < static_cast<int>(state->tag_count)) {
          PushTooltipValue(eval_stack, state->tag_values[tag_idx++]);
        } else {
          state->status = 1;
          return 0.0;
        }
        break;

      case kOpPower:
      case kOpMultiply:
      case kOpDivide:
      case kOpModulo:
      case kOpAdd:
      case kOpSubtract:

        if (eval_stack.top <= static_cast<std::int32_t>(eval_stack.values.size()) - 2) {
          const float lhs = eval_stack.values[static_cast<std::size_t>(eval_stack.top)];
          const float rhs =
              eval_stack.values[static_cast<std::size_t>(eval_stack.top + 1)];
          eval_stack.values[static_cast<std::size_t>(eval_stack.top + 1)] =
              ApplyBinaryOp(op, lhs, rhs);
          ++eval_stack.top;
        }
        break;

      case kOpNegate:

        if (eval_stack.top < static_cast<std::int32_t>(eval_stack.values.size())) {
          eval_stack.values[static_cast<std::size_t>(eval_stack.top)] =
              -eval_stack.values[static_cast<std::size_t>(eval_stack.top)];
        }
        break;

      case kOpFuncAbs:
      case kOpFuncCeil:
      case kOpFuncFloor:

        if (eval_stack.top < static_cast<std::int32_t>(eval_stack.values.size())) {
          eval_stack.values[static_cast<std::size_t>(eval_stack.top)] =
              ApplyBuiltinFunc(
                  op,
                  eval_stack.values[static_cast<std::size_t>(eval_stack.top)],
                  0.0f);
        }
        break;

      case kOpFuncMin:
      case kOpFuncMax:
      case kOpFuncGt:
      case kOpFuncLt:
      case kOpFuncGte:
      case kOpFuncLte:
      case kOpFuncEq:

        if (eval_stack.top <= static_cast<std::int32_t>(eval_stack.values.size()) - 2) {
          const float a = eval_stack.values[static_cast<std::size_t>(eval_stack.top)];
          const float b =
              eval_stack.values[static_cast<std::size_t>(eval_stack.top + 1)];
          eval_stack.values[static_cast<std::size_t>(eval_stack.top + 1)] =
              ApplyBuiltinFunc(op, a, b);
          ++eval_stack.top;
        }
        break;

      case kOpFuncCond:
      case kOpFuncClamp:

        if (eval_stack.top <= static_cast<std::int32_t>(eval_stack.values.size()) - 3) {
          const float a = eval_stack.values[static_cast<std::size_t>(eval_stack.top)];
          const float b =
              eval_stack.values[static_cast<std::size_t>(eval_stack.top + 1)];
          const float c =
              eval_stack.values[static_cast<std::size_t>(eval_stack.top + 2)];
          eval_stack.values[static_cast<std::size_t>(eval_stack.top + 2)] =
              ApplyBuiltinFunc(op, a, b, c);
          eval_stack.top += 2;
        }
        break;

      default:
        SpellTextFormatter::EvaluateSpellTooltipVariable(
            parser_state_ptr,
            op,
            &eval_stack,
            active_spell_data,
            effect_index,
            stack_count,
            combo_points,
            is_pet,
            is_periodic);
        if (has_dereferenced_record) {
          active_spell_data = spell_data;
          has_dereferenced_record = false;
        }
        break;
    }
  }

  if (state->status != 1) {
    state->status = 2;
  }
  if (state->status != 2) {
    return 0.0;
  }
  return eval_stack.top < static_cast<std::int32_t>(eval_stack.values.size())
      ? static_cast<double>(eval_stack.values[static_cast<std::size_t>(eval_stack.top)])
      : 0.0;
}

void* SpellTextFormatter::EvaluateSpellTooltipVariable(
    void* parser_state,
    std::uint32_t var_id,
    void* eval_stack,
    const void* spell_data,
    std::int32_t effect_index,
    std::int32_t stack_count,
    std::int32_t combo_points,
    std::int32_t is_pet,
    std::int32_t is_periodic) {
  auto* parsed_state = static_cast<std::uint32_t*>(parser_state);
  auto* resolved_stack = static_cast<ExpressionEvalStack*>(eval_stack);
  if (resolved_stack == nullptr) {
    return eval_stack;
  }

  if (var_id < 0x16 || var_id > 0xA1) {
    if (parsed_state != nullptr) {
      *parsed_state = 1;
    }
    PushTooltipValue(*resolved_stack, 0.0f);
    return eval_stack;
  }

  const auto* const objects = ResolveSpellTextObjectManager();
  const auto* active_player =
      objects != nullptr ? objects->GetActivePlayer() : nullptr;
  if (active_player == nullptr) {
    PushTooltipValue(*resolved_stack, 0.0f);
    return eval_stack;
  }

  TooltipVariableContext context;
  context.spell = static_cast<const openwow::data::dbc::SpellEntry*>(spell_data);
  context.level_override = effect_index;
  context.stack_multiplier = stack_count;
  context.use_target = combo_points != 0;
  context.use_pet = is_pet != 0;
  context.suppress_spell_modifiers = is_periodic != 0;
  PushTooltipValue(
      *resolved_stack,
      ResolveTooltipVariableValue(var_id, context, *active_player));
  return eval_stack;
}

bool SpellTextFormatter::ExpandObjectTextVariables(
    const char* format_text,
    char* output,
    std::uint32_t output_size,
    std::uint64_t guid,
    char* name_buf,
    std::int32_t name_size,
    const WorldStateValueResolver& resolve_world_state,
    std::int32_t current_time_seconds,
    std::int32_t achievement_id) {
  if (!format_text || !output || output_size == 0) return false;

  (void)name_size;
  output[0] = '\0';
  const auto context = ResolveObjectTextContext(guid, name_buf);
  const char* p = format_text;
  bool all_resolved = true;

  while (*p) {
    const char* dollar = std::strchr(p, '$');
    if (!dollar) {
      StrCat(output, output_size, p);
      break;
    }

    if (dollar > p) {
      AppendChars(output, output_size, p, static_cast<std::size_t>(dollar - p));
    }

    p = dollar + 1;

    if (!*p) {
      AppendChar(output, output_size, '$');
      all_resolved = false;
      break;
    }

    const char* token_body = p;
    const char* token_ptr = token_body;
    while (*token_ptr >= '0' && *token_ptr <= '9') {
      ++token_ptr;
    }
    const auto variable_id =
        static_cast<std::int32_t>(std::strtol(token_body, nullptr, 10));

    switch (*token_ptr) {
      case 'A':
      case 'a':
        if (TryAppendAchievementLink(output, output_size, guid,
                                     resolve_world_state,
                                     current_time_seconds,
                                     achievement_id)) {
          p = token_ptr + 1;
          break;
        }
        AppendChar(output, output_size, '$');
        all_resolved = false;
        p = token_body;
        break;

      case 'C':
      case 'c':
        if (context.unit != nullptr && !context.is_player_type) {
          if (!context.live_name.empty()) {
            StrCat(output, output_size, context.live_name.c_str());
            p = token_ptr + 1;
            break;
          }
        } else if (!context.class_name.empty()) {
          const auto class_text =
              *token_ptr == 'c' ? LowercaseAscii(context.class_name)
                                : context.class_name;
          StrCat(output, output_size, class_text.c_str());
          p = token_ptr + 1;
          break;
        }
        AppendChar(output, output_size, '$');
        all_resolved = false;
        p = token_body;
        break;

      case 'D':
      case 'd': {
        const auto text = FormatQuestDurationMinutes(
            ResolveWorldStateValue(resolve_world_state, -variable_id));
        StrCat(output, output_size, text.c_str());
        p = token_ptr + 1;
        break;
      }

      case 'E':
      case 'e': {
        char number_buffer[16] = {};
        std::snprintf(number_buffer, sizeof(number_buffer), "%d",
                      ResolveWorldStateValue(resolve_world_state, -variable_id));
        StrCat(output, output_size, number_buffer);
        p = token_ptr + 1;
        break;
      }

      case 'n':
      case 'N':
        if (name_buf != nullptr) {
          StrCat(output, output_size, name_buf);
          p = token_ptr + 1;
          break;
        }
        if (!context.resolved_name.empty()) {
          StrCat(output, output_size, context.resolved_name.c_str());
          p = token_ptr + 1;
          break;
        }
        AppendChar(output, output_size, '$');
        all_resolved = false;
        p = token_body;
        break;

      case 'b':
      case 'B':
        AppendChar(output, output_size, '\n');
        p = token_ptr + 1;
        break;

      case 'R':
      case 'r':
        if (context.unit != nullptr && !context.is_player_type) {
          if (!context.live_name.empty()) {
            StrCat(output, output_size, context.live_name.c_str());
            p = token_ptr + 1;
            break;
          }
        } else if (!context.race_name.empty()) {
          const auto race_text =
              *token_ptr == 'r' ? LowercaseAscii(context.race_name)
                                : context.race_name;
          StrCat(output, output_size, race_text.c_str());
          p = token_ptr + 1;
          break;
        }
        AppendChar(output, output_size, '$');
        all_resolved = false;
        p = token_body;
        break;

      case 'T':
      case 't': {
        if (!context.has_object_or_cache) {
          AppendChar(output, output_size, '$');
          all_resolved = false;
          p = token_body;
          break;
        }

        const char* cursor = token_ptr + 1;
        while (*cursor == ' ') {
          ++cursor;
        }
        if (*cursor == '\0') {
          p = cursor;
          break;
        }

        const char* colon = std::strchr(cursor, ':');
        if (colon == nullptr) {
          p = cursor;
          break;
        }

        const char* semicolon = std::strchr(colon, ';');
        if (semicolon == nullptr) {
          p = cursor;
          break;
        }

        if (TryAppendPvPRankTitle(context, *token_ptr == 't',
                                  output, output_size)) {
          p = semicolon + 1;
          break;
        }

        if (context.gender_selector != 0) {
          AppendTrimmedBranch(output, output_size, colon + 1, semicolon);
        } else {
          AppendTrimmedBranch(output, output_size, cursor, colon);
        }
        p = semicolon + 1;
        break;
      }

      case 'w':
      case 'W': {
        char number_buffer[16] = {};
        std::snprintf(number_buffer, sizeof(number_buffer), "%d",
                      ResolveWorldStateValue(resolve_world_state, variable_id));
        StrCat(output, output_size, number_buffer);
        p = token_ptr + 1;
        break;
      }

      case 'k':
      case 'K':
        QuestTextParser::AppendWorldStateCountdown(
            output,
            output_size,
            ResolveWorldStateValue(resolve_world_state, variable_id),
            current_time_seconds);
        p = token_ptr + 1;
        break;

      case 'g':
      case 'G': {
        if (!context.has_object_or_cache) {
          AppendChar(output, output_size, '$');
          all_resolved = false;
          p = token_body;
          break;
        }

        ConditionalTextTagSelection selection;
        ConditionalTextTagContext tag_context;
        tag_context.selector = context.gender_selector;
        tag_context.class_selector = context.class_selector;
        tag_context.race_selector = context.race_selector;
        if (!TrySelectConditionalTextTag(std::string_view(token_ptr + 1),
                                         tag_context,
                                         &selection)) {
          AppendChar(output, output_size, '$');
          all_resolved = false;
          p = token_body;
          break;
        }

        AppendChars(output, output_size, selection.text.data(),
                    selection.text.size());
        p = token_ptr + 1 + selection.consumed;
        break;
      }

      default:
        AppendChar(output, output_size, '$');
        all_resolved = false;
        p = token_body;
        break;
    }
  }

  return all_resolved;
}

bool SpellTextFormatter::ExpandTextVariables(
    const void* spell_data,
    char* output,
    std::uint32_t output_size,
    std::int32_t effect_index,
    std::int32_t combo_points,
    std::int32_t stack_count,
    std::int32_t is_periodic,
    std::int32_t is_pet,
    const char** format_string_ptr,
    bool expand_inner) {
  if (!output || output_size == 0 || !format_string_ptr || !*format_string_ptr) {
    return false;
  }

  output[0] = '\0';
  SpellTextRuntimeState runtime_state;
  const SpellTextExpansionContext context{
      spell_data,
      effect_index,
      combo_points,
      stack_count,
      is_periodic,
      is_pet,
      &runtime_state};
  const char* cursor = *format_string_ptr;
  const bool resolved =
      ExpandSpellTextSegment(context, output, output_size, cursor, expand_inner);
  *format_string_ptr = cursor;
  return resolved;
}

bool SpellTextFormatter::ExpandTextVariablesWithNamedTagDefinitions(
    const void* spell_data,
    char* output,
    std::uint32_t output_size,
    std::int32_t effect_index,
    std::int32_t combo_points,
    std::int32_t stack_count,
    std::int32_t is_periodic,
    std::int32_t is_pet,
    const char* named_tag_definitions,
    const char** format_string_ptr,
    bool expand_inner) {
  SpellTextRuntimeState runtime_state;
  const SpellTextExpansionContext context{
      spell_data,
      effect_index,
      combo_points,
      stack_count,
      is_periodic,
      is_pet,
      &runtime_state};
  ScopedTooltipNamedTagDefinitions scoped_named_tags(
      BuildTooltipNamedTagDefinitions(context, named_tag_definitions));
  return SpellTextFormatter::ExpandTextVariables(
      spell_data,
      output,
      output_size,
      effect_index,
      combo_points,
      stack_count,
      is_periodic,
      is_pet,
      format_string_ptr,
      expand_inner);
}

std::string ExpandSpellDescription(
    std::uint32_t spell_id,
    std::int32_t effect_index,
    std::int32_t combo_points) {
  const auto* dbc = ResolveSpellTextDbc();
  const auto* spell = ResolveExpressionSpellRecord(spell_id);
  if (dbc == nullptr || spell == nullptr || spell->description.empty()) {
    return {};
  }

  char output[4096] = {};
  const std::string description(spell->description);
  const char* format = description.c_str();
  std::string named_tag_definitions;
  if (spell->spell_description_variable_id != 0) {
    if (const auto* variables = dbc->spell_description_variables().LookupEntry(
            spell->spell_description_variable_id)) {
      named_tag_definitions.assign(variables->variables);
    }
  }

  SpellTextFormatter::ExpandTextVariablesWithNamedTagDefinitions(
      spell,
      output,
      sizeof(output),
      effect_index,
      combo_points,
      1,
      0,
      0,
      named_tag_definitions.empty() ? nullptr : named_tag_definitions.c_str(),
      &format,
      true);
  return std::string(output);
}

std::string ResolveSpellDescriptionForDisplay(
    const std::uint32_t spell_id,
    const std::string_view authored_description,
    const std::int32_t effect_index,
    const std::int32_t combo_points) {
  if (authored_description.empty()) {
    return {};
  }

  if (ResolveSpellTextDbc() == nullptr ||
      ResolveExpressionSpellRecord(spell_id) == nullptr) {
    return std::string(authored_description);
  }
  return ExpandSpellDescription(spell_id, effect_index, combo_points);
}

std::string ExpandQuestText(
    const std::string& format_text,
    std::uint64_t quest_giver_guid) {
  char output[4096] = {};
  SpellTextFormatter::ExpandObjectTextVariables(
      format_text.c_str(),
      output,
      sizeof(output),
      quest_giver_guid,
      nullptr,
      0);
  return std::string(output);
}

}
