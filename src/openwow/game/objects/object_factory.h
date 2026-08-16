#pragma once

#include "openwow/game/objects/cgcorpse.h"
#include "openwow/game/objects/cgdynamicobject.h"
#include "openwow/game/objects/cggameobject.h"
#include "openwow/game/objects/cgitem.h"
#include "openwow/game/objects/cgobject.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/objects/cgunit.h"

#include <memory>

namespace openwow::game {

inline std::unique_ptr<CGObject_C> CreateObject(ObjectGuid guid,
                                                TypeID type_id,
                                                ObjectManager& objects,
                                                PlayerInventoryReplica& inventory,
                                                ItemDefinitions& item_definitions,
                                                const data::dbc::DbcLoader& dbc_loader) {
  switch (type_id) {
    case TypeID::kObject:
      return std::make_unique<CGObject_C>(objects, guid, type_id);
    case TypeID::kItem:
      return std::make_unique<CGItem_C>(objects, item_definitions, guid,
                                        TypeID::kItem);
    case TypeID::kContainer:
      return std::make_unique<CGContainer_C>(objects, item_definitions, guid);
    case TypeID::kUnit:
      return std::make_unique<CGUnit_C>(
          objects, item_definitions, dbc_loader, guid, TypeID::kUnit);
    case TypeID::kPlayer:
      return std::make_unique<CGPlayer_C>(
          objects, item_definitions, dbc_loader, guid);
    case TypeID::kGameObject:
      return std::make_unique<CGGameObject_C>(objects, inventory, guid);
    case TypeID::kDynamicObject:
      return std::make_unique<CGDynamicObject_C>(objects, guid);
    case TypeID::kCorpse:
      return std::make_unique<CGCorpse_C>(objects, guid);
  }
  return std::make_unique<CGObject_C>(objects, guid, type_id);
}

}
