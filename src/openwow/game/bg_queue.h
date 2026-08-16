
#pragma once

#include <array>
#include <cstdint>
#include <mutex>

namespace openwow::game {

enum class BattlegroundType : std::uint32_t {
  kAlteracValley = 1,
  kWarsongGulch = 2,
  kArathiBasin = 3,
  kEyeOfTheStorm = 7,
  kStrandOfTheAncients = 9,
  kIsleOfConquest = 30,
  kRandomBattleground = 32,
};

struct BGQueueInfo {
  BattlegroundType bg_type = BattlegroundType::kWarsongGulch;
  std::uint32_t instance_id = 0;

  enum Status : std::uint32_t {
    kNone = 0,
    kQueued = 1,
    kWaitJoin = 2,
    kInProgress = 3,
  };
  Status status = kNone;

  std::uint32_t estimated_wait = 0;
  std::uint32_t time_in_queue = 0;

  float join_timer = 0;
  std::uint32_t map_id = 0;

  std::int32_t ally_score = 0;
  std::int32_t horde_score = 0;
};

class BGQueue {
 public:
  static BGQueue& Get();

  BGQueue(const BGQueue&) = delete;
  BGQueue& operator=(const BGQueue&) = delete;

  static constexpr std::uint32_t kMaxQueues = 3;

  void SetQueueStatus(std::uint32_t queue_slot, const BGQueueInfo& info);
  void ClearQueue(std::uint32_t queue_slot);
  [[nodiscard]] const BGQueueInfo& GetQueueInfo(std::uint32_t slot) const;

  [[nodiscard]] bool IsQueued() const;
  [[nodiscard]] std::uint32_t GetActiveQueueCount() const;

  [[nodiscard]] bool HasReadyQueue() const;
  [[nodiscard]] std::uint32_t GetReadyQueueSlot() const;

  void AcceptBG(std::uint32_t slot);
  void DeclineBG(std::uint32_t slot);

  void SetInBattleground(bool in_bg,
                          BattlegroundType type = BattlegroundType::kWarsongGulch);
  [[nodiscard]] bool IsInBattleground() const;
  [[nodiscard]] BattlegroundType GetCurrentBGType() const;

  void UpdateScores(std::int32_t ally, std::int32_t horde);
  [[nodiscard]] std::int32_t GetAllyScore() const;
  [[nodiscard]] std::int32_t GetHordeScore() const;

  void SetBGTimer(float remaining);
  [[nodiscard]] float GetBGTimer() const;

  void LeaveBattleground();

  void SetWintergraspTimer(std::uint32_t seconds);
  [[nodiscard]] std::uint32_t GetWintergraspTimer() const;
  [[nodiscard]] bool IsWintergraspInProgress() const;

  void Reset();

 private:
  BGQueue() = default;

  std::array<BGQueueInfo, kMaxQueues> queues_{};
  bool in_bg_ = false;
  BattlegroundType current_bg_type_ = BattlegroundType::kWarsongGulch;
  float bg_timer_ = 0;
  std::uint32_t wintergrasp_timer_ = 0;

  static const BGQueueInfo kEmptyInfo;

  mutable std::mutex mutex_;
};

}
