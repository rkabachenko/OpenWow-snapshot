#pragma once

#include "openwow/game/object_guid.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::game {

enum class GOType : std::uint8_t {
  Door,
  Button,
  QuestGiver,
  Chest,
  Binder,
  Generic,
  Trap,
  Chair,
  SpellFocus,
  Text,
  Goober,
  Transport,
  AreaDamage,
  Camera,
  MapObject,
  FlagStand,
  FishingBobber,
  Ritual,
  Mailbox,
  AuctionHouse,
  Forge,
  Anvil,
  MeetingStone,
  FlagDrop,
  FishingHole,
  FlagCapture,
  BarberChair,
  DestructibleBuilding,
  GuildBank,
};

struct GODataEntry {
  ObjectGuid guid;
  GOType goType{GOType::Generic};
  std::string name;
  std::uint32_t displayId{0};
  float x{0.0f};
  float y{0.0f};
  float z{0.0f};
  float facing{0.0f};
  std::uint32_t flags{0};
  std::uint32_t state{0};
  bool isUsable{false};
  bool isLocked{false};
  bool questItem{false};
};

class GameObjectDataView {
 public:
  GameObjectDataView() = default;

  void AddGameObject(const GODataEntry& entry);

  void RemoveGameObject(ObjectGuid guid);

  [[nodiscard]] std::optional<GODataEntry> GetGameObject(
      ObjectGuid guid) const;

  [[nodiscard]] bool HasGameObject(ObjectGuid guid) const;

  [[nodiscard]] std::vector<GODataEntry> GetGameObjectsByType(
      GOType type) const;

  [[nodiscard]] std::vector<GODataEntry> GetUsableGameObjects() const;

  [[nodiscard]] std::vector<GODataEntry> GetQuestGameObjects() const;

  [[nodiscard]] std::uint32_t GetGameObjectCount() const;

  [[nodiscard]] std::vector<GODataEntry> GetNearby(float x, float y, float z,
                                                   float range) const;

  [[nodiscard]] static std::string GetTypeName(GOType type);

  void UpdateState(ObjectGuid guid, std::uint32_t newState);

  void Clear();

 private:
  std::unordered_map<ObjectGuid, GODataEntry, ObjectGuid::Hash> objects_;
};

}
