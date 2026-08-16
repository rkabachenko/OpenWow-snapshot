
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace openwow::game {

struct MOTDInfo {
  std::string   message;
  std::string   serverName;
  std::uint32_t timestamp{0};
  bool          isGuildMOTD{false};
};

class MOTDDisplay {
 public:
  MOTDDisplay() = default;

  void SetServerMOTD(const std::string& message,
                     const std::string& serverName);
  void SetGuildMOTD(const std::string& message);

  [[nodiscard]] const std::string& GetServerMOTD() const;
  [[nodiscard]] const std::string& GetGuildMOTD() const;
  [[nodiscard]] const std::string& GetServerName() const;
  [[nodiscard]] bool HasServerMOTD() const;
  [[nodiscard]] bool HasGuildMOTD() const;

  [[nodiscard]] std::string GetDisplayText() const;
  [[nodiscard]] std::string GetGuildDisplayText() const;

  void SetDismissed(bool dismissed);
  [[nodiscard]] bool IsDismissed() const;

  [[nodiscard]] std::vector<MOTDInfo> GetAll() const;
  void Clear();

  [[nodiscard]] std::uint32_t GetTimestamp() const;

  void SetAutoShow(bool autoShow);
  [[nodiscard]] bool IsAutoShow() const;

  [[nodiscard]] std::vector<std::string> SplitServerMOTDLines() const;

  [[nodiscard]] std::uint32_t GetWordCount() const;

  [[nodiscard]] std::string GetPreview(std::uint32_t maxChars = 60) const;

  [[nodiscard]] bool HasAnyMOTD() const;
 private:
  MOTDInfo    serverMotd_{};
  MOTDInfo    guildMotd_{};
  bool        dismissed_{false};
  bool        autoShow_{true};
};

}
