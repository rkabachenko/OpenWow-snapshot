
#pragma once

#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>

#include "openwow/game/object_guid.h"

namespace openwow::game {

enum class DuelState : std::uint8_t {
  None       = 0,
  Challenged = 1,
  Pending    = 2,
  Countdown  = 3,
  InProgress = 4,
  Complete   = 5,
};

enum class DuelResult : std::uint8_t {
  None     = 0,
  Won      = 1,
  Lost     = 2,
  Fled     = 3,
};

enum class DuelWinType : std::uint8_t {
  Knockout = 0,
  Retreat  = 1,
};

struct DuelHealthSnapshot {
  std::uint32_t health     = 0;
  std::uint32_t max_health = 0;
  std::uint32_t mana       = 0;
  std::uint32_t max_mana   = 0;
};

class DuelSystem {
 public:

  void Challenge(ObjectGuid targetGuid, const std::string& targetName);

  void ReceiveChallenge(ObjectGuid challengerGuid,
                        const std::string& challengerName,
                        ObjectGuid flagGuid);

  void ReceiveChallenge(ObjectGuid challengerGuid,
                        const std::string& challengerName);

  void Accept();

  void Decline();

  void Cancel();

  void StartCountdown(std::uint32_t countdown_ms);

  void NotifyOutOfBounds();

  void NotifyInBounds();

  void CompleteDuel(bool canceled);

  void SetWinner(DuelWinType winType,
                 const std::string& winnerName,
                 const std::string& loserName);

  [[nodiscard]] DuelState GetState() const { return state_; }

  [[nodiscard]] bool IsIdle() const { return state_ == DuelState::None; }

  [[nodiscard]] bool IsInDuel() const {
    return state_ == DuelState::Countdown ||
           state_ == DuelState::InProgress;
  }

  [[nodiscard]] bool IsInActiveCombat() const {
    return state_ == DuelState::InProgress;
  }

  [[nodiscard]] bool IsCountdownActive() const {
    return state_ == DuelState::Countdown;
  }

  [[nodiscard]] ObjectGuid GetOpponent() const { return opponent_guid_; }
  [[nodiscard]] const std::string& GetOpponentName() const {
    return opponent_name_;
  }
  [[nodiscard]] bool IsChallenger() const { return is_challenger_; }

  [[nodiscard]] DuelResult GetResult() const { return result_; }
  [[nodiscard]] DuelWinType GetWinType() const { return win_type_; }
  [[nodiscard]] const std::string& GetWinnerName() const { return winner_name_; }
  [[nodiscard]] const std::string& GetLoserName() const { return loser_name_; }

  [[nodiscard]] bool IsDuelTarget(ObjectGuid guid) const {
    return IsInDuel() && guid == opponent_guid_ && !guid.IsEmpty();
  }

  [[nodiscard]] float GetCountdown() const { return countdown_; }

  [[nodiscard]] int GetCountdownPhase() const;

  [[nodiscard]] std::string GetCountdownText() const;

  void EndDuel(DuelResult result);

  [[nodiscard]] std::pair<float, float> GetDuelBoundaryCenter() const {
    return {boundary_x_, boundary_y_};
  }

  void SetBoundaryCenter(float x, float y, float z = 0.0f) {
    boundary_x_ = x;
    boundary_y_ = y;
    boundary_z_ = z;
  }

  [[nodiscard]] float GetBoundaryRadius() const { return kBoundaryRadius; }

  [[nodiscard]] bool IsOutOfBounds(float playerX, float playerY) const;

  [[nodiscard]] float GetDistanceSqFromFlag(float px, float py) const;

  [[nodiscard]] bool IsOutOfBoundsWarningActive() const { return oob_active_; }

  [[nodiscard]] float GetOutOfBoundsTimer() const { return oob_timer_; }
  void SetOutOfBoundsTimer(float t) { oob_timer_ = t; }

  void StartOutOfBoundsTimer() { oob_timer_ = kOOBForfeitTime; }

