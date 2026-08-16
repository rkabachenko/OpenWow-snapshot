
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace openwow::game {

enum class EmblemType : uint8_t {
    Heroism  = 0,
    Valor    = 1,
    Conquest = 2,
    Triumph  = 3,
    Frost    = 4,
    Count    = 5,
};

struct TokenInfo {
    EmblemType emblemType   = EmblemType::Heroism;
    uint32_t   count        = 0;
    uint32_t   weeklyEarned = 0;
    uint32_t   weeklyMax    = 0;
    uint32_t   totalEarned  = 0;
};

class TokenSystem {
public:

    void SetTokenCount(EmblemType type, uint32_t count);

    [[nodiscard]] uint32_t GetTokenCount(EmblemType type) const;

    void AddTokens(EmblemType type, uint32_t amount);

    bool SpendTokens(EmblemType type, uint32_t amount);

    [[nodiscard]] TokenInfo GetTokenInfo(EmblemType type) const;

    [[nodiscard]] std::vector<TokenInfo> GetAllTokens() const;

    void SetWeeklyEarned(EmblemType type, uint32_t earned);
    [[nodiscard]] uint32_t GetWeeklyEarned(EmblemType type) const;

    void SetWeeklyMax(EmblemType type, uint32_t max);
    [[nodiscard]] bool IsWeeklyCapped(EmblemType type) const;

    void ResetWeekly();

    [[nodiscard]] static std::string GetEmblemName(EmblemType type);
    [[nodiscard]] static uint32_t    GetEmblemColor(EmblemType type);

    void Reset();

private:
    static constexpr size_t kTypeCount = static_cast<size_t>(EmblemType::Count);

    [[nodiscard]] size_t Idx(EmblemType type) const {
        return static_cast<size_t>(type);
    }

    TokenInfo tokens_[kTypeCount] = {
        {EmblemType::Heroism},
        {EmblemType::Valor},
        {EmblemType::Conquest},
        {EmblemType::Triumph},
        {EmblemType::Frost},
    };
};

}
