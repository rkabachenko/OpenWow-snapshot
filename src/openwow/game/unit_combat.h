#pragma once

namespace openwow::audio { class SoundRuntime; }

#include <cstdint>
#include <optional>

#include "openwow/game/object_guid.h"

namespace openwow::game::unit_combat {

enum CombatTextMissDisplayId : std::uint32_t {
  kCombatTextNone      = 0,
  kCombatTextMiss      = 1,
  kCombatTextResist    = 2,
  kCombatTextDodge     = 3,
  kCombatTextParry     = 4,
  kCombatTextBlock     = 5,
  kCombatTextEvade     = 6,
  kCombatTextImmune    = 7,
  kCombatTextImmuneAlt = 8,
  kCombatTextDeflect   = 9,
  kCombatTextAbsorb    = 10,
};

enum class AttackResultType : std::uint32_t {
  kMiss      = 0,
  kWound     = 1,
  kDodge     = 2,
  kParry     = 3,
  kInterrupt = 4,
  kBlock     = 5,
  kEvade     = 6,
  kImmune    = 7,
  kDeflect   = 8,
};

namespace AttackHitFlags {
inline constexpr std::uint32_t kOffhand         = 0x00000004;
inline constexpr std::uint32_t kFullAbsorb      = 0x00000020;
inline constexpr std::uint32_t kFullResist      = 0x00000080;
inline constexpr std::uint32_t kCriticalHit     = 0x00000200;
inline constexpr std::uint32_t kBlock           = 0x00002000;
inline constexpr std::uint32_t kGlancing        = 0x00004000;
inline constexpr std::uint32_t kCrushing        = 0x00020000;
inline constexpr std::uint32_t kEnvironmental   = 0x01000000;
}

enum CombatEmoteAnim : std::uint32_t {
  kAnimShieldBlock = 24,
  kAnimDodge = 30,
};

enum CombatAnimFourCC : std::uint32_t {
  kFourCC_AH0 = 0x30484124,
  kFourCC_AH1 = 0x31484124,
  kFourCC_AH2 = 0x32484124,
  kFourCC_AH3 = 0x33484124,
  kFourCC_DTH = 0x48544424,
  kFourCC_CAH = 0x48414324,
  kFourCC_CPP = 0x50504324,
  kFourCC_BWP = 0x50574224,
  kFourCC_CSS = 0x53534324,
};

enum CombatLogOpcode : std::uint32_t {
  kSMSG_ATTACKSTART         = 323,
  kSMSG_ATTACKSTOP          = 324,
  kSMSG_ATTACKSWING_NOTINRANGE = 325,
  kSMSG_ATTACKSWING_BADFACING  = 326,
  kSMSG_ATTACKSWING_DEADTARGET = 328,
  kSMSG_ATTACKSWING_CANT_ATTACK = 329,
  kSMSG_ATTACKERSTATEUPDATE = 330,
  kSMSG_COMBAT_VISUAL       = 508,
  kSMSG_ATTACKSTOP_ALT      = 609,
  kSMSG_FORCE_CLEAR_TARGET  = 959,
};

void* CGUnit_C_GetWeaponVisualForSlot(void* unit, int is_offhand,
                                      bool ensure_model_loaded);

void UnitCombat_C_HandleAttackResultAnim(void* unit, int attackResult,
                                         const void* combatData);

void CombatText_ProcessAttackResult(void* attacker, int edx_unused,
                                    int* combatData);

void UnitCombat_ProcessTargetHit(void* unit, int* combatData);

void UnitCombat_CreateDeathCameraShake(void* unit);

struct AnimationPlaybackState {
  std::int32_t anim_id{-1};
  std::uint32_t sub_anim_count{0};
  std::uint32_t current_time{0};
  float playback_speed{1.0f};
  std::uint32_t start_time{0};
  std::uint32_t end_time{0};
  std::uint32_t flags{0};
  std::uint32_t finished{0};
};

inline constexpr std::uint32_t kAnimWeaponTrail = 0xA0;

inline constexpr std::int32_t kCombatAnimStateParry = 4;

[[nodiscard]] std::optional<float> ComputeWeaponTrailAnimSpeed(
    const AnimationPlaybackState& base_state,
    std::uint32_t weapon_anim_duration_ms);

void UnitCombat_ProcessPendingCombatResult(void* unit, int edx_unused);

void UnitCombat_HandleAnimEvent(void* unit, std::uint32_t fourCC,
                                int param, const float* position, int flags);

void UnitCombat_ClearAndFace(void* unit, std::uint64_t attackerGUID,
                             int attackerPresent);

int CombatLog_HandleAttackOpcodes(const char* edx_unused, int param2,
                                  int opcode, int param4,
                                  void* packetData);

int CombatLog_Initialize();

int CGUnit_CheckCantEmoteFlag(const void* unit);

struct AttackResultContext {
  ObjectGuid active_player_guid;
  ObjectGuid attacker_guid;
  ObjectGuid victim_guid;
  std::uint32_t hit_flags{0};
  std::int32_t damage{0};
  AttackResultType result_type{AttackResultType::kMiss};
  std::uint32_t extra_attacks{0};
};

struct AttackResultDisplayAction {
  bool should_process{false};

  bool show_miss_text{false};
  CombatTextMissDisplayId miss_display_id{kCombatTextNone};

  bool show_damage_number{false};
  bool is_crit{false};

  bool play_miss_sound{false};
  bool play_weapon_impact_sound{false};
};

[[nodiscard]] AttackResultDisplayAction
DetermineAttackResultDisplay(const AttackResultContext& ctx);

}
