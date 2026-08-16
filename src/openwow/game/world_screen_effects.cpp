#include "openwow/game/world_screen_effects.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/battlefield_info.h"
#include "openwow/game/inebriation.h"
#include "openwow/game/update_fields.h"
#include "openwow/world/environment/day_night.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/render/effects/postprocess/post_process.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace openwow::game {

namespace {

constexpr std::uint32_t kNetherFogFallbackBgra = 0xFF4C4C63u;
constexpr float kNetherFogStartFactor = 0.69999999f;
constexpr float kNetherFogEndDistance = 150.0f;

[[nodiscard]] std::uint8_t EncodeWorldViewEffectByte(const float value) {
  if (!(value > 0.0f)) {
    return 0u;
  }
  const float scaled = std::min(value * 255.0f, 255.999f);
  return static_cast<std::uint8_t>(scaled);
}

[[nodiscard]] render::FogColor NormalizeFogColorFromBgra(
    const std::uint32_t bgra) {
  return {
      static_cast<float>((bgra >> 0) & 0xFFu) / 255.0f,
      static_cast<float>((bgra >> 8) & 0xFFu) / 255.0f,
      static_cast<float>((bgra >> 16) & 0xFFu) / 255.0f,
      static_cast<float>((bgra >> 24) & 0xFFu) / 255.0f,
  };
}

render::ScreenEffectKind ToScreenEffectKind(std::uint32_t effect) {
  switch (effect) {
    case 0:
      return render::ScreenEffectKind::kDefault;
    case 1:
      return render::ScreenEffectKind::kDeath;
    case 2:
      return render::ScreenEffectKind::kNether;
    case 3:
      return render::ScreenEffectKind::kSpecial;
    default:
      return render::ScreenEffectKind::kUnknown;
  }
}

std::optional<std::uint8_t> ResolveLightParamSlotOverride(
    std::uint32_t raw_value) {
  if (raw_value > 7u) {
    return std::nullopt;
  }

  return static_cast<std::uint8_t>(raw_value);
}

}

std::optional<std::uint32_t> FindAuraDrivenScreenEffectId(
    std::span<const openwow::data::dbc::SpellEntry*> aura_spells) {
  for (std::size_t aura_index = aura_spells.size(); aura_index > 0; --aura_index) {
    const auto* spell = aura_spells[aura_index - 1];
    if (!spell) {
      continue;
    }

    for (std::size_t effect_index = 0;
         effect_index < spell->effect_apply_aura.size(); ++effect_index) {

      if (spell->effect_apply_aura[effect_index] != kSpellAuraScreenEffect) {
        continue;
      }

      const std::int32_t screen_effect_id =
          spell->effect_misc_value[effect_index];
      if (screen_effect_id > 0) {
        return static_cast<std::uint32_t>(screen_effect_id);
      }
      return 0u;
    }
  }

  return std::nullopt;
}

std::uint32_t SelectScreenEffectId(
    std::span<const openwow::data::dbc::SpellEntry*> aura_spells,
    bool is_ghost, bool has_aura_vision_screen_effect,
    std::optional<std::uint32_t> battlefield_instance_type) {
  if (const auto aura_screen_effect = FindAuraDrivenScreenEffectId(aura_spells)) {
    return *aura_screen_effect;
  }

  if (is_ghost
      && battlefield_instance_type != kSuppressedGhostBattlefieldInstanceType) {
    return kGhostScreenEffectId;
  }

  if (has_aura_vision_screen_effect) {
    return kAuraVisionScreenEffectId;
  }

  return 0;
}

std::optional<std::uint32_t> ResolveActiveBattlefieldInstanceType(
    const BattlefieldInfo& battlefield_info,
    const openwow::data::dbc::DbcLoader& dbc) {
  const std::int32_t active_slot = battlefield_info.GetActiveSlotIndex();
  if (active_slot < 0) {
    return std::nullopt;
  }

  const auto& queue_slot =
      battlefield_info.GetQueueSlot(static_cast<std::size_t>(active_slot));
  if (queue_slot.status != 3 || queue_slot.bg_type_id == 0) {
    return std::nullopt;
  }

  const auto* battlemaster =
      dbc.battlemaster_list().LookupEntry(queue_slot.bg_type_id);
  if (!battlemaster) {
    return std::nullopt;
  }

  return battlemaster->instance_type;
}

