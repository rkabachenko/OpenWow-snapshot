
#include "openwow/game/title_system.h"

namespace openwow::game {

TitleSystem& TitleSystem::Get() {
    static TitleSystem instance;
    return instance;
}

void TitleSystem::AddTitle(uint32_t titleId, const std::string& maleName,
                           const std::string& femaleName,
                           uint32_t maskIndex) {
    std::lock_guard lock(mutex_);
    TitleEntry e;
    e.title_id   = titleId;
    e.name_male  = maleName;
    e.name_female = femaleName;
    e.mask_index = maskIndex;
    title_db_[titleId] = std::move(e);
}

std::optional<TitleEntry> TitleSystem::GetTitleEntry(uint32_t titleId) const {
    std::lock_guard lock(mutex_);
    auto it = title_db_.find(titleId);
    if (it == title_db_.end()) return std::nullopt;
    return it->second;
}

std::optional<TitleEntry> TitleSystem::GetTitleEntryByMaskIndex(uint32_t maskIndex) const {
    std::lock_guard lock(mutex_);
    for (const auto& [id, entry] : title_db_) {
        if (entry.mask_index == maskIndex) {
            return entry;
        }
    }
    return std::nullopt;
}

void TitleSystem::SetKnownTitles(const std::vector<uint64_t>& bitfield) {
    std::lock_guard lock(mutex_);
    known_bits_ = bitfield;
}

bool TitleSystem::HasTitle(uint32_t titleId) const {
    std::lock_guard lock(mutex_);
    auto it = title_db_.find(titleId);
    if (it == title_db_.end()) return false;

    uint32_t idx = it->second.mask_index;
    uint32_t qword = idx / 64;
    uint32_t bit   = idx % 64;
    if (qword >= known_bits_.size()) return false;
    return (known_bits_[qword] & (uint64_t{1} << bit)) != 0;
}

std::vector<uint32_t> TitleSystem::GetKnownTitles() const {
    std::lock_guard lock(mutex_);
    std::vector<uint32_t> result;
    for (const auto& [id, entry] : title_db_) {
        uint32_t idx = entry.mask_index;
        uint32_t qword = idx / 64;
        uint32_t bit   = idx % 64;
        if (qword < known_bits_.size() &&
            (known_bits_[qword] & (uint64_t{1} << bit)) != 0) {
            result.push_back(id);
        }
    }
    return result;
}

uint32_t TitleSystem::GetKnownTitleCount() const {
    std::lock_guard lock(mutex_);
    uint32_t count = 0;
    for (const auto& [id, entry] : title_db_) {
        uint32_t idx = entry.mask_index;
        uint32_t qword = idx / 64;
        uint32_t bit   = idx % 64;
        if (qword < known_bits_.size() &&
            (known_bits_[qword] & (uint64_t{1} << bit)) != 0) {
            ++count;
        }
    }
    return count;
}

void TitleSystem::SetCurrentTitle(uint32_t titleId) {
    std::lock_guard lock(mutex_);
    current_title_ = titleId;
}

uint32_t TitleSystem::GetCurrentTitle() const {
    std::lock_guard lock(mutex_);
    return current_title_;
}

std::string TitleSystem::GetCurrentTitleName(bool isFemale) const {
    std::lock_guard lock(mutex_);
    if (current_title_ == 0) return {};
    auto it = title_db_.find(current_title_);
    if (it == title_db_.end()) return {};
    return isFemale ? it->second.name_female : it->second.name_male;
}

void TitleSystem::ForEachKnown(const TitleCallback& cb) const {
    std::lock_guard lock(mutex_);
    for (const auto& [id, entry] : title_db_) {
        uint32_t idx = entry.mask_index;
        uint32_t qword = idx / 64;
        uint32_t bit   = idx % 64;
        if (qword < known_bits_.size() &&
            (known_bits_[qword] & (uint64_t{1} << bit)) != 0) {
            cb(entry);
        }
    }
}

void TitleSystem::ClearAll() {
    std::lock_guard lock(mutex_);
    title_db_.clear();
    known_bits_.clear();
    current_title_ = 0;
}

void TitleSystem::AddKnownTitle(const TitleDisplayEntry& entry) {
    std::lock_guard lock(mutex_);
    display_titles_[entry.titleId] = entry;
}

bool TitleSystem::HasDisplayTitle(uint32_t titleId) const {
    std::lock_guard lock(mutex_);
    return display_titles_.contains(titleId);
}

std::vector<TitleDisplayEntry> TitleSystem::GetKnownDisplayTitles() const {
    std::lock_guard lock(mutex_);
    std::vector<TitleDisplayEntry> result;
    result.reserve(display_titles_.size());
    for (const auto& [id, entry] : display_titles_) {
        result.push_back(entry);
    }
    return result;
}

void TitleSystem::SelectTitle(uint32_t titleId) {
    std::lock_guard lock(mutex_);
    if (display_titles_.contains(titleId)) {
        selected_display_title_ = titleId;
    }
}

void TitleSystem::ClearSelectedTitle() {
    std::lock_guard lock(mutex_);
    selected_display_title_.reset();
}

std::optional<TitleDisplayEntry> TitleSystem::GetSelectedTitle() const {
    std::lock_guard lock(mutex_);
    if (!selected_display_title_) return std::nullopt;
    auto it = display_titles_.find(*selected_display_title_);
    if (it == display_titles_.end()) return std::nullopt;
    return it->second;
}

std::string TitleSystem::GetDisplayName(const std::string& playerName) const {
    std::lock_guard lock(mutex_);
    if (!selected_display_title_) return playerName;
    auto it = display_titles_.find(*selected_display_title_);
    if (it == display_titles_.end()) return playerName;
    const auto& entry = it->second;
    if (entry.position == TitlePosition::Prefix) {
        return entry.name + " " + playerName;
    } else {
        return playerName + " " + entry.name;
    }
}

size_t TitleSystem::GetTitleCount() const {
    std::lock_guard lock(mutex_);
    return display_titles_.size();
}

std::vector<TitleDisplayEntry> TitleSystem::GetTitlesByPosition(
    TitlePosition pos) const {
    std::lock_guard lock(mutex_);
    std::vector<TitleDisplayEntry> result;
    for (const auto& [id, entry] : display_titles_) {
        if (entry.position == pos) {
            result.push_back(entry);
        }
    }
    return result;
}

void TitleSystem::Reset() {
    std::lock_guard lock(mutex_);
    title_db_.clear();
    known_bits_.clear();
    current_title_ = 0;
    display_titles_.clear();
    selected_display_title_.reset();
}

}
