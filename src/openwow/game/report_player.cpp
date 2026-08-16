
#include "openwow/game/report_player.h"

#include <algorithm>

namespace openwow::game {

bool ReportPlayerSystem::ReportPlayer(ObjectGuid guid, const std::string& name,
                                      PlayerReportType type,
                                      const std::string& comment) {
  std::lock_guard lock(mutex_);

  if (cooldown_remaining_ > 0.0f) return false;

  for (const auto& r : reports_) {
    if (r.reportedGuid.GetRawValue() == guid.GetRawValue()) return false;
  }

  PlayerReportEntry entry;
  entry.reportedGuid = guid;
  entry.reportedName = name;
  entry.type = type;

  entry.comment = comment.substr(0, GetMaxCommentLength());
  entry.timestamp = elapsed_time_;
  reports_.push_back(std::move(entry));
  cooldown_remaining_ = kCooldownDuration;
  return true;
}

bool ReportPlayerSystem::HasReported(ObjectGuid guid) const {
  std::lock_guard lock(mutex_);
  for (const auto& r : reports_) {
    if (r.reportedGuid.GetRawValue() == guid.GetRawValue()) return true;
  }
  return false;
}

std::vector<PlayerReportEntry> ReportPlayerSystem::GetReports() const {
  std::lock_guard lock(mutex_);
  return reports_;
}

std::uint32_t ReportPlayerSystem::GetReportCount() const {
  std::lock_guard lock(mutex_);
  return static_cast<std::uint32_t>(reports_.size());
}

bool ReportPlayerSystem::IsOnCooldown() const {
  std::lock_guard lock(mutex_);
  return cooldown_remaining_ > 0.0f;
}

float ReportPlayerSystem::GetCooldownRemaining() const {
  std::lock_guard lock(mutex_);
  return cooldown_remaining_;
}

std::string ReportPlayerSystem::GetReportTypeName(PlayerReportType type) {
  switch (type) {
    case PlayerReportType::kSpam:     return "Spam";
    case PlayerReportType::kLanguage: return "Language";
    case PlayerReportType::kName:     return "Name";
    case PlayerReportType::kCheating: return "Cheating";
    case PlayerReportType::kBotting:  return "Botting";
    case PlayerReportType::kAFK_BG:   return "AFK in Battleground";
  }
  return "Unknown";
}

bool ReportPlayerSystem::CanReport(ObjectGuid guid) const {
  std::lock_guard lock(mutex_);
  if (cooldown_remaining_ > 0.0f) return false;
  for (const auto& r : reports_) {
    if (r.reportedGuid.GetRawValue() == guid.GetRawValue()) return false;
  }
  return true;
}

void ReportPlayerSystem::Update(float dt) {
  std::lock_guard lock(mutex_);
  elapsed_time_ += dt;
  if (cooldown_remaining_ > 0.0f) {
    cooldown_remaining_ -= dt;
    if (cooldown_remaining_ < 0.0f) cooldown_remaining_ = 0.0f;
  }
}

void ReportPlayerSystem::Reset() {
  std::lock_guard lock(mutex_);
  reports_.clear();
  cooldown_remaining_ = 0.0f;
  elapsed_time_ = 0.0f;
}

}
