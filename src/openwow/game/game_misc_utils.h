
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "openwow/game/inventory/loot/loot_interaction.h"

namespace openwow::game {

class WorldSession;
class ObjectManager;
class BindingProfiles;
struct ItemTemplate;

std::uint32_t ReadUInt32FromPacket(void* data_store);

struct StablePetSlot {
  std::uint32_t pet_number  = 0;
  std::uint32_t level       = 0;
  std::uint32_t creature_id = 0;
  std::string   name;
  std::uint8_t  flags       = 0;
  bool          active      = false;
};

const StablePetSlot* StablePet_GetSlotEntry(std::uint32_t slot_index);

double GetWidescreenAspectRatio();

bool IsAutoLootEnabled(const BindingProfiles* bindings);
void PrepareAutoLootInteraction(WorldSession& session, bool enabled);

[[nodiscard]] bool ApplyPendingAutoLoot(WorldSession& session,
                                        bool release_when_emptied = true);
[[nodiscard]] bool LootItemRequiresBindConfirm(const ItemTemplate& item_template,
                                               LootSlotType slot_type);
[[nodiscard]] bool CanPromptLootItemBindConfirm(WorldSession& session,
                                                const ItemTemplate& item_template,
                                                std::uint32_t loot_count,
                                                bool show_errors);
void ClearActivePlayerLootInteractionState(WorldSession& session);
void CleanupClosingLootSourceState(WorldSession& session, const LootWindow& loot_window);

struct CloseLootWindowOptions {
  bool send_release = true;
  bool skip_item_check = false;
  bool show_interrupted = false;
  bool clear_dead_target = true;
};

void CloseActiveLootWindow(WorldSession& session,
                           const CloseLootWindowOptions& options = {});

const char* GetMirrorTimerName(int timer_index);

int GetMirrorTimerIndex(const char* timer_name);

struct MirrorTimerScriptState {
  std::int32_t  type = 3;
  std::uint32_t current_value = 0;
  std::uint32_t max_value = 0;
  std::int32_t  scale = 0;
  std::int32_t  paused = 0;
  std::uint32_t spell_id = 0;
  std::uint32_t base_tick_count = 0;
};

[[nodiscard]] std::optional<MirrorTimerScriptState> GetMirrorTimerState(
    int timer_index);
[[nodiscard]] bool HasNegativeMirrorTimerScale(int timer_index);
[[nodiscard]] std::optional<std::int32_t> GetMirrorTimerProgressValue(
    int timer_index);
[[nodiscard]] std::string ResolveMirrorTimerLabel(int timer_index,
                                                  std::uint32_t spell_id);
void StartMirrorTimer(int timer_index, std::uint32_t current_value,
                      std::uint32_t max_value, std::int32_t scale,
                      bool paused, std::uint32_t spell_id);
void StopMirrorTimer(int timer_index);

using MirrorTimerTickCountProvider = std::uint32_t (*)();
void SetMirrorTimerTickCountProvider(MirrorTimerTickCountProvider provider);
void UseDefaultMirrorTimerTickCountProvider();

void ResetMirrorTimersForEnterWorldInit();

void ResetAllMirrorTimers();

struct DeathReleasePosition {
  int   map_id = -1;
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

void SetDeathReleasePosition(const ObjectManager& objects,
                             const DeathReleasePosition& position);
void ResetDeathReleasePosition();
DeathReleasePosition GetDeathReleasePosition();

void LoadArchivedRuntimeStateFromCVars(ObjectManager& objects);

void LoadingScreen_PreInitFont();

int GetHyperlinkCompareSlot();

}
