
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace openwow::game {

class MOTDSystem {
 public:
  MOTDSystem() = default;

  void SetMOTD(const std::string& text);
  [[nodiscard]] const std::string& GetMOTD() const;
  [[nodiscard]] bool HasMOTD() const;
  void ClearMOTD();

  void SetGuildMOTD(const std::string& text);
  [[nodiscard]] const std::string& GetGuildMOTD() const;
  [[nodiscard]] bool HasGuildMOTD() const;
  void ClearGuildMOTD();

  void AddLoginMessage(const std::string& text);
  [[nodiscard]] const std::vector<std::string>& GetLoginMessages() const;
  [[nodiscard]] std::uint32_t GetLoginMessageCount() const;
  void ClearLoginMessages();

  void SetDisplayed(bool displayed);
  [[nodiscard]] bool IsDisplayed() const;

  [[nodiscard]] std::vector<std::string> FormatMOTD() const;

  void Reset();

 private:
  std::string motd_;
  std::string guild_motd_;
  std::vector<std::string> login_messages_;
  bool displayed_{false};
};

}
