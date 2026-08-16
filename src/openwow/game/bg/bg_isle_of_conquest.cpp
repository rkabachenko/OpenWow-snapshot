
#include "openwow/game/bg/bg_isle_of_conquest.h"

#include <algorithm>
#include <string>

namespace openwow::game {

bool BgIsleOfConquest::IsRelevantWorldState(std::int32_t ws_id) {
  if (ws_id == kWsAllianceReinfSet || ws_id == kWsHordeReinfSet ||
      ws_id == kWsAllianceReinf || ws_id == kWsHordeReinf) {
    return true;
  }

  for (int n = 0; n < 7; ++n) {
    for (int s = 0; s < 5; ++s) {
      if (ws_id == (&kNodeWS[n].uncontrolled)[s]) return true;
    }
  }

  for (int g = 0; g < 6; ++g) {
    if (ws_id == kGateWS[g].closed || ws_id == kGateWS[g].open) return true;
  }

  return false;
}

void BgIsleOfConquest::OnWorldStateUpdate(std::int32_t ws_id,
                                           std::int32_t value) {

  if (ws_id == kWsAllianceReinf) {
    alliance_reinforcements_ = std::max(0, value);
    return;
  }
  if (ws_id == kWsHordeReinf) {
    horde_reinforcements_ = std::max(0, value);
    return;
  }

  if (value == 1) {
    for (int n = 0; n < 7; ++n) {
      const auto& ws = kNodeWS[n];

      if (ws_id == ws.uncontrolled) {
        nodes_[n] = IocNodeState::kUncontrolled;
        return;
      }
      if (ws_id == ws.conflict_alliance) {
        nodes_[n] = IocNodeState::kConflictAlliance;
        return;
      }
      if (ws_id == ws.conflict_horde) {
        nodes_[n] = IocNodeState::kConflictHorde;
        return;
      }
      if (ws_id == ws.controlled_alliance) {
        nodes_[n] = IocNodeState::kControlledAlliance;
        return;
      }
      if (ws_id == ws.controlled_horde) {
        nodes_[n] = IocNodeState::kControlledHorde;
        return;
      }
    }
  }

  if (value == 1) {
    for (int g = 0; g < 6; ++g) {
      if (ws_id == kGateWS[g].closed) {
        gates_[g] = IocGateState::kOk;
        return;
      }
      if (ws_id == kGateWS[g].open) {

        gates_[g] = IocGateState::kDestroyed;
        return;
      }
    }
  }

}

void BgIsleOfConquest::Update(float ) {

  alliance_boss_alive_ =
      (GetNodeState(IocNode::kHordeKeep) != IocNodeState::kControlledAlliance ||
       horde_reinforcements_ > 0);
  horde_boss_alive_ =
      (GetNodeState(IocNode::kAllianceKeep) != IocNodeState::kControlledHorde ||
       alliance_reinforcements_ > 0);
}

IocNodeState BgIsleOfConquest::GetNodeState(IocNode node) const {
  auto idx = static_cast<std::size_t>(node);
  if (idx >= nodes_.size()) return IocNodeState::kUnknown;
  return nodes_[idx];
}

IocGateState BgIsleOfConquest::GetGateState(IocGate gate) const {
  auto idx = static_cast<std::size_t>(gate);
  if (idx >= gates_.size()) return IocGateState::kUnknown;
  return gates_[idx];
}

int BgIsleOfConquest::GetAllianceNodesControlled() const {
  int count = 0;
  for (auto s : nodes_) {
    if (s == IocNodeState::kControlledAlliance) ++count;
  }
  return count;
}

int BgIsleOfConquest::GetHordeNodesControlled() const {
  int count = 0;
  for (auto s : nodes_) {
    if (s == IocNodeState::kControlledHorde) ++count;
  }
  return count;
}

int BgIsleOfConquest::GetAllianceNodesConflict() const {
  int count = 0;
  for (auto s : nodes_) {
    if (s == IocNodeState::kConflictAlliance) ++count;
  }
  return count;
}

int BgIsleOfConquest::GetHordeNodesConflict() const {
  int count = 0;
  for (auto s : nodes_) {
    if (s == IocNodeState::kConflictHorde) ++count;
  }
  return count;
}

bool BgIsleOfConquest::IsBossKilled(bool alliance) const {
  return alliance ? !alliance_boss_alive_ : !horde_boss_alive_;
}

bool BgIsleOfConquest::IsFinished() const {
  return alliance_reinforcements_ <= 0 || horde_reinforcements_ <= 0 ||
         !alliance_boss_alive_ || !horde_boss_alive_;
}

std::string BgIsleOfConquest::GetScoreText() const {
  return "Alliance " + std::to_string(alliance_reinforcements_) + " - " +
         std::to_string(horde_reinforcements_) + " Horde";
}

std::string BgIsleOfConquest::GetStatusText() const {
  std::string status = GetScoreText();

  status += "  Nodes: A=" + std::to_string(GetAllianceNodesControlled()) +
            " H=" + std::to_string(GetHordeNodesControlled());

  status += "  Boss: A=" + std::string(alliance_boss_alive_ ? "Alive" : "Dead") +
            " H=" + std::string(horde_boss_alive_ ? "Alive" : "Dead");

  if (IsFinished()) {
    int winner = GetWinner();
    status += (winner == 1) ? " [ALLIANCE WINS]" : " [HORDE WINS]";
  }
  return status;
}

int BgIsleOfConquest::GetWinner() const {
  if (horde_reinforcements_ <= 0 || !horde_boss_alive_) return 1;
  if (alliance_reinforcements_ <= 0 || !alliance_boss_alive_) return 2;
  return 0;
}

std::string_view BgIsleOfConquest::GetNodeName(IocNode node) {
  switch (node) {
    case IocNode::kDocks:         return "Docks";
    case IocNode::kHangar:        return "Hangar";
    case IocNode::kWorkshop:      return "Workshop";
    case IocNode::kQuarry:        return "Quarry";
    case IocNode::kRefinery:      return "Refinery";
    case IocNode::kAllianceKeep:  return "Alliance Keep";
    case IocNode::kHordeKeep:     return "Horde Keep";
    default:                      return "Unknown Node";
  }
}

std::string_view BgIsleOfConquest::GetGateName(IocGate gate) {
  switch (gate) {
    case IocGate::kHordeFront:    return "Horde Front Gate";
    case IocGate::kHordeWest:     return "Horde West Gate";
    case IocGate::kHordeEast:     return "Horde East Gate";
    case IocGate::kAllianceFront: return "Alliance Front Gate";
    case IocGate::kAllianceWest:  return "Alliance West Gate";
    case IocGate::kAllianceEast:  return "Alliance East Gate";
    default:                      return "Unknown Gate";
  }
}

void BgIsleOfConquest::Reset() {
  alliance_reinforcements_ = 0;
  horde_reinforcements_ = 0;
  nodes_.fill(IocNodeState::kUnknown);
  gates_.fill(IocGateState::kUnknown);
  alliance_boss_alive_ = true;
  horde_boss_alive_ = true;
}

}
