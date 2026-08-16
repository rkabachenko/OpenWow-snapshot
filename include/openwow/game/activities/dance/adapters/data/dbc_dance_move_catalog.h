#pragma once

#include "openwow/game/activities/dance/model/dance_move_catalog.h"

namespace openwow::data::dbc {

struct DanceMovesEntry;

template <typename Entry>
class DbcStore;

}

namespace openwow::game {

[[nodiscard]] DanceMoveCatalog BuildDanceMoveCatalog(
    const data::dbc::DbcStore<data::dbc::DanceMovesEntry>& store);

}
