
#include "openwow/game/gameobject_data_view.h"

#include <cmath>

namespace openwow::game {

void GameObjectDataView::AddGameObject(const GODataEntry& entry) {
  objects_.insert_or_assign(entry.guid, entry);
}

void GameObjectDataView::RemoveGameObject(ObjectGuid guid) {
  objects_.erase(guid);
}

std::optional<GODataEntry> GameObjectDataView::GetGameObject(
    ObjectGuid guid) const {
  auto it = objects_.find(guid);
  if (it != objects_.end()) return it->second;
  return std::nullopt;
}

bool GameObjectDataView::HasGameObject(ObjectGuid guid) const {
  return objects_.contains(guid);
}

std::vector<GODataEntry> GameObjectDataView::GetGameObjectsByType(
    GOType type) const {
  std::vector<GODataEntry> result;
  for (const auto& [guid, entry] : objects_) {
    if (entry.goType == type) result.push_back(entry);
  }
  return result;
}

std::vector<GODataEntry> GameObjectDataView::GetUsableGameObjects() const {
  std::vector<GODataEntry> result;
  for (const auto& [guid, entry] : objects_) {
    if (entry.isUsable) result.push_back(entry);
  }
  return result;
}

std::vector<GODataEntry> GameObjectDataView::GetQuestGameObjects() const {
  std::vector<GODataEntry> result;
  for (const auto& [guid, entry] : objects_) {
    if (entry.questItem) result.push_back(entry);
  }
  return result;
}

std::uint32_t GameObjectDataView::GetGameObjectCount() const {
  return static_cast<std::uint32_t>(objects_.size());
}

std::vector<GODataEntry> GameObjectDataView::GetNearby(float x, float y,
                                                       float z,
                                                       float range) const {
  const float range_sq = range * range;
  std::vector<GODataEntry> result;
  for (const auto& [guid, entry] : objects_) {
    const float dx = entry.x - x;
    const float dy = entry.y - y;
    const float dz = entry.z - z;
    if (dx * dx + dy * dy + dz * dz <= range_sq) {
      result.push_back(entry);
    }
  }
  return result;
}

std::string GameObjectDataView::GetTypeName(GOType type) {
  switch (type) {
    case GOType::Door:                 return "Door";
    case GOType::Button:               return "Button";
    case GOType::QuestGiver:           return "Quest Giver";
    case GOType::Chest:                return "Chest";
    case GOType::Binder:               return "Binder";
    case GOType::Generic:              return "Generic";
    case GOType::Trap:                 return "Trap";
    case GOType::Chair:                return "Chair";
    case GOType::SpellFocus:           return "Spell Focus";
    case GOType::Text:                 return "Text";
    case GOType::Goober:               return "Goober";
    case GOType::Transport:            return "Transport";
    case GOType::AreaDamage:           return "Area Damage";
    case GOType::Camera:               return "Camera";
    case GOType::MapObject:            return "Map Object";
    case GOType::FlagStand:            return "Flag Stand";
    case GOType::FishingBobber:        return "Fishing Bobber";
    case GOType::Ritual:               return "Ritual";
    case GOType::Mailbox:              return "Mailbox";
    case GOType::AuctionHouse:         return "Auction House";
    case GOType::Forge:                return "Forge";
    case GOType::Anvil:                return "Anvil";
    case GOType::MeetingStone:         return "Meeting Stone";
    case GOType::FlagDrop:             return "Flag Drop";
    case GOType::FishingHole:          return "Fishing Hole";
    case GOType::FlagCapture:          return "Flag Capture";
    case GOType::BarberChair:          return "Barber Chair";
    case GOType::DestructibleBuilding: return "Destructible Building";
    case GOType::GuildBank:            return "Guild Bank";
  }
  return "Unknown";
}

void GameObjectDataView::UpdateState(ObjectGuid guid,
                                     std::uint32_t newState) {
  auto it = objects_.find(guid);
  if (it != objects_.end()) {
    it->second.state = newState;
  }
}

void GameObjectDataView::Clear() {
  objects_.clear();
}

}
