#include "openwow/game/achievements/adapters/data/dbc_achievement_metadata_catalog.h"

#include "openwow/data/formats/dbc/dbc_loader.h"

namespace openwow::game {
namespace {

[[nodiscard]] const data::dbc::AchievementEntry* LookupAchievement(
    const data::dbc::DbcLoader* dbc,
    const AchievementId achievement_id) {
  return dbc == nullptr
             ? nullptr
             : dbc->achievement().LookupEntry(achievement_id.value);
}

}

bool DbcAchievementMetadataCatalog::Contains(
    const AchievementId achievement_id) const {
  return LookupAchievement(dbc_, achievement_id) != nullptr;
}

std::uint32_t DbcAchievementMetadataCatalog::OrderInGroup(
    const AchievementId achievement_id) const {
  const auto* achievement = LookupAchievement(dbc_, achievement_id);
  return achievement == nullptr ? 0 : achievement->order_in_group;
}

}