render::ScreenEffectState ResolveScreenEffectState(
    std::uint32_t screen_effect_id,
    const openwow::data::dbc::ScreenEffectEntry* screen_effect) {
  render::ScreenEffectState state{};
  if (!screen_effect) {
    return state;
  }

  state.screen_effect_id = screen_effect_id;
  state.kind = ToScreenEffectKind(screen_effect->effect);
  for (std::size_t i = 0; i < state.param.size(); ++i) {
    state.param[i] = static_cast<float>(screen_effect->param[i]);
  }
  state.light_param_slot_override =
      ResolveLightParamSlotOverride(screen_effect->light_params_slot);
  state.sound_ambience_id = screen_effect->sound_ambience_id;
  state.zone_music_id = screen_effect->zone_music_id;
  return state;
}

render::ScreenEffectState ResolveActivePlayerScreenEffect(
    const CGPlayer_C& active_player, const openwow::data::dbc::DbcLoader& dbc,
    std::optional<std::uint32_t> battlefield_instance_type) {
  std::vector<const openwow::data::dbc::SpellEntry*> aura_spells;
  aura_spells.reserve(active_player.Auras().Count());
  for (std::size_t aura_index = 0; aura_index < active_player.Auras().Count();
       ++aura_index) {
    const auto* aura = active_player.Auras().At(aura_index);
    if (!aura || aura->spell_id == 0) {
      aura_spells.push_back(nullptr);
      continue;
    }
    aura_spells.push_back(dbc.spell().LookupEntry(aura->spell_id));
  }

  const std::uint32_t screen_effect_id = SelectScreenEffectId(
      aura_spells, active_player.IsGhost(),
      (active_player.GetUInt32(PLAYER_FIELD_BYTES2) &
       kPlayerFieldBytes2AuraVisionScreenEffectMask) != 0,
      battlefield_instance_type);

  return ResolveScreenEffectState(
      screen_effect_id,
      dbc.screen_effect().LookupEntry(screen_effect_id));
}

std::optional<render::FogBandOverride> ResolveScreenEffectFogOverride(
    const render::ScreenEffectState& screen_effect_state,
    const bool has_screen_capture) {
  if (screen_effect_state.kind != render::ScreenEffectKind::kNether) {
    return std::nullopt;
  }

  render::FogBandOverride fog_override{};
  fog_override.band.end_distance = kNetherFogEndDistance;
  fog_override.band.start_factor = kNetherFogStartFactor;
  fog_override.band.color = NormalizeFogColorFromBgra(
      has_screen_capture ? 0xFFFFFFFFu : kNetherFogFallbackBgra);

  fog_override.sky_dome_enabled = false;
  return fog_override;
}

void ApplyScreenEffectFogOverrideToDayNight(
    const std::optional<render::FogBandOverride>& fog_override) {
  if (!fog_override.has_value()) {
    DayNight_ClearScreenEffectFogOverride();
    return;
  }

  DayNight_SetScreenEffectFogOverride(
      fog_override->band.start_factor,
      fog_override->band.end_distance,
      render::PackFogColorToArgb(fog_override->band.color),
      fog_override->sky_dome_enabled ? 1u : 0u);
}

bool UsesDefaultWorldViewPipeline(
    const render::ScreenEffectState& screen_effect_state) {
  return screen_effect_state.kind == render::ScreenEffectKind::kDefault;
}

render::WorldViewEffectState ResolveWorldViewEffectState(
    const float scene_visibility,
    const CGPlayer_C* const active_player,
    const bool underwater) {
  const std::uint8_t drunk_state =
      active_player != nullptr ? active_player->GetDrunkState() : 0u;
  const std::uint32_t fake_inebriation =
      active_player != nullptr ? active_player->GetFakeInebriation() : 0u;

  render::WorldViewEffectState state{};
  state.underwater_enabled = underwater;
  state.glow_coefficient_byte =
      EncodeWorldViewEffectByte(scene_visibility);
  state.normalized_inebriation =
      ComputeNormalizedInebriation(drunk_state, fake_inebriation);
  state.effect_intensity_byte = std::max(
      underwater ? std::uint8_t{84u} : std::uint8_t{0u},
      EncodeWorldViewEffectByte(state.normalized_inebriation));
  return state;
}

void ApplyScreenEffectToPostProcess(
    const render::ScreenEffectState& screen_effect_state,
    render::PostProcess& post_process) {
  if (screen_effect_state.light_param_slot_override.has_value()) {
    DayNight_SetScreenEffectLightParamsId(
        *screen_effect_state.light_param_slot_override);
  } else {
    DayNight_ClearScreenEffectLightParamsId();
  }
  post_process.SetScreenEffectState(screen_effect_state);
  post_process.SetDeathEffect(
      screen_effect_state.kind == render::ScreenEffectKind::kDeath, 1.0f);
}

}
