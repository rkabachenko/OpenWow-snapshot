#include "openwow/game/session/reputation_runtime.h"

#include "openwow/game/object_manager.h"
#include "openwow/game/reputation_info.h"

namespace openwow::game {

bool ReputationRuntime::Apply(const Mutation mutation,
                              ReputationInfo& reputation,
                              const ObjectManager& objects,
                              const std::uint8_t* data,
                              const std::size_t size) {
  switch (mutation) {
    case Mutation::kInitialize:
      return reputation.HandleInitializeFactions(objects, data, size) &&
             factions_.HandleInitializeFactions(data, size);
    case Mutation::kStanding:
      return reputation.HandleSetFactionStanding(objects, data, size) &&
             factions_.HandleSetFactionStanding(data, size);
    case Mutation::kVisible:
      return reputation.HandleSetFactionVisible(objects, data, size) &&
             factions_.HandleSetFactionVisible(data, size);
    case Mutation::kAtWar:
      return reputation.HandleSetFactionAtWar(data, size) &&
             factions_.HandleSetFactionAtWar(data, size);
  }
  return false;
}

}
