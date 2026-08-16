#pragma once

#include "openwow/game/object_guid.h"
#include "openwow/game/packet_reader.h"

#include <cstdint>

namespace openwow::game {

class UnitLootComponent {
public:
  UnitLootComponent() = default;
  UnitLootComponent(const UnitLootComponent &) = delete;
  UnitLootComponent &operator=(const UnitLootComponent &) = delete;
  UnitLootComponent(UnitLootComponent &&) noexcept = default;
  UnitLootComponent &operator=(UnitLootComponent &&) noexcept = default;
  ~UnitLootComponent() = default;

  void SetCorpseReadyTick(std::uint32_t ready_tick_ms) noexcept {
    corpse_ready_tick_ = ready_tick_ms;
  }
  [[nodiscard]] std::uint32_t CorpseReadyTick() const noexcept {
    return corpse_ready_tick_;
  }
  void ClearCorpseReadyTick() noexcept { corpse_ready_tick_ = 0; }
  void MarkCorpseReadyNow(std::uint32_t receive_tick_ms) noexcept {
    if (corpse_ready_tick_ == 0) {
      corpse_ready_tick_ = receive_tick_ms;
    }
  }
  [[nodiscard]] bool IsCorpseReadyAt(std::uint32_t current_tick_ms) const noexcept {
    if (corpse_ready_tick_ == 0) {
      return true;
    }
    return static_cast<std::int32_t>(current_tick_ms - corpse_ready_tick_) >= 0;
  }

  void StoreLootListGuids(PacketReader &reader) {
    ObjectGuid master;
    ObjectGuid group;
    if (!reader.ReadPackedGuid(master) || !reader.ReadPackedGuid(group)) {
      return;
    }
    master_guid_ = master;
    round_robin_guid_ = group;
  }

  [[nodiscard]] ObjectGuid MasterGuid() const noexcept { return master_guid_; }
  [[nodiscard]] ObjectGuid RoundRobinGuid() const noexcept {
    return round_robin_guid_;
  }

private:
  std::uint32_t corpse_ready_tick_{0};
  ObjectGuid master_guid_;
  ObjectGuid round_robin_guid_;
};

}
