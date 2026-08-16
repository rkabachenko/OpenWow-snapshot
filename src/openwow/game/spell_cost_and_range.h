#pragma once

#include "openwow/game/object_guid.h"
#include "openwow/game/spell_runtime_values.h"

#include <cstdint>
#include <optional>

namespace openwow::data::dbc {
class DbcLoader;
struct SpellEntry;
struct SpellVisualEntry;
}

namespace openwow::game {

class CGItem_C;
class CGObject_C;
class CGPlayer_C;
class CGUnit_C;
class ItemDefinitions;
class ObjectManager;
class PlayerInventoryReplica;
class WorldSession;

void SpellAuraList_AdjustDuration(std::uintptr_t aura_list,
                                  std::uint32_t spell_id,
                                  std::int32_t delta_ms);
void SpellAuraList_RecycleAll(std::uintptr_t aura_list);
bool GetTargetRangeWindow(const WorldSession& session,
                          std::uint32_t spell_id, const CGUnit_C& caster,
                          const CGObject_C* pending_target, float* out_min,
                          float* out_max);

std::uint32_t GetCastFailureMessageId(std::uint32_t error_code,
                                       std::uintptr_t spell_rec,
                                       std::int32_t extra_param);

void PlaySpellSchoolFizzleSound(const data::dbc::DbcLoader& dbc,
                                std::uint32_t spell_id,
                                const CGObject_C* unit);

void HandleCastFailure(WorldSession& session, std::uintptr_t spell_entry,
                        std::uintptr_t spell_rec,
                        std::uint32_t error_code, std::int32_t extra1,
                        std::int32_t extra2, bool is_auto_repeat);

int PetSpellCast(std::uint32_t action_slot, std::uintptr_t action_data);

int IsItemUseSpellRecentlyCast(const ItemDefinitions& item_definitions,
                               std::uint32_t item_entry_id);

int HasActiveSpellCooldown(std::uintptr_t spell_rec);

bool CheckEquippedItemAndAmmo(const WorldSession& session,
                              const CGPlayer_C& player,
                               const data::dbc::SpellEntry& spell,
                               bool check_ammo);

}
