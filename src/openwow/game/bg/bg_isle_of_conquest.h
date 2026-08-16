#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace openwow::game {

enum class IocNodeState : std::uint8_t {
  kUncontrolled        = 0,
  kConflictAlliance    = 1,
  kConflictHorde       = 2,
  kControlledAlliance  = 3,
  kControlledHorde     = 4,
  kUnknown             = 5,
};

enum class IocGateState : std::uint8_t {
  kOk       = 1,
  kDamaged  = 2,
  kDestroyed = 3,
  kUnknown  = 0,
};

enum class IocNode : std::uint8_t {
  kDocks      = 0,
  kHangar     = 1,
  kWorkshop   = 2,
  kQuarry     = 3,
  kRefinery   = 4,
  kAllianceKeep = 5,
  kHordeKeep  = 6,
  kCount      = 7,
};

enum class IocGate : std::uint8_t {
  kHordeFront  = 0,
  kHordeWest   = 1,
  kHordeEast   = 2,
  kAllianceFront = 3,
  kAllianceWest  = 4,
  kAllianceEast  = 5,
  kCount       = 6,
};

class BgIsleOfConquest {
 public:

  static constexpr std::int32_t kWsAllianceReinfSet    = 4221;
  static constexpr std::int32_t kWsHordeReinfSet        = 4222;
  static constexpr std::int32_t kWsAllianceReinf         = 4226;
  static constexpr std::int32_t kWsHordeReinf            = 4227;

  struct NodeWorldStates {
    std::int32_t uncontrolled;
    std::int32_t conflict_alliance;
    std::int32_t conflict_horde;
    std::int32_t controlled_alliance;
    std::int32_t controlled_horde;
  };

  static constexpr NodeWorldStates kNodeWS[7] = {
    {4301, 4305, 4302, 4304, 4303},
    {4296, 4300, 4297, 4299, 4298},
    {4294, 4228, 4293, 4229, 4230},
    {4306, 4310, 4307, 4309, 4308},
    {4311, 4315, 4312, 4314, 4313},
    {4341, 4342, 4343, 4339, 4340},
    {4346, 4347, 4348, 4344, 4345},
  };

  struct GateWorldStates {
    std::int32_t closed;
    std::int32_t open;
  };

  static constexpr GateWorldStates kGateWS[6] = {
    {4317, 4322},
    {4318, 4321},
    {4319, 4320},
    {4328, 4323},
    {4327, 4324},
    {4326, 4325},
  };

  static constexpr int kStartingReinforcements = 300;

  [[nodiscard]] static bool IsRelevantWorldState(std::int32_t ws_id);

  void OnWorldStateUpdate(std::int32_t ws_id, std::int32_t value);

  void Update(float dt);

  [[nodiscard]] int GetAllianceReinforcements() const { return alliance_reinforcements_; }
  [[nodiscard]] int GetHordeReinforcements() const { return horde_reinforcements_; }

  [[nodiscard]] IocNodeState GetNodeState(IocNode node) const;
  [[nodiscard]] IocGateState GetGateState(IocGate gate) const;

  [[nodiscard]] int GetAllianceNodesControlled() const;
  [[nodiscard]] int GetHordeNodesControlled() const;
  [[nodiscard]] int GetAllianceNodesConflict() const;
  [[nodiscard]] int GetHordeNodesConflict() const;

  [[nodiscard]] bool IsBossKilled(bool alliance) const;
  [[nodiscard]] bool IsFinished() const;

  [[nodiscard]] std::string GetScoreText() const;
  [[nodiscard]] std::string GetStatusText() const;

  [[nodiscard]] int GetWinner() const;

  [[nodiscard]] static std::string_view GetNodeName(IocNode node);
  [[nodiscard]] static std::string_view GetGateName(IocGate gate);

  void Reset();

 private:
  int alliance_reinforcements_{0};
  int horde_reinforcements_{0};

  std::array<IocNodeState, static_cast<std::size_t>(IocNode::kCount)> nodes_{};
  std::array<IocGateState, static_cast<std::size_t>(IocGate::kCount)> gates_{};

  bool alliance_boss_alive_{true};
  bool horde_boss_alive_{true};
};

}
