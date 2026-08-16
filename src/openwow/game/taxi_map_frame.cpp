
#include "openwow/game/taxi_map_frame.h"

#include "openwow/game/taxi_handler.h"
#include "openwow/game/taxi_system.h"
#include "openwow/ui/game/script_event_dispatch.h"

namespace openwow::game {

std::uint64_t GetTaxiMapFrameNpcGuid(const TaxiHandler& taxi) {
  if (!TaxiSystem::Get().IsTaxiMapOpen()) {
    return 0;
  }

  return taxi.GetFlightMasterGuid();
}

void TaxiMapFrame_Close(TaxiHandler& taxi) {
  auto& taxi_system = TaxiSystem::Get();
  if (!taxi_system.IsTaxiMapOpen()) {
    return;
  }

  taxi.CloseTaxiMap();

  taxi_system.CloseTaxiMap();
  ui::game::ScriptEventDispatch::Get().FireTaxiMapClosed();
}

}
