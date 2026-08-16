
#include "openwow/game/lua_error_display.h"

#include <chrono>

namespace openwow::game {

static double Now() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

void LuaErrorDisplay::PushError(const std::string& message,
                                 const std::string& stack,
                                 const std::string& addonName) {
    if (errors_.size() >= kMaxErrors) {

        errors_.erase(errors_.begin());
        if (currentIdx_ > 0) {
            --currentIdx_;
        }
    }

    LuaErrorDisplayEntry entry;
    entry.message   = message;
    entry.fullStack = stack;
    entry.timestamp = Now();
    entry.addonName = addonName;
    entry.dismissed = false;
    errors_.push_back(std::move(entry));

    if (GetQueueSize() == 1) {
        currentIdx_ = static_cast<uint32_t>(errors_.size()) - 1;
    }
}

std::optional<LuaErrorDisplayEntry> LuaErrorDisplay::GetCurrentError() const {
    if (errors_.empty()) return std::nullopt;
    if (mode_ == LuaErrorDisplayMode::HideAll) return std::nullopt;

    if (currentIdx_ < errors_.size() && !errors_[currentIdx_].dismissed) {
        return errors_[currentIdx_];
    }

    int found = FindNextUndismissed(0, 1);
    if (found >= 0) {
        return errors_[found];
    }
    return std::nullopt;
}

void LuaErrorDisplay::DismissCurrent() {
    if (currentIdx_ < errors_.size()) {
        errors_[currentIdx_].dismissed = true;

        int next = FindNextUndismissed(static_cast<int>(currentIdx_) + 1, 1);
        if (next >= 0) {
            currentIdx_ = static_cast<uint32_t>(next);
        }
    }
}

void LuaErrorDisplay::DismissAll() {
    for (auto& e : errors_) {
        e.dismissed = true;
    }
}

uint32_t LuaErrorDisplay::GetQueueSize() const {
    uint32_t count = 0;
    for (const auto& e : errors_) {
        if (!e.dismissed) ++count;
    }
    return count;
}

LuaErrorDisplayMode LuaErrorDisplay::GetMode() const {
    return mode_;
}

void LuaErrorDisplay::SetMode(LuaErrorDisplayMode mode) {
    mode_ = mode;
}

bool LuaErrorDisplay::HasVisibleError() const {
    if (mode_ == LuaErrorDisplayMode::HideAll) return false;
    for (const auto& e : errors_) {
        if (!e.dismissed) return true;
    }
    return false;
}

std::vector<LuaErrorDisplayEntry> LuaErrorDisplay::GetAllErrors() const {
    return errors_;
}

void LuaErrorDisplay::ClearAll() {
    errors_.clear();
    currentIdx_ = 0;
}

void LuaErrorDisplay::NavigateNext() {
    if (errors_.empty()) return;
    int next = FindNextUndismissed(static_cast<int>(currentIdx_) + 1, 1);
    if (next >= 0) {
        currentIdx_ = static_cast<uint32_t>(next);
    }
}

void LuaErrorDisplay::NavigatePrev() {
    if (errors_.empty()) return;
    int prev = FindNextUndismissed(static_cast<int>(currentIdx_) - 1, -1);
    if (prev >= 0) {
        currentIdx_ = static_cast<uint32_t>(prev);
    }
}

uint32_t LuaErrorDisplay::GetCurrentIndex() const {
    if (errors_.empty()) return 0;

    uint32_t oneBasedIdx = 0;
    for (uint32_t i = 0; i <= currentIdx_ && i < errors_.size(); ++i) {
        if (!errors_[i].dismissed) ++oneBasedIdx;
    }
    return oneBasedIdx;
}

int LuaErrorDisplay::FindNextUndismissed(int from, int direction) const {
    if (errors_.empty()) return -1;
    int i = from;
    while (i >= 0 && i < static_cast<int>(errors_.size())) {
        if (!errors_[i].dismissed) return i;
        i += direction;
    }
    return -1;
}

}
