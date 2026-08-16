#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::game {

class Localization;

std::string FormatItemDisplayNameWithRandomProperty(
    Localization& localization,
    const openwow::data::dbc::DbcLoader* dbc,
    std::string_view base_name,
    std::int32_t random_property_id);

}
