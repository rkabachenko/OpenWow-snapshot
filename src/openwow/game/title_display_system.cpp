
#include "openwow/game/title_display_system.h"

#include <cctype>

namespace openwow::game {

namespace {

bool CaseInsensitiveContains(const std::string& haystack,
                             const std::string& needle) {
    if (needle.empty()) return true;
    if (needle.size() > haystack.size()) return false;
    auto it = std::search(
        haystack.begin(), haystack.end(),
        needle.begin(), needle.end(),
        [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) ==
                   std::tolower(static_cast<unsigned char>(b));
        });
    return it != haystack.end();
}

std::string SubstitutePercS(const std::string& fmt,
                            const std::string& value) {
    auto pos = fmt.find("%s");
    if (pos == std::string::npos) return fmt;
    std::string out = fmt;
    out.replace(pos, 2, value);
    return out;
}

}

void TitleDisplaySystem::SetAvailableTitles(
    const std::vector<TitleEntryInfo>& titles) {
    titles_ = titles;
}

const std::vector<TitleEntryInfo>& TitleDisplaySystem::GetAvailableTitles()
    const {
    return titles_;
}

std::vector<TitleEntryInfo> TitleDisplaySystem::GetEarnedTitles() const {
    std::vector<TitleEntryInfo> out;
    for (const auto& t : titles_) {
        if (t.isEarned) out.push_back(t);
    }
    return out;
}

uint32_t TitleDisplaySystem::GetEarnedCount() const {
    uint32_t n = 0;
    for (const auto& t : titles_) {
        if (t.isEarned) ++n;
    }
    return n;
}

uint32_t TitleDisplaySystem::GetTotalCount() const {
    return static_cast<uint32_t>(titles_.size());
}

void TitleDisplaySystem::SetActiveTitle(uint32_t titleId) {
    for (const auto& t : titles_) {
        if (t.titleId == titleId) {
            activeTitleId_ = titleId;
            return;
        }
    }
}

void TitleDisplaySystem::ClearActiveTitle() { activeTitleId_.reset(); }

std::optional<uint32_t> TitleDisplaySystem::GetActiveTitleId() const {
    return activeTitleId_;
}

std::optional<TitleEntryInfo> TitleDisplaySystem::GetActiveTitle() const {
    if (!activeTitleId_) return std::nullopt;
    for (const auto& t : titles_) {
        if (t.titleId == *activeTitleId_) return t;
    }
    return std::nullopt;
}

std::string TitleDisplaySystem::FormatPlayerName(
    const std::string& playerName) const {
    auto active = GetActiveTitle();
    if (!active) return playerName;
    return SubstitutePercS(active->name, playerName);
}

bool TitleDisplaySystem::HasTitle(uint32_t titleId) const {
    for (const auto& t : titles_) {
        if (t.titleId == titleId && t.isEarned) return true;
    }
    return false;
}

void TitleDisplaySystem::AddTitle(const TitleEntryInfo& info) {
    for (auto& t : titles_) {
        if (t.titleId == info.titleId) {
            t.isEarned = true;
            return;
        }
    }
    titles_.push_back(info);
}

std::vector<TitleEntryInfo> TitleDisplaySystem::GetPrefixTitles() const {
    std::vector<TitleEntryInfo> out;
    for (const auto& t : titles_) {
        if (t.position == TitlePositionType::Prefix) out.push_back(t);
    }
    return out;
}

std::vector<TitleEntryInfo> TitleDisplaySystem::GetSuffixTitles() const {
    std::vector<TitleEntryInfo> out;
    for (const auto& t : titles_) {
        if (t.position == TitlePositionType::Suffix) out.push_back(t);
    }
    return out;
}

std::vector<TitleEntryInfo> TitleDisplaySystem::Search(
    const std::string& query) const {
    std::vector<TitleEntryInfo> out;
    for (const auto& t : titles_) {
        if (CaseInsensitiveContains(t.name, query)) out.push_back(t);
    }
    return out;
}

void TitleDisplaySystem::Open()  { open_ = true; }
void TitleDisplaySystem::Close() { open_ = false; }
bool TitleDisplaySystem::IsOpen() const { return open_; }

void TitleDisplaySystem::Clear() {
    titles_.clear();
    activeTitleId_.reset();
    open_ = false;
}

}
