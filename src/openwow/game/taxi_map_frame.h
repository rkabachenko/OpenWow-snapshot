
#pragma once

#include <cstdint>

namespace openwow::game {

class TaxiHandler;
class WorldSession;

void TaxiMapFrame_Close(WorldSession& session);

[[nodiscard]] std::uint64_t GetTaxiMapFrameNpcGuid(const TaxiHandler& taxi);

}
