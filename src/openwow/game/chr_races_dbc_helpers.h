#pragma once

#include <cstdint>

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::game {

[[nodiscard]] std::uint32_t ChrRaces_GetDisplayInfoForSex(
    const data::dbc::DbcLoader& dbc, std::uint32_t race_id, std::uint32_t sex);

}
