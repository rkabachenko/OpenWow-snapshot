#pragma once

#include "openwow/data/formats/dbc/dbc_entries_world.h"
#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/render/world/environment/fog_controller.h"

#include <cstdint>
#include <optional>
#include <span>

namespace openwow::data::dbc {
class DbcLoader;
}
namespace openwow::render {
class PostProcess;
struct ScreenEffectState;
struct WorldViewEffectState;
}

namespace openwow::game {

class BattlefieldInfo;
class CGPlayer_C;

inline constexpr std::uint32_t kSpellAuraScreenEffect = 260u;
inline constexpr std::uint32_t kGhostScreenEffectId = 1u;
inline constexpr std::uint32_t kAuraVisionScreenEffectId = 81u;
inline constexpr std::uint32_t kSuppressedGhostBattlefieldInstanceType = 4u;

inline constexpr std::uint32_t kPlayerFieldBytes2AuraVisionScreenEffectMask =
    0x40000000u;

[[nodiscard]] std::optional<std::uint32_t> FindAuraDrivenScreenEffectId(
    std::span<const openwow::data::dbc::SpellEntry*> aura_spells);

[[nodiscard]] std::uint32_t SelectScreenEffectId(
    std::span<const openwow::data::dbc::SpellEntry*> aura_spells,
    bool is_ghost, bool has_aura_vision_screen_effect,
    std::optional<std::uint32_t> battlefield_instance_type);

[[nodiscard]] std::optional<std::uint32_t> ResolveActiveBattlefieldInstanceType(
    const BattlefieldInfo& battlefield_info,
    const openwow::data::dbc::DbcLoader& dbc);

[[nodiscard]] render::ScreenEffectState ResolveScreenEffectState(
    std::uint32_t screen_effect_id,
    const openwow::data::dbc::ScreenEffectEntry* screen_effect);

[[nodiscard]] render::ScreenEffectState ResolveActivePlayerScreenEffect(
    const CGPlayer_C& active_player, const openwow::data::dbc::DbcLoader& dbc,
    std::optional<std::uint32_t> battlefield_instance_type);

[[nodiscard]] std::optional<render::FogBandOverride> ResolveScreenEffectFogOverride(
    const render::ScreenEffectState& screen_effect_state,
    bool has_screen_capture);

void ApplyScreenEffectFogOverrideToDayNight(
    const std::optional<render::FogBandOverride>& fog_override);

[[nodiscard]] bool UsesDefaultWorldViewPipeline(
    const render::ScreenEffectState& screen_effect_state);

[[nodiscard]] render::WorldViewEffectState ResolveWorldViewEffectState(
    float scene_visibility,
    const CGPlayer_C* active_player,
    bool underwater);

void ApplyScreenEffectToPostProcess(
    const render::ScreenEffectState& screen_effect_state,
    render::PostProcess& post_process);

}
