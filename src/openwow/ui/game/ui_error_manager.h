
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::ui {

enum class UIMessageType {
  Error,
  Info,
  Emote,
  RaidWarning,
  BossEmote,
  ZoneText,
  SubZoneText,
};

struct UIMessage {
  UIMessageType type = UIMessageType::Error;
  std::string text;
  float lifetime = 0;
  float max_lifetime = 3.0f;
  float alpha = 1.0f;
  std::uint32_t color = 0xFFFF0000;
  bool is_fading = false;
  float fade_start = 2.5f;
  float font_size = 12.0f;
  bool hold = false;
};

class UIErrorManager {
 public:
  static UIErrorManager& Get();

  void AddErrorMessage(const std::string& text);
  void AddInfoMessage(const std::string& text);
  void AddRaidWarning(const std::string& text);
  void AddBossEmote(const std::string& text);
  void AddZoneText(const std::string& zone, const std::string& sub_zone);

  void SetSuppressDuration(float seconds);
  float GetSuppressDuration() const;

  void Update(float delta_time);

  const std::vector<UIMessage>& GetActiveMessages() const;
  const UIMessage* GetCurrentError() const;
  const UIMessage* GetCurrentInfo() const;
  const UIMessage* GetCurrentRaidWarning() const;
  const UIMessage* GetCurrentZoneText() const;

  const std::vector<std::string>& GetErrorHistory() const;

  void SetErrorsEnabled(bool enabled);
  bool AreErrorsEnabled() const;

  void Clear();
  void Reset();

 private:
  UIErrorManager() = default;

  void AddMessage(UIMessageType type, const std::string& text,
                  std::uint32_t color, float max_lifetime, float fade_start,
                  float font_size);

  bool IsSuppressed(const std::string& text) const;

  const UIMessage* FindByType(UIMessageType type) const;

  std::vector<UIMessage> active_;
  std::vector<std::string> error_history_;
  float suppress_duration_ = 1.0f;
  std::unordered_map<std::string, float> suppress_map_;
  float current_time_ = 0;
  bool errors_enabled_ = true;
  mutable std::mutex mutex_;

  static constexpr std::size_t kMaxErrorHistory = 20;
};

}
