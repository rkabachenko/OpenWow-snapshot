
#include "openwow/game/motd_display.h"

#include <cctype>
#include <sstream>

namespace openwow::game {

void MOTDDisplay::SetServerMOTD(const std::string& message,
                                const std::string& serverName) {
  serverMotd_.message    = message;
  serverMotd_.serverName = serverName;
  serverMotd_.isGuildMOTD = false;
  serverMotd_.timestamp++;
  dismissed_ = false;
}

void MOTDDisplay::SetGuildMOTD(const std::string& message) {
  guildMotd_.message     = message;
  guildMotd_.isGuildMOTD = true;
  guildMotd_.timestamp++;
}

const std::string& MOTDDisplay::GetServerMOTD() const {
  return serverMotd_.message;
}

const std::string& MOTDDisplay::GetGuildMOTD() const {
  return guildMotd_.message;
}

const std::string& MOTDDisplay::GetServerName() const {
  return serverMotd_.serverName;
}

bool MOTDDisplay::HasServerMOTD() const {
  return !serverMotd_.message.empty();
}

bool MOTDDisplay::HasGuildMOTD() const {
  return !guildMotd_.message.empty();
}

std::string MOTDDisplay::GetDisplayText() const {
  if (serverMotd_.message.empty()) return {};
  return "[" + serverMotd_.serverName + "] MOTD: " + serverMotd_.message;
}

std::string MOTDDisplay::GetGuildDisplayText() const {
  if (guildMotd_.message.empty()) return {};
  return "Guild Message of the Day: " + guildMotd_.message;
}

void MOTDDisplay::SetDismissed(bool dismissed) {
  dismissed_ = dismissed;
}

bool MOTDDisplay::IsDismissed() const {
  return dismissed_;
}

std::vector<MOTDInfo> MOTDDisplay::GetAll() const {
  std::vector<MOTDInfo> result;
  if (!serverMotd_.message.empty()) result.push_back(serverMotd_);
  if (!guildMotd_.message.empty()) result.push_back(guildMotd_);
  return result;
}

void MOTDDisplay::Clear() {
  serverMotd_ = {};
  guildMotd_  = {};
  dismissed_  = false;
}

std::uint32_t MOTDDisplay::GetTimestamp() const {
  return serverMotd_.timestamp;
}

void MOTDDisplay::SetAutoShow(bool autoShow) {
  autoShow_ = autoShow;
}

bool MOTDDisplay::IsAutoShow() const {
  return autoShow_;
}

std::vector<std::string> MOTDDisplay::SplitServerMOTDLines() const {
  std::vector<std::string> lines;
  if (serverMotd_.message.empty()) return lines;

  std::string current;
  for (char c : serverMotd_.message) {
    if (c == '|') {
      lines.push_back(current);
      current.clear();
    } else {
      current += c;
    }
  }
  if (!current.empty()) {
    lines.push_back(current);
  }
  return lines;
}

std::uint32_t MOTDDisplay::GetWordCount() const {
  if (serverMotd_.message.empty()) return 0;

  std::uint32_t count = 0;
  bool in_word = false;
  for (char c : serverMotd_.message) {
    if (std::isspace(static_cast<unsigned char>(c)) || c == '|') {
      if (in_word) {
        ++count;
        in_word = false;
      }
    } else {
      in_word = true;
    }
  }
  if (in_word) ++count;
  return count;
}

std::string MOTDDisplay::GetPreview(std::uint32_t maxChars) const {
  if (serverMotd_.message.empty()) return {};
  if (serverMotd_.message.size() <= maxChars) return serverMotd_.message;
  return serverMotd_.message.substr(0, maxChars) + "...";
}

bool MOTDDisplay::HasAnyMOTD() const {
  return !serverMotd_.message.empty() || !guildMotd_.message.empty();
}

}