  [[nodiscard]] bool ShouldAutoForfeit() const {
    return state_ == DuelState::InProgress && oob_timer_expired_;
  }

  [[nodiscard]] std::string GetOOBWarningText() const;

  void SetFlagGuid(ObjectGuid guid) { flag_guid_ = guid; }
  [[nodiscard]] ObjectGuid GetFlagGuid() const { return flag_guid_; }

  void SaveHealthSnapshot(std::uint32_t health, std::uint32_t maxHealth,
                          std::uint32_t mana, std::uint32_t maxMana);
  [[nodiscard]] const DuelHealthSnapshot& GetHealthSnapshot() const {
    return health_snapshot_;
  }
  [[nodiscard]] bool HasHealthSnapshot() const {
    return health_snapshot_.max_health > 0;
  }

  [[nodiscard]] std::string GetVictoryMessage() const;

  [[nodiscard]] std::string GetWinnerEventName() const;

  [[nodiscard]] std::string GetWinnerAnnouncement() const;

  static constexpr std::uint32_t kSoundDuelCountdown = 3288;
  static constexpr std::uint32_t kSoundDuelStart     = 3289;
  static constexpr std::uint32_t kSoundDuelEnd       = 8457;

  static constexpr std::uint32_t kDuelRequestSpellId = 7266;

  void Update(float dt);

  using Callback = std::function<void()>;
  using CountdownMessageCallback = std::function<void(const std::string&)>;

  void SetOnCountdownTick(Callback cb) { on_countdown_tick_ = std::move(cb); }
  void SetOnCountdownFinished(Callback cb) { on_countdown_finished_ = std::move(cb); }
  void SetOnOutOfBounds(Callback cb) { on_out_of_bounds_ = std::move(cb); }
  void SetOnInBounds(Callback cb) { on_in_bounds_ = std::move(cb); }
  void SetOnDuelComplete(Callback cb) { on_duel_complete_ = std::move(cb); }
  void SetOnForfeit(Callback cb) { on_forfeit_ = std::move(cb); }
  void SetOnCountdownMessage(CountdownMessageCallback cb) {
    on_countdown_message_ = std::move(cb);
  }

  void Reset();

  struct CountdownTickResult {
    std::string message;
    bool schedule_next = false;
  };
  [[nodiscard]] CountdownTickResult FormatCountdownTick();

  void ResetCountdownState();

  struct DuelCompleteResult {
    bool was_in_duel = false;
    bool canceled = false;
    bool show_system_msg = false;
  };
  [[nodiscard]] DuelCompleteResult HandleDuelCompletePacket(
      const uint8_t* data, size_t len);

  static constexpr float kBoundaryRadius   = 40.0f;
  static constexpr float kDefaultCountdown = 3.0f;
  static constexpr float kOOBForfeitTime   = 10.0f;

 private:
  void CancelCountdownMessageSchedule();
  void EmitScheduledCountdownMessages(float dt);
  void DispatchCountdownMessageTick();

  DuelState  state_{DuelState::None};
  DuelResult result_{DuelResult::None};
  DuelWinType win_type_{DuelWinType::Knockout};

  ObjectGuid opponent_guid_;
  std::string opponent_name_;
  bool is_challenger_{false};

  float countdown_{0.0f};
  int   countdown_integer_{0};

  int   last_countdown_phase_{-1};
  float countdown_message_elapsed_{0.0f};
  bool  countdown_message_scheduled_{false};

  float boundary_x_{0.0f};
  float boundary_y_{0.0f};
  float boundary_z_{0.0f};

  bool  oob_active_{false};
  float oob_timer_{0.0f};
  bool  oob_timer_expired_{false};

  ObjectGuid flag_guid_;

  DuelHealthSnapshot health_snapshot_{};

  std::string winner_name_;
  std::string loser_name_;

  Callback on_countdown_tick_;
  Callback on_countdown_finished_;
  Callback on_out_of_bounds_;
  Callback on_in_bounds_;
  Callback on_duel_complete_;
  Callback on_forfeit_;
  CountdownMessageCallback on_countdown_message_;
};

}
