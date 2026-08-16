
#include "openwow/game/motd_system.h"

#include <algorithm>

namespace openwow::game {

void MOTDSystem::SetMOTD(const std::string& text) {
    motd_ = text;
}

const std::string& MOTDSystem::GetMOTD() const {
    return motd_;
}

bool MOTDSystem::HasMOTD() const {
    return !motd_.empty();
}

void MOTDSystem::ClearMOTD() {
    motd_.clear();
}

void MOTDSystem::SetGuildMOTD(const std::string& text) {
    guild_motd_ = text;
}

const std::string& MOTDSystem::GetGuildMOTD() const {
    return guild_motd_;
}

bool MOTDSystem::HasGuildMOTD() const {
    return !guild_motd_.empty();
}

void MOTDSystem::ClearGuildMOTD() {
    guild_motd_.clear();
}

void MOTDSystem::AddLoginMessage(const std::string& text) {
    login_messages_.push_back(text);
}

const std::vector<std::string>& MOTDSystem::GetLoginMessages() const {
    return login_messages_;
}

std::uint32_t MOTDSystem::GetLoginMessageCount() const {
    return static_cast<std::uint32_t>(login_messages_.size());
}

void MOTDSystem::ClearLoginMessages() {
    login_messages_.clear();
}

void MOTDSystem::SetDisplayed(bool displayed) {
    displayed_ = displayed;
}

bool MOTDSystem::IsDisplayed() const {
    return displayed_;
}

std::vector<std::string> MOTDSystem::FormatMOTD() const {
    std::vector<std::string> lines;
    if (motd_.empty()) return lines;

    std::string::size_type start = 0;
    while (start <= motd_.size()) {
        auto pos = motd_.find('|', start);
        if (pos == std::string::npos) {
            lines.push_back(motd_.substr(start));
            break;
        }
        lines.push_back(motd_.substr(start, pos - start));
        start = pos + 1;
        if (start == motd_.size()) {
            lines.emplace_back();
            break;
        }
    }
    return lines;
}

void MOTDSystem::Reset() {
    motd_.clear();
    guild_motd_.clear();
    login_messages_.clear();
    displayed_ = false;
}

}
