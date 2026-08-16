#pragma once

#include <cstdint>
#include <deque>
#include <mutex>

namespace openwow::game {

inline constexpr std::uint32_t kRecentCastWindowMs = 1500;

class RecentCastTracker {
 public:

  static RecentCastTracker& GetPlayerTracker();

  static RecentCastTracker& GetPetTracker();

  void RecordCast(std::uint32_t spell_id, std::uint32_t context_id,
                  std::uint32_t tick_ms);

  [[nodiscard]] bool Contains(std::uint32_t spell_id,
                              std::uint32_t context_id,
                              std::uint32_t current_tick_ms) const;

  void ExpireEntries(std::uint32_t current_tick_ms);

  void Clear();

 private:
  void ExpireEntriesUnlocked(std::uint32_t current_tick_ms);

  struct Entry {
    std::uint32_t spell_id;
    std::uint32_t context_id;
    std::uint32_t tick_ms;
  };

  mutable std::mutex mutex_;
  std::deque<Entry> entries_;
};

}
