
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace openwow::game {

enum class GuildLogType : uint8_t {
    Invite         = 0,
    Remove         = 1,
    Promote        = 2,
    Demote         = 3,
    MOTD           = 4,
    Create         = 5,
    Disband        = 6,
    BankDeposit    = 7,
    BankWithdraw   = 8,
    BankMoveItem   = 9,
    BankBuyTab     = 10,
    MoneyDeposit   = 11,
    MoneyWithdraw  = 12,
};

struct GuildLogEntry {
    GuildLogType logType   = GuildLogType::Invite;
    std::string playerName;
    std::string targetName;
    uint64_t timestamp     = 0;
    uint64_t amount        = 0;
    uint32_t tabIndex      = 0;
    uint32_t itemId        = 0;
    std::string extra;
};

class GuildLog {
 public:
    GuildLog() = default;

    void AddEntry(const GuildLogEntry& entry);
    [[nodiscard]] std::vector<GuildLogEntry> GetEntries() const { return entries_; }
    [[nodiscard]] std::vector<GuildLogEntry> GetEntriesByType(GuildLogType type) const;

    [[nodiscard]] std::vector<GuildLogEntry> GetBankLog(uint32_t tabIndex) const;

    [[nodiscard]] std::vector<GuildLogEntry> GetMoneyLog() const;

    [[nodiscard]] std::vector<GuildLogEntry> GetEventLog() const;

    [[nodiscard]] uint32_t GetEntryCount() const {
        return static_cast<uint32_t>(entries_.size());
    }
    [[nodiscard]] static constexpr uint32_t GetMaxEntries() { return 25; }

    [[nodiscard]] static std::string FormatEntry(const GuildLogEntry& entry);
    [[nodiscard]] static std::string GetLogTypeName(GuildLogType type);

    void Clear() { entries_.clear(); }

 private:
    std::vector<GuildLogEntry> entries_;
};

}
