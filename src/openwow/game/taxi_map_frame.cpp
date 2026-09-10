
#include "openwow/game/taxi_map_frame.h"

#include "openwow/game/taxi_handler.h"
#include "openwow/game/taxi_system.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/surfaces/game/runtime/npc_interaction_controller.h"

namespace openwow::game {

std::uint64_t GetTaxiMapFrameNpcGuid(const TaxiHandler& taxi) {
  if (!TaxiSystem::Get().IsTaxiMapOpen()) {
    return 0;
  }

  return taxi.GetFlightMasterGuid();
}

void TaxiMapFrame_Close(WorldSession& session) {
  auto& taxi = session.taxi();
  auto& taxi_system = TaxiSystem::Get();
  if (!taxi_system.IsTaxiMapOpen()) {
    return;
  }

  ui::game::HandleNpcInteractionLoss(
      session, ObjectGuid(taxi.GetFlightMasterGuid()),
      ui::game::NpcInteractionClosureCause::UnitUnavailable);
  // Common interaction closure may already have closed this map and delivered
  // TAXIMAP_CLOSED through the same service entry point.
  if (!taxi_system.IsTaxiMapOpen()) {
    return;
  }

  taxi.CloseTaxiMap();

  taxi_system.CloseTaxiMap();
  ui::game::ScriptEventDispatch::Get().FireTaxiMapClosed();
}

}
