
#include "openwow/game/token_system.h"

namespace openwow::game {

void TokenSystem::SetTokenCount(EmblemType type, uint32_t count) {
    tokens_[Idx(type)].count = count;
}

uint32_t TokenSystem::GetTokenCount(EmblemType type) const {
    return tokens_[Idx(type)].count;
}

void TokenSystem::AddTokens(EmblemType type, uint32_t amount) {
    auto& t = tokens_[Idx(type)];

    if (t.weeklyMax > 0) {
        uint32_t room = (t.weeklyEarned < t.weeklyMax)
                            ? (t.weeklyMax - t.weeklyEarned)
                            : 0;
        uint32_t add = (amount < room) ? amount : room;
        t.count += add;
        t.weeklyEarned += add;
        t.totalEarned += add;
    } else {
        t.count += amount;
        t.weeklyEarned += amount;
        t.totalEarned += amount;
    }
}

bool TokenSystem::SpendTokens(EmblemType type, uint32_t amount) {
    auto& t = tokens_[Idx(type)];
    if (t.count < amount) return false;
    t.count -= amount;
    return true;
}

TokenInfo TokenSystem::GetTokenInfo(EmblemType type) const {
    return tokens_[Idx(type)];
}

std::vector<TokenInfo> TokenSystem::GetAllTokens() const {
    return {std::begin(tokens_), std::end(tokens_)};
}

void TokenSystem::SetWeeklyEarned(EmblemType type, uint32_t earned) {
    tokens_[Idx(type)].weeklyEarned = earned;
}

uint32_t TokenSystem::GetWeeklyEarned(EmblemType type) const {
    return tokens_[Idx(type)].weeklyEarned;
}

void TokenSystem::SetWeeklyMax(EmblemType type, uint32_t max) {
    tokens_[Idx(type)].weeklyMax = max;
}

bool TokenSystem::IsWeeklyCapped(EmblemType type) const {
    auto& t = tokens_[Idx(type)];
    return t.weeklyMax > 0 && t.weeklyEarned >= t.weeklyMax;
}

void TokenSystem::ResetWeekly() {
    for (auto& t : tokens_) {
        t.weeklyEarned = 0;
    }
}

std::string TokenSystem::GetEmblemName(EmblemType type) {
    switch (type) {
        case EmblemType::Heroism:  return "Emblem of Heroism";
        case EmblemType::Valor:    return "Emblem of Valor";
        case EmblemType::Conquest: return "Emblem of Conquest";
        case EmblemType::Triumph:  return "Emblem of Triumph";
        case EmblemType::Frost:    return "Emblem of Frost";
        default:                   return "Unknown";
    }
}

uint32_t TokenSystem::GetEmblemColor(EmblemType type) {

    switch (type) {
        case EmblemType::Heroism:  return 0xFF1EFF00;
        case EmblemType::Valor:    return 0xFF0070DD;
        case EmblemType::Conquest: return 0xFFA335EE;
        case EmblemType::Triumph:  return 0xFFFF8000;
        case EmblemType::Frost:    return 0xFF00CCFF;
        default:                   return 0xFFFFFFFF;
    }
}

void TokenSystem::Reset() {
    for (size_t i = 0; i < kTypeCount; ++i) {
        tokens_[i] = TokenInfo{static_cast<EmblemType>(i)};
    }
}

}
