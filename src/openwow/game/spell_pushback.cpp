
#include "openwow/game/spell_pushback.h"

#include <algorithm>

namespace openwow::game {

static constexpr float kDamagePushbackAmount = 0.5f;
static constexpr float kChannelPushbackFraction = 0.25f;

void SpellPushbackSystem::BeginCast(std::uint32_t spellId, float castTime) {
    SpellPushbackState state{};
    state.spellId = spellId;
    state.originalCastTime = castTime;
    state.currentCastTime = castTime;
    state.pushbackCount = 0;
    state.pushbackAmount = 0.0f;
    casts_[spellId] = state;
}

float SpellPushbackSystem::ApplyPushback(std::uint32_t spellId,
                                         PushbackType type) {
    auto it = casts_.find(spellId);
    if (it == casts_.end()) {
        return 0.0f;
    }

    auto& state = it->second;

    if (type == PushbackType::Interrupt) {

        float remaining = state.currentCastTime;
        casts_.erase(it);
        return remaining;
    }

    if (state.pushbackCount >= SpellPushbackState::kMaxPushbacks) {
        return state.currentCastTime;
    }

    state.pushbackCount++;
    state.pushbackAmount += kDamagePushbackAmount;
    state.currentCastTime += kDamagePushbackAmount;

    return state.currentCastTime;
}

float SpellPushbackSystem::ApplyChannelPushback(std::uint32_t spellId) {
    auto it = casts_.find(spellId);
    if (it == casts_.end()) {
        return 0.0f;
    }

    auto& state = it->second;
    float loss = state.originalCastTime * kChannelPushbackFraction;
    state.currentCastTime = std::max(0.0f, state.currentCastTime - loss);
    state.pushbackCount++;
    state.pushbackAmount += loss;

    return state.currentCastTime;
}

std::optional<SpellPushbackState> SpellPushbackSystem::GetPushbackState(
    std::uint32_t spellId) const {
    auto it = casts_.find(spellId);
    if (it == casts_.end()) {
        return std::nullopt;
    }
    return it->second;
}

float SpellPushbackSystem::GetCurrentCastTime(std::uint32_t spellId) const {
    auto it = casts_.find(spellId);
    if (it == casts_.end()) {
        return 0.0f;
    }
    return it->second.currentCastTime;
}

std::uint8_t SpellPushbackSystem::GetPushbackCount(
    std::uint32_t spellId) const {
    auto it = casts_.find(spellId);
    if (it == casts_.end()) {
        return 0;
    }
    return it->second.pushbackCount;
}

bool SpellPushbackSystem::IsMaxPushback(std::uint32_t spellId) const {
    auto it = casts_.find(spellId);
    if (it == casts_.end()) {
        return false;
    }
    return it->second.pushbackCount >= SpellPushbackState::kMaxPushbacks;
}

void SpellPushbackSystem::CancelCast(std::uint32_t spellId) {
    casts_.erase(spellId);
}

void SpellPushbackSystem::CompleteCast(std::uint32_t spellId) {
    casts_.erase(spellId);
}

std::vector<SpellPushbackState> SpellPushbackSystem::GetActiveCasts() const {
    std::vector<SpellPushbackState> result;
    result.reserve(casts_.size());
    for (const auto& [id, state] : casts_) {
        result.push_back(state);
    }
    return result;
}

void SpellPushbackSystem::Clear() {
    casts_.clear();
}

}
