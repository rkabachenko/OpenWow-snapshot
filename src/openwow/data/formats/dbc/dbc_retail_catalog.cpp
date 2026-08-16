#include "openwow/data/formats/dbc/dbc_entries_extended.h"
#include "openwow/data/formats/dbc/dbc_entries_gameplay.h"
#include "openwow/data/formats/dbc/dbc_entries_world.h"
#include "openwow/data/formats/dbc/dbc_store.h"
#include "openwow/data/formats/dbc/dbc_structures.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>

namespace openwow::data::dbc {
namespace {

#include "dbc_retail_catalog.inc"

constexpr auto kRetailDbcCatalog = std::array{
#define OPENWOW_DBC_DESCRIPTOR(type, member, path, fields, size) \
  RetailDbcDescriptor{path, fields, size},
    OPENWOW_RETAIL_DBC_CATALOG(OPENWOW_DBC_DESCRIPTOR)
#undef OPENWOW_DBC_DESCRIPTOR
};

consteval bool RetailCatalogIsValid() {
  if (kRetailDbcCatalog.size() != 241u) {
    return false;
  }

  for (std::size_t i = 0; i < kRetailDbcCatalog.size(); ++i) {
    const auto &entry = kRetailDbcCatalog[i];
    if (!entry.retail_path.starts_with("DBFilesClient\\") ||
        !entry.retail_path.ends_with(".dbc") || entry.field_count == 0u ||
        entry.record_size == 0u) {
      return false;
    }
    for (std::size_t j = i + 1u; j < kRetailDbcCatalog.size(); ++j) {
      if (entry.retail_path == kRetailDbcCatalog[j].retail_path) {
        return false;
      }
    }
  }
  return true;
}

static_assert(RetailCatalogIsValid());

}

const RetailDbcDescriptor &
FindRetailDbcDescriptor(const std::string_view filename) {
  const auto match =
      std::find_if(kRetailDbcCatalog.begin(), kRetailDbcCatalog.end(),
                   [filename](const RetailDbcDescriptor &entry) {
                     return entry.filename() == filename;
                   });
  if (match == kRetailDbcCatalog.end()) {
    throw std::out_of_range("Unknown retail DBC descriptor: " +
                            std::string(filename));
  }
  return *match;
}

#undef OPENWOW_RETAIL_DBC_CATALOG

}
