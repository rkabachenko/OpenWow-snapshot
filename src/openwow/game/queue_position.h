#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

namespace openwow::game {

struct QueuePositionSnapshot {
  bool active = false;
  std::uint32_t position = 0;
  bool free_character_migration = false;
  bool has_account_info = false;
  std::uint32_t billing_time = 0;
  std::uint8_t billing_flags = 0;
  std::uint32_t billing_rested = 0;
  std::uint8_t expansion_level = 0;
  std::string realm_name;
  std::array<std::uint32_t, 8> sampled_positions{};
  std::array<std::uint32_t, 8> sample_ticks_ms{};
  bool has_estimated_wait = false;
  std::int32_t estimated_wait_ms = 0;
  std::int32_t remaining_wait_ms = 0;
};

class QueuePositionTracker {
 public:
  static constexpr std::size_t kSampleCount = 8;

  void Reset();
  void Reset(std::string realm_name);
  void SetRealmName(std::string realm_name);

  void RecordQueuePosition(std::uint32_t position,
                           bool free_character_migration,
                           std::uint32_t now_tick_ms);
  void RecordAccountInfo(std::uint32_t billing_time,
                         std::uint8_t billing_flags,
                         std::uint32_t billing_rested,
                         std::uint8_t expansion_level);

  [[nodiscard]] QueuePositionSnapshot Snapshot(
      std::uint32_t now_tick_ms) const;

 private:
  mutable std::mutex mutex_;
  std::array<std::uint32_t, kSampleCount> sampled_positions_{};
  std::array<std::uint32_t, kSampleCount> sample_ticks_ms_{};
  bool active_{false};
  bool free_character_migration_{false};
  bool has_account_info_{false};
  std::uint32_t billing_time_{0};
  std::uint8_t billing_flags_{0};
  std::uint32_t billing_rested_{0};
  std::uint8_t expansion_level_{0};
  std::string realm_name_;
  std::int32_t estimated_wait_ms_{0};
};

}
