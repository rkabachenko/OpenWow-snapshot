
#pragma once

#include <cmath>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "openwow/game/object_guid.h"

namespace openwow::game {

struct GraveyardInfo {
  uint32_t graveyardId = 0;
  uint32_t mapId = 0;
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  std::string name;
  uint32_t factionRequired = 0;
};

struct CorpsePosition {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

class GraveyardSystem {
 public:
  static GraveyardSystem& Get();

  GraveyardSystem(const GraveyardSystem&) = delete;
  GraveyardSystem& operator=(const GraveyardSystem&) = delete;

  void AddGraveyard(const GraveyardInfo& info);
  void RemoveGraveyard(uint32_t graveyardId);
  [[nodiscard]] std::optional<GraveyardInfo> GetGraveyard(
      uint32_t graveyardId) const;
  [[nodiscard]] uint32_t GetGraveyardCount() const;

  [[nodiscard]] std::optional<GraveyardInfo> GetNearestGraveyard(
      float x, float y, float z, uint32_t mapId, uint32_t faction) const;

  [[nodiscard]] std::vector<GraveyardInfo> GetGraveyardsForMap(
      uint32_t mapId) const;

  [[nodiscard]] bool IsAtGraveyard() const;
  void SetAtGraveyard(bool at);

  void SetSpiritHealerGuid(ObjectGuid guid);
  [[nodiscard]] ObjectGuid GetSpiritHealerGuid() const;
  [[nodiscard]] bool HasSpiritHealer() const;

  [[nodiscard]] float GetResurrectionTimer() const;
  void SetResurrectionTimer(float seconds);

  [[nodiscard]] bool ShouldApplyResSickness() const;

  [[nodiscard]] float GetCorpseDistance() const;
  void SetCorpseDistance(float distance);

  [[nodiscard]] bool IsInGhostForm() const;
  void SetInGhostForm(bool ghost);

  void SetCorpsePosition(float x, float y, float z);
  [[nodiscard]] CorpsePosition GetCorpsePosition() const;

  void Update(float dt);

  void Reset();

 private:
  GraveyardSystem() = default;

  std::vector<GraveyardInfo> graveyards_;
  bool atGraveyard_ = false;
  ObjectGuid spiritHealerGuid_;
  float resurrectionTimer_ = 0.0f;
  float corpseDistance_ = 0.0f;
  bool inGhostForm_ = false;
  CorpsePosition corpsePos_;
  mutable std::mutex mutex_;
};

}
