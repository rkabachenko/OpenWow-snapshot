#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "openwow/game/object_guid.h"

namespace openwow::game {

enum class PlayerReportType : std::uint8_t {
  kSpam     = 0,
  kLanguage = 1,
  kName     = 2,
  kCheating = 3,
  kBotting  = 4,
  kAFK_BG   = 5,
};

struct PlayerReportEntry {
  ObjectGuid reportedGuid;
  std::string reportedName;
  PlayerReportType type{PlayerReportType::kSpam};
  std::string comment;
  float timestamp{0.0f};
};

class ReportPlayerSystem {
 public:

  bool ReportPlayer(ObjectGuid guid, const std::string& name,
                    PlayerReportType type, const std::string& comment);

  [[nodiscard]] bool HasReported(ObjectGuid guid) const;

  [[nodiscard]] std::vector<PlayerReportEntry> GetReports() const;

  [[nodiscard]] std::uint32_t GetReportCount() const;

  [[nodiscard]] bool IsOnCooldown() const;

  [[nodiscard]] float GetCooldownRemaining() const;

  [[nodiscard]] static std::string GetReportTypeName(PlayerReportType type);

  [[nodiscard]] static constexpr std::uint32_t GetMaxCommentLength() {
    return 256;
  }

  [[nodiscard]] bool CanReport(ObjectGuid guid) const;

  void Update(float dt);

  void Reset();

 private:
  static constexpr float kCooldownDuration = 60.0f;

  mutable std::mutex mutex_;
  std::vector<PlayerReportEntry> reports_;
  float cooldown_remaining_{0.0f};
  float elapsed_time_{0.0f};
};

}
