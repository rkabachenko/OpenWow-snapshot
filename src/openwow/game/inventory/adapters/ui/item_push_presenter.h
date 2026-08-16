#pragma once

#include <functional>
#include <string>
#include <vector>

namespace openwow::data::dbc {
class DbcLoader;
}
namespace openwow::game {
struct ItemPushResult;
class ObjectManager;
class QueryCache;
class Localization;
class TutorialSystem;
namespace inventory::ui {

struct ItemPushPresentationCapabilities {
  Localization* localization = nullptr;
  TutorialSystem* tutorials = nullptr;
  std::function<void(std::string)> display_loot_message;
  std::function<void(int slot, std::string icon)> fire_item_push;
};

void PresentItemPushResult(
    ObjectManager& objects, QueryCache& queries,
    const openwow::data::dbc::DbcLoader* dbc,
    const ItemPushResult& result,
    ItemPushPresentationCapabilities capabilities);

}
}
