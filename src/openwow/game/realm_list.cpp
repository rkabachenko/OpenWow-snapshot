
#include "openwow/game/realm_list.h"

#include <algorithm>

namespace openwow::game {

RealmList& RealmList::Get() {
    static RealmList instance;
    return instance;
}

void RealmList::SetRealms(const std::vector<RealmEntry>& realms) {
    std::lock_guard lock(mutex_);
    realms_ = realms;
}

std::vector<RealmEntry> RealmList::GetRealms() const {
    std::lock_guard lock(mutex_);
    return realms_;
}

std::optional<RealmEntry> RealmList::GetRealm(uint32_t id) const {
    std::lock_guard lock(mutex_);
    for (const auto& r : realms_) {
        if (r.id == id) return r;
    }
    return std::nullopt;
}

std::optional<RealmEntry> RealmList::GetRealmByName(const std::string& name) const {
    std::lock_guard lock(mutex_);
    for (const auto& r : realms_) {
        if (r.name == name) return r;
    }
    return std::nullopt;
}

uint32_t RealmList::GetRealmCount() const {
    std::lock_guard lock(mutex_);
    return static_cast<uint32_t>(realms_.size());
}

void RealmList::SetSelectedRealm(uint32_t id) {
    std::lock_guard lock(mutex_);
    selected_realm_id_ = id;
}

std::optional<RealmEntry> RealmList::GetSelectedRealm() const {
    std::lock_guard lock(mutex_);
    for (const auto& r : realms_) {
        if (r.id == selected_realm_id_) return r;
    }
    return std::nullopt;
}

uint32_t RealmList::GetSelectedRealmId() const {
    std::lock_guard lock(mutex_);
    return selected_realm_id_;
}

std::string RealmList::GetLastSelectedRealmName() const {
    std::lock_guard lock(mutex_);
    return last_selected_realm_name_;
}

void RealmList::SetLastSelectedRealmName(const std::string& name) {
    std::lock_guard lock(mutex_);
    last_selected_realm_name_ = name;
}

bool RealmList::IsRealmOnline(uint32_t id) const {
    std::lock_guard lock(mutex_);
    for (const auto& r : realms_) {
        if (r.id == id) {
            return (r.flags & RealmFlag_Offline) == 0;
        }
    }
    return false;
}

std::optional<RealmEntry> RealmList::GetRecommendedRealm() const {
    std::lock_guard lock(mutex_);

    for (const auto& r : realms_) {
        if (r.population == RealmPopulation::Recommended &&
            (r.flags & RealmFlag_Offline) == 0) {
            return r;
        }
    }

    for (const auto& r : realms_) {
        if ((r.flags & RealmFlag_Offline) == 0) {
            return r;
        }
    }
    return std::nullopt;
}

void RealmList::SortByName() {
    std::lock_guard lock(mutex_);
    std::sort(realms_.begin(), realms_.end(),
              [](const RealmEntry& a, const RealmEntry& b) { return a.name < b.name; });
}

void RealmList::SortByType() {
    std::lock_guard lock(mutex_);
    std::sort(realms_.begin(), realms_.end(),
              [](const RealmEntry& a, const RealmEntry& b) {
                  return static_cast<uint32_t>(a.type) < static_cast<uint32_t>(b.type);
              });
}

void RealmList::SortByPopulation() {
    std::lock_guard lock(mutex_);
    std::sort(realms_.begin(), realms_.end(),
              [](const RealmEntry& a, const RealmEntry& b) {
                  return static_cast<uint8_t>(a.population) < static_cast<uint8_t>(b.population);
              });
}

uint32_t RealmList::GetCharacterCount(uint32_t realm_id) const {
    std::lock_guard lock(mutex_);
    for (const auto& r : realms_) {
        if (r.id == realm_id) return r.character_count;
    }
    return 0;
}

void RealmList::SetCharacterCounts(const std::vector<RealmCharacterCount>& counts) {
    std::lock_guard lock(mutex_);
    for (const auto& c : counts) {
        for (auto& r : realms_) {
            if (r.id == c.realm_id) {
                r.character_count = c.num_chars;
                break;
            }
        }
    }
}

void RealmList::ForEach(const std::function<void(const RealmEntry&)>& callback) const {
    std::lock_guard lock(mutex_);
    for (const auto& r : realms_) {
        callback(r);
    }
}

void RealmList::Reset() {
    std::lock_guard lock(mutex_);
    realms_.clear();
    selected_realm_id_ = 0;
    last_selected_realm_name_.clear();
}

}
