#pragma once

#include "openwow/game/object_guid.h"

#include <string_view>

namespace openwow::game {

class BattlefieldInfo;
class GroupSystem;
class InstanceHandler;
class ObjectManager;

[[nodiscard]] ObjectGuid ResolveUnitToken(
    ObjectManager& objects, const GroupSystem& group,
    const BattlefieldInfo& battlefield, const InstanceHandler& instance,
    std::string_view token);

}
