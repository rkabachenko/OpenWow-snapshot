#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"

#include "openwow/game/player_bag_family.h"

#include "openwow/data/formats/dbc/dbc_entries_gameplay.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/world_session.h"
#include "openwow/foundation/math/client_rounding.h"

#include <cmath>

namespace openwow::game {

int BagFamilyMaskToDbcId(const std::uint32_t bag_family_mask) {
  if (bag_family_mask == 0) {
    return -1;
  }
  const double log2_val = std::log2(static_cast<double>(bag_family_mask));
  return math::RoundFloatHalfAwayFromZero(static_cast<float>(log2_val)) + 1;
}

bool DisplayBagFamilyText(const WorldSession &session,
                          const std::uint8_t bag_type_subclass) {
  if (bag_type_subclass == 0xFF) {
    return false;
  }

  const auto *dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    return false;
  }

  const auto &inv = session.inventory_replica();
  std::uint32_t bag_family_mask = 0;

  for (std::uint8_t i = 0; i < PlayerInventoryReplica::kMaxBags; ++i) {
    const auto *bag = inv.GetBag(i);
    if (bag == nullptr || bag->IsEmpty()) {
      continue;
    }
    const auto *tmpl = session.query_cache().GetItemTemplate(bag->entry);
    if (tmpl == nullptr) {
      continue;
    }
    if (tmpl->subclass == bag_type_subclass) {
      bag_family_mask = tmpl->bag_family;
      break;
    }
  }

  if (bag_family_mask == 0) {
    for (std::uint8_t i = 0; i < PlayerInventoryReplica::kMaxBankBags; ++i) {
      const auto *bag = inv.GetBankBag(i);
      if (bag == nullptr || bag->IsEmpty()) {
        continue;
      }
      const auto *tmpl = session.query_cache().GetItemTemplate(bag->entry);
      if (tmpl == nullptr) {
        continue;
      }
      if (tmpl->subclass == bag_type_subclass) {
        bag_family_mask = tmpl->bag_family;
        break;
      }
    }
  }

  if (bag_family_mask == 0) {
    return false;
  }

  const int dbc_id = BagFamilyMaskToDbcId(bag_family_mask);
  if (dbc_id <= 0) {
    return false;
  }

  const auto *entry = dbc->item_bag_family().LookupEntry(
      static_cast<std::uint32_t>(dbc_id));
  if (entry == nullptr) {
    return false;
  }

  ui::game::DisplaySystemMessage(kBagFamilySystemMessageId,
                                 entry->name.data());
  return true;
}

}
