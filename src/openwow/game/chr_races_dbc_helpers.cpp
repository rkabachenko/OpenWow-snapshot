
#include "openwow/game/chr_races_dbc_helpers.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <cassert>

namespace openwow::game {

std::uint32_t ChrRaces_GetDisplayInfoForSex(
    const data::dbc::DbcLoader& dbc,
    const std::uint32_t race_id,
    const std::uint32_t sex) {

  assert(sex < 3 && "ChrRaces_GetDisplayInfoForSex: sex must be < 3");

  const auto* entry = dbc.chr_races().LookupEntry(race_id);
  if (entry == nullptr) {
    return 0;
  }

  switch (sex) {
    case 0:
      return entry->model_male;

    case 1:
      return entry->model_female;

    default:
      return 0;
  }
}

}
