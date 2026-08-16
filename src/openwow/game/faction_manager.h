
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "openwow/game/packet_reader.h"

namespace openwow::game {

inline constexpr std::uint32_t kMaxFactionSlots = 128;

enum class ReputationRank : std::uint8_t {
  Hated      = 0,
  Hostile    = 1,
  Unfriendly = 2,
  Neutral    = 3,
  Friendly   = 4,
  Honored    = 5,
  Revered    = 6,
  Exalted    = 7,
};

inline constexpr std::array<std::int32_t, 8> kRepThresholds = {
    -42000, -6000, -3000, 0, 3000, 9000, 21000, 42000,
};

inline constexpr std::array<std::int32_t, 8> kRepBarWidths = {
    36000, 3000, 3000, 3000, 6000, 12000, 21000, 999,
};

struct FactionEntry {
  std::uint8_t flags = 0;
  std::int32_t standing = 0;
};

struct FactionStandingUpdate {
  std::uint32_t list_id = 0;
  std::int32_t standing = 0;
};

struct FactionStandingNotification {
  float bonus_rep = 0.0f;
  bool increased = false;
  std::vector<FactionStandingUpdate> updates;
};

class FactionManager {
 public:

  bool HandleInitializeFactions(const std::uint8_t* data, std::size_t len);
  bool HandleSetFactionStanding(const std::uint8_t* data, std::size_t len);
  bool HandleSetFactionVisible(const std::uint8_t* data, std::size_t len);
  bool HandleSetFactionAtWar(const std::uint8_t* data, std::size_t len);

  [[nodiscard]] const FactionEntry& GetFaction(std::uint32_t slot) const;
  [[nodiscard]] const FactionStandingNotification& last_standing_update() const {
    return last_standing_;
  }
  [[nodiscard]] std::uint32_t last_visible_faction() const {
    return last_visible_;
  }

  [[nodiscard]] static ReputationRank GetRankFromStanding(std::int32_t standing);
  [[nodiscard]] static std::string GetRankName(ReputationRank rank);
  [[nodiscard]] static std::int32_t GetBarMin(ReputationRank rank);
  [[nodiscard]] static std::int32_t GetBarMax(ReputationRank rank);
  [[nodiscard]] static float GetBarProgress(std::int32_t standing);
  [[nodiscard]] ReputationRank GetFactionRank(std::uint32_t slot) const;
  [[nodiscard]] std::string GetFactionRankName(std::uint32_t slot) const;
  [[nodiscard]] float GetFactionBarProgress(std::uint32_t slot) const;

  void SetWatchedFaction(std::uint32_t slot);
  [[nodiscard]] std::uint32_t GetWatchedFaction() const;

  [[nodiscard]] bool IsAtWar(std::uint32_t slot) const;
  void SetAtWar(std::uint32_t slot, bool atWar);
  [[nodiscard]] bool IsVisible(std::uint32_t slot) const;

  void Clear();

 private:
  static const FactionEntry kEmpty;
  std::array<FactionEntry, kMaxFactionSlots> factions_{};
  FactionStandingNotification last_standing_{};
  std::uint32_t last_visible_ = 0;
  std::uint32_t watched_faction_ = 0;
};

}
