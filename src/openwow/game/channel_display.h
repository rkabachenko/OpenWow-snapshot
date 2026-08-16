#pragma once

#include "openwow/game/object_guid.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace openwow::game {

struct ChannelTickInfo {
  float         tickTime    = 0.0f;
  float         nextTickAt  = 0.0f;
  std::uint32_t tickCount   = 0;
  std::uint32_t totalTicks  = 0;
};

struct ChannelDisplayInfo {
  std::uint32_t          spellId       = 0;
  std::string            name;
  float                  totalDuration = 0.0f;
  float                  remaining     = 0.0f;
  std::uint32_t          targetCount   = 0;
  std::vector<ObjectGuid> targets;
  ChannelTickInfo        tickInfo;
  bool                   isBreakable   = true;
};

class ChannelDisplay {
 public:
  ChannelDisplay() = default;

  void Start(std::uint32_t spellId, const std::string& name,
             float duration, std::uint32_t totalTicks = 0);

  void Cancel();
  void Reset();

  void AddTarget(const ObjectGuid& guid);
  void RemoveTarget(const ObjectGuid& guid);

  [[nodiscard]] std::vector<ObjectGuid> GetTargets() const;
  [[nodiscard]] std::size_t GetTargetCount() const;

  void SetBreakable(bool breakable);
  [[nodiscard]] bool IsBreakable() const;

  [[nodiscard]] ChannelTickInfo GetTickInfo() const;

  void ProcessTick();

  [[nodiscard]] float GetProgress() const;

  [[nodiscard]] std::uint32_t GetSpellId() const;
  [[nodiscard]] bool  IsChanneling() const;
  [[nodiscard]] float GetRemaining() const;

  void Update(float dt);

 private:
  bool          active_         = false;
  std::uint32_t spell_id_       = 0;
  std::string   name_;
  float         total_duration_ = 0.0f;
  float         remaining_      = 0.0f;
  bool          breakable_      = true;

  ChannelTickInfo tick_info_;
  std::vector<ObjectGuid> targets_;
};

}
