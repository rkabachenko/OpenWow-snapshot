#pragma once

namespace openwow::game {

class CGUnit_C;
class ObjectManager;

enum class ControlledGroupScope {
  kParty,
  kRaid,
};

[[nodiscard]] bool IsActivePlayerControlledGroupLink(
    const CGUnit_C &left, const CGUnit_C &right, ControlledGroupScope scope,
    const ObjectManager &objects);

[[nodiscard]] bool IsPlayerOwnedCritterLootCase(const CGUnit_C &unit);

}
