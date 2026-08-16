
#include "openwow/game/world_state_display.h"

#include <algorithm>

#include "openwow/core/localized_format.h"

namespace openwow::game {

WorldStateDisplay& WorldStateDisplay::Get() {
    static WorldStateDisplay instance;
    return instance;
}

void WorldStateDisplay::SetState(uint32_t stateId, int32_t value,
                                 uint32_t zoneId) {
    std::lock_guard lock(mutex_);
    auto& entry = states_[stateId];
    entry.stateId = stateId;
    entry.value = value;
    if (zoneId != 0) entry.zoneId = zoneId;
    entry.displayText = FormatEntry(entry);
}

int32_t WorldStateDisplay::GetState(uint32_t stateId) const {
    std::lock_guard lock(mutex_);
    auto it = states_.find(stateId);
    return it != states_.end() ? it->second.value : 0;
}

bool WorldStateDisplay::HasState(uint32_t stateId) const {
    std::lock_guard lock(mutex_);
    return states_.count(stateId) > 0;
}

void WorldStateDisplay::RemoveState(uint32_t stateId) {
    std::lock_guard lock(mutex_);
    states_.erase(stateId);
}

void WorldStateDisplay::SetFormat(uint32_t stateId,
                                  const std::string& format) {
    std::lock_guard lock(mutex_);
    auto it = states_.find(stateId);
    if (it != states_.end()) {
        it->second.formatString = format;
        it->second.displayText = FormatEntry(it->second);
    }
}

std::string WorldStateDisplay::GetDisplayText(uint32_t stateId) const {
    std::lock_guard lock(mutex_);
    auto it = states_.find(stateId);
    if (it == states_.end()) return {};
    return it->second.displayText;
}

std::vector<WorldStateDisplayEntry> WorldStateDisplay::GetStatesForZone(
    uint32_t zoneId) const {
    std::lock_guard lock(mutex_);
    std::vector<WorldStateDisplayEntry> result;
    for (auto& [_, entry] : states_) {
        if (entry.zoneId == zoneId) result.push_back(entry);
    }
    return result;
}

std::vector<WorldStateDisplayEntry> WorldStateDisplay::GetVisibleStates()
    const {
    std::lock_guard lock(mutex_);
    std::vector<WorldStateDisplayEntry> result;
    for (auto& [_, entry] : states_) {
        if (entry.isVisible) result.push_back(entry);
    }
    return result;
}

void WorldStateDisplay::SetVisible(uint32_t stateId, bool visible) {
    std::lock_guard lock(mutex_);
    auto it = states_.find(stateId);
    if (it != states_.end()) it->second.isVisible = visible;
}

uint32_t WorldStateDisplay::GetActiveStateCount() const {
    std::lock_guard lock(mutex_);
    return static_cast<uint32_t>(states_.size());
}

void WorldStateDisplay::ClearZone(uint32_t zoneId) {
    std::lock_guard lock(mutex_);
    for (auto it = states_.begin(); it != states_.end();) {
        if (it->second.zoneId == zoneId)
            it = states_.erase(it);
        else
            ++it;
    }
}

void WorldStateDisplay::ClearAll() {
    std::lock_guard lock(mutex_);
    states_.clear();
}

std::string WorldStateDisplay::FormatEntry(
    const WorldStateDisplayEntry& entry) const {
    if (entry.formatString.empty())
        return std::to_string(entry.value);

    char buf[256];
    core::FormatLocalized(buf, sizeof(buf), entry.formatString.c_str(),
                          entry.value);
    return buf;
}

}
