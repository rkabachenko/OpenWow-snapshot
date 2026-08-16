#pragma once

#include "openwow/game/faction_manager.h"

#include <cstddef>
#include <cstdint>

namespace openwow::game {

class ObjectManager;
class ReputationInfo;

class ReputationRuntime final {
 public:
  enum class Mutation {
    kInitialize,
    kStanding,
    kVisible,
    kAtWar,
  };

  bool Apply(Mutation mutation, ReputationInfo& reputation,
             const ObjectManager& objects, const std::uint8_t* data,
             std::size_t size);
  void Clear() { factions_.Clear(); }

  [[nodiscard]] FactionManager& factions() noexcept { return factions_; }
  [[nodiscard]] const FactionManager& factions() const noexcept {
    return factions_;
  }

 private:
  FactionManager factions_;
};

}
