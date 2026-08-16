#pragma once

#include "openwow/game/object_guid.h"

#include <cstdint>

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::ui {
class UIErrorManager;
namespace game {
class ScriptEventDispatch;
}
}

namespace openwow::game {
class InteractionSender;
class ItemDefinitions;
class ItemInteractionSession;
class Localization;
class ObjectManager;
class PlayerInventoryReplica;
class QueryCache;
class SpellCastRuntime;
struct ItemInstance;
struct ItemTemplate;

namespace inventory::ui {

enum class ItemTargetConfirmation : std::uint8_t {
  kPrompt,
  kConfirmed,
};

[[nodiscard]] bool TryStartItemSpellTargeting(
    const openwow::data::dbc::DbcLoader* dbc, SpellCastRuntime& spells,
    const ItemInstance &item,
    const ItemTemplate &item_template);
void ProcessItemSpellTarget(const openwow::data::dbc::DbcLoader* dbc,
                            PlayerInventoryReplica& inventory,
                            QueryCache& item_definitions,
                            ItemInteractionSession& interactions, ObjectManager& objects,
                            InteractionSender& interaction, SpellCastRuntime& spells,
                            Localization& localization,
                            openwow::ui::UIErrorManager& errors,
                            openwow::ui::game::ScriptEventDispatch& events,
                            ObjectGuid item_guid,
                            ItemTargetConfirmation confirmation);

}
}
