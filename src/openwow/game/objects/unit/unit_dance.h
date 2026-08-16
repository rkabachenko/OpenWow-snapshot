#pragma once

#include "openwow/game/activities/dance/application/unit_dance_state.h"
#include "openwow/game/activities/dance/model/dance_sequence.h"
#include "openwow/game/activities/dance/model/dance_move_catalog.h"

#include <memory>

namespace openwow::game {

class CGUnit_C;
class WorldSession;

enum class DanceSynchronizationRole : std::uint8_t;

class UnitDanceComponent final {
public:
  UnitDanceComponent() = default;
  UnitDanceComponent(const UnitDanceComponent &) = delete;
  UnitDanceComponent &operator=(const UnitDanceComponent &) = delete;
  UnitDanceComponent(UnitDanceComponent &&) noexcept = default;
  UnitDanceComponent &operator=(UnitDanceComponent &&) noexcept = default;
  ~UnitDanceComponent() = default;

  [[nodiscard]] UnitDanceState &Get(CGUnit_C &owner, const WorldSession &session);
  [[nodiscard]] const UnitDanceState &Get() const noexcept;

  void Cancel() noexcept;
  void SetData(CGUnit_C &owner, const WorldSession &session,
               DanceSequence sequence,
               const DanceMoveCatalog &catalog);

  [[nodiscard]] bool IsActive() const noexcept { return Get().IsActive(); }

private:
  std::unique_ptr<UnitDanceState> state_;
};

}
