#include "openwow/ui/game/threat_warning_state.h"

#include "openwow/core/decimal_parse.h"
#include "openwow/data/formats/dbc/dbc_enums.h"
#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/game/group_system.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/script_event_dispatch.h"

namespace openwow::ui::game {

ThreatWarningState& ThreatWarningState::Get() {
  static ThreatWarningState instance;
  return instance;
}

ThreatWarningContext ThreatWarningState::BuildContext(
    const openwow::game::WorldSession* const session,
    const std::string_view cvar_value) {
  ThreatWarningContext context;
  context.mode = openwow::core::ParseSignedDecimalLikeSub76F0D0(cvar_value);
  context.has_party_or_raid = openwow::game::GroupSystem::Get().IsInGroup();

  if (session != nullptr) {
    if (const auto* const map_entry = session->LookupMapEntry(session->current_map_id())) {
      context.is_instance_or_raid_map =
          map_entry->map_type == static_cast<std::uint32_t>(openwow::data::dbc::MapType::kInstance) ||
          map_entry->map_type == static_cast<std::uint32_t>(openwow::data::dbc::MapType::kRaid);
    }
  }

  return context;
}

bool ThreatWarningState::Evaluate(const ThreatWarningContext& context) {
  switch (context.mode) {
    case 0:
      return false;
    case 1:
      return context.is_instance_or_raid_map;
    case 2:
      return context.has_party_or_raid;
    default:
      return true;
  }
}

void ThreatWarningState::EnsureBinding(const openwow::game::WorldSession* const session) {
  std::string current_value;
  const openwow::game::WorldSession* session_for_sync = nullptr;
  bool register_binding = false;
  bool needs_initial_sync = false;

  {
    std::lock_guard lock(mutex_);
    if (session != nullptr) {
      session_ = session;
    }
    session_for_sync = session_;
    if (!binding_registered_) {
      binding_registered_ = true;
      register_binding = true;
    }
    if (!initialized_) {
      current_value = CVarSystem::Instance().GetCVar("threatWarning");
      needs_initial_sync = true;
    }
  }

  if (register_binding) {
    CVarSystem::Instance().AddCallback(
        "threatWarning",
        [this](const std::string&, const std::string& new_value) {
          const openwow::game::WorldSession* session = nullptr;
          {
            std::lock_guard lock(mutex_);
            session = session_;
          }
          Refresh(BuildContext(session, new_value));
        });
  }

  if (needs_initial_sync) {
    UpdateEnabled(BuildContext(session_for_sync, current_value), false);
  }
}

void ThreatWarningState::Refresh(const openwow::game::WorldSession& session) {
  {
    std::lock_guard lock(mutex_);
    session_ = &session;
  }
  Refresh(BuildContext(&session, CVarSystem::Instance().GetCVar("threatWarning")));
}

void ThreatWarningState::Refresh(const ThreatWarningContext& context) {
  UpdateEnabled(context, true);
}

bool ThreatWarningState::IsEnabled() const {
  std::lock_guard lock(mutex_);
  return enabled_;
}

void ThreatWarningState::UpdateEnabled(const ThreatWarningContext& context,
                                       const bool fire_events) {
  {
    std::lock_guard lock(mutex_);
    enabled_ = Evaluate(context);
    initialized_ = true;
  }

  if (!fire_events) {
    return;
  }

  auto& dispatch = ScriptEventDispatch::Get();
  dispatch.FireEvent(events::UNIT_THREAT_SITUATION_UPDATE);
  dispatch.FireEvent(events::UNIT_THREAT_LIST_UPDATE);
}

}
