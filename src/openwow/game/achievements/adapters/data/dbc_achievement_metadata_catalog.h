#pragma once

#include "openwow/game/achievements/application/achievement_metadata_catalog.h"

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::game {

class DbcAchievementMetadataCatalog final
    : public AchievementMetadataCatalog {
 public:
  explicit DbcAchievementMetadataCatalog(
      const data::dbc::DbcLoader* dbc = nullptr)
      : dbc_(dbc) {}

  void Bind(const data::dbc::DbcLoader* dbc) {
    dbc_ = dbc;
  }

  [[nodiscard]] bool Contains(AchievementId achievement_id) const override;
  [[nodiscard]] std::uint32_t OrderInGroup(
      AchievementId achievement_id) const override;

 private:
  const data::dbc::DbcLoader* dbc_ = nullptr;
};

}
