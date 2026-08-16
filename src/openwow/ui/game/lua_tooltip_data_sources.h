#pragma once

struct lua_State;

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::game {
class EquipmentSets;
class ItemDefinitions;
class PlayerInventoryReplica;
class WorldSession;
}

namespace openwow::ui::game {

struct TooltipDataSources final {

  const openwow::data::dbc::DbcLoader* dbc{nullptr};
  openwow::game::WorldSession* world_session{nullptr};
  const openwow::game::EquipmentSets* equipment{nullptr};
  const openwow::game::PlayerInventoryReplica* inventory{nullptr};
  openwow::game::ItemDefinitions* item_definitions{nullptr};
};

[[nodiscard]] TooltipDataSources ResolveLuaTooltipDataSources(lua_State* lua);

}
