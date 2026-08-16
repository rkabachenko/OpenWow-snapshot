#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace openwow::game {
class CGGameObject_C;
struct GameObjectTemplateInfo;
class ObjectManager;
class WorldStateManager;
}

namespace openwow::ui::game {

struct CapturePointUIEntry {
  std::uint64_t object_guid{0};
  std::int32_t capture_radius_sq{0};
  std::int32_t point_id{0};
};

class CapturePointUIManagerState {
public:
  using EntryList = std::vector<CapturePointUIEntry>;
  using ConstEntryIterator = EntryList::const_iterator;
  using TickCountProvider = std::function<std::uint32_t()>;

  void AddCapturePoint(const openwow::game::CGGameObject_C &object, std::int32_t point_id,
                       std::int32_t capture_radius);
  [[nodiscard]] ConstEntryIterator Erase(ConstEntryIterator entry);
  void Clear();
  void SetTickCountProvider(TickCountProvider provider);
  void UseDefaultTickCountProvider();
  [[nodiscard]] std::uint32_t GetCurrentTickCount() const;
  void ArmCaptureWarningCooldown(std::uint32_t current_tick, std::uint32_t cooldown_ms);
  [[nodiscard]] std::uint32_t GetNextCaptureWarningTick() const {
    return next_capture_warning_tick_;
  }
  [[nodiscard]] bool ContainsObjectGuid(std::uint64_t object_guid) const;
  [[nodiscard]] bool ContainsPointId(std::int32_t point_id) const;

  [[nodiscard]] const EntryList &Entries() const {
    return entries_;
  }

private:
  EntryList entries_;
  std::uint32_t next_capture_warning_tick_{0};
  TickCountProvider tick_count_provider_;
};

int CapturePointUIManagerNode_Insert(CapturePointUIManagerState &manager,
                                     const openwow::game::CGGameObject_C *object, int point_id,
                                     int capture_radius);

CapturePointUIManagerState::ConstEntryIterator
CapturePointUIManagerNode_Erase(CapturePointUIManagerState &manager,
                                CapturePointUIManagerState::ConstEntryIterator entry);

void RegisterCapturePointGameObject(CapturePointUIManagerState &manager,
                                    const openwow::game::CGGameObject_C &object,
                                    const openwow::game::GameObjectTemplateInfo &template_info);

bool UnregisterCapturePointGameObject(CapturePointUIManagerState &manager,
                                      std::uint64_t object_guid,
                                      openwow::game::WorldStateManager *world_states);

CapturePointUIManagerState &GetCapturePointUIManagerState();
void ResetCapturePointUIManagerState();

void GameUI_CapturePointProximityCheck(CapturePointUIManagerState &manager,
                                       const openwow::game::ObjectManager &object_manager);
void GameUI_CapturePointProximityCheck(void *manager_node);

}
