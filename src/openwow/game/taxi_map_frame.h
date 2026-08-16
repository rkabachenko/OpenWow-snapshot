
#pragma once

#include <cstdint>

namespace openwow::game {

class TaxiHandler;

void TaxiMapFrame_Close(TaxiHandler& taxi);

[[nodiscard]] std::uint64_t GetTaxiMapFrameNpcGuid(const TaxiHandler& taxi);

}
