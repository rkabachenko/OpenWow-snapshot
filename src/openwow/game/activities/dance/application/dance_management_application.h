#pragma once

#include "openwow/game/activities/dance/model/dance_studio_messages.h"

#include <optional>

namespace openwow::game {

class DanceCacheCoordinator;
class KnownDanceCatalog;

class DanceManagementApplication final {
 public:
  DanceManagementApplication(KnownDanceCatalog& known_dances,
                             DanceCacheCoordinator& cache);

  [[nodiscard]] std::optional<DanceManagementError> Apply(
      const DanceManagementNotification& notification);

 private:
  KnownDanceCatalog& known_dances_;
  DanceCacheCoordinator& cache_;
};

}
