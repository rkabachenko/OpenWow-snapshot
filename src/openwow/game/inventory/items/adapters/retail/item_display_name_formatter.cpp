#include "openwow/game/inventory/items/adapters/retail/item_display_name_formatter.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/localization.h"

namespace openwow::game {

std::string FormatItemDisplayNameWithRandomProperty(
    Localization& localization,
    const openwow::data::dbc::DbcLoader* dbc,
    std::string_view base_name,
    const std::int32_t random_property_id) {
  std::string display_name(base_name);
  if (display_name.empty() || random_property_id == 0 || dbc == nullptr) {
    return display_name;
  }

  std::string suffix_name;
  bool should_format = false;
  if (random_property_id > 0) {
    if (const auto* entry = dbc->item_random_suffix().LookupEntry(
            static_cast<std::uint32_t>(random_property_id));
        entry != nullptr && !entry->name.empty()) {
      suffix_name.assign(entry->name.data(), entry->name.size());
      should_format = true;
    }
  } else {
    should_format = true;
    if (const auto* entry = dbc->item_random_properties().LookupEntry(
            static_cast<std::uint32_t>(-random_property_id));
        entry != nullptr) {
      suffix_name.assign(entry->name.data(), entry->name.size());
    }
  }

  if (!should_format) {
    return display_name;
  }

  const auto format = localization.GetString("ITEM_SUFFIX_TEMPLATE");
  if (format.empty()) {
    return display_name;
  }

  return localization.FormatString(format, {display_name, suffix_name});
}

}
