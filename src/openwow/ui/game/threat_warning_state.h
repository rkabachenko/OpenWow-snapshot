#pragma once

#include <cstdint>
#include <mutex>
#include <string_view>

#include "openwow/game/world_session_fwd.h"

namespace openwow::ui::game {

struct ThreatWarningContext {
  std::uint32_t mode{0};
  bool is_instance_or_raid_map{false};
  bool has_party_or_raid{false};
};

class ThreatWarningState {
 public:
  static ThreatWarningState& Get();

  static ThreatWarningContext BuildContext(const openwow::game::WorldSession* session,
                                          std::string_view cvar_value);
  static bool Evaluate(const ThreatWarningContext& context);

  void EnsureBinding(const openwow::game::WorldSession* session = nullptr);
  void Refresh(const openwow::game::WorldSession& session);
  void Refresh(const ThreatWarningContext& context);

  [[nodiscard]] bool IsEnabled() const;

 private:
  ThreatWarningState() = default;

  void UpdateEnabled(const ThreatWarningContext& context, bool fire_events);

  mutable std::mutex mutex_;
  const openwow::game::WorldSession* session_{nullptr};
  bool binding_registered_{false};
  bool initialized_{false};
  bool enabled_{false};
};

}
