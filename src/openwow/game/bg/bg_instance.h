#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>

#include "openwow/game/bg/bg_alterac_valley.h"
#include "openwow/game/bg/bg_arathi_basin.h"
#include "openwow/game/bg/bg_eye_of_the_storm.h"
#include "openwow/game/bg/bg_isle_of_conquest.h"
#include "openwow/game/bg/bg_strand_of_the_ancients.h"
#include "openwow/game/bg/bg_warsong_gulch.h"

namespace openwow::game {

enum class BgType : std::uint32_t {
  kNone           = 0,
  kAlteracValley  = 1,
  kWarsongGulch   = 2,
  kArathiBasin    = 3,

  kEyeOfTheStorm  = 7,
  kStrandOfAncients = 9,
  kIsleOfConquest  = 30,
  kRandomBG        = 32,
};

class BgInstance {
 public:
  BgInstance();

  void Enter(BgType type);

  void Leave();

  void OnWorldStateUpdate(std::int32_t ws_id, std::int32_t value);

  void OnInitWorldStates(
      const std::unordered_map<std::int32_t, std::int32_t>& states);

  void Update(float dt);

  [[nodiscard]] BgType GetType() const { return type_; }
  [[nodiscard]] bool IsActive() const { return type_ != BgType::kNone; }
  [[nodiscard]] bool IsFinished() const;

  [[nodiscard]] BgWarsongGulch* GetWSG();
  [[nodiscard]] const BgWarsongGulch* GetWSG() const;

  [[nodiscard]] BgArathiBasin* GetAB();
  [[nodiscard]] const BgArathiBasin* GetAB() const;

  [[nodiscard]] BgAlteracValley* GetAV();
  [[nodiscard]] const BgAlteracValley* GetAV() const;

  [[nodiscard]] BgEyeOfTheStorm* GetEotS();
  [[nodiscard]] const BgEyeOfTheStorm* GetEotS() const;

  [[nodiscard]] BgStrandOfTheAncients* GetSotA();
  [[nodiscard]] const BgStrandOfTheAncients* GetSotA() const;

  [[nodiscard]] BgIsleOfConquest* GetIoC();
  [[nodiscard]] const BgIsleOfConquest* GetIoC() const;

  void Reset();

 private:
  BgType type_{BgType::kNone};

  std::unique_ptr<BgWarsongGulch> wsg_;
  std::unique_ptr<BgArathiBasin> ab_;
  std::unique_ptr<BgAlteracValley> av_;
  std::unique_ptr<BgEyeOfTheStorm> eots_;
  std::unique_ptr<BgStrandOfTheAncients> sota_;
  std::unique_ptr<BgIsleOfConquest> ioc_;
};

}
