
#include "openwow/game/guild_log.h"

#include <sstream>

namespace openwow::game {

void GuildLog::AddEntry(const GuildLogEntry& entry) {
    entries_.push_back(entry);

    while (entries_.size() > GetMaxEntries()) {
        entries_.erase(entries_.begin());
    }
}

std::vector<GuildLogEntry> GuildLog::GetEntriesByType(GuildLogType type) const {
    std::vector<GuildLogEntry> result;
    for (auto& e : entries_) {
        if (e.logType == type) result.push_back(e);
    }
    return result;
}

std::vector<GuildLogEntry> GuildLog::GetBankLog(uint32_t tabIndex) const {
    std::vector<GuildLogEntry> result;
    for (auto& e : entries_) {
        if ((e.logType == GuildLogType::BankDeposit ||
             e.logType == GuildLogType::BankWithdraw ||
             e.logType == GuildLogType::BankMoveItem) &&
            e.tabIndex == tabIndex) {
            result.push_back(e);
        }
    }
    return result;
}

std::vector<GuildLogEntry> GuildLog::GetMoneyLog() const {
    std::vector<GuildLogEntry> result;
    for (auto& e : entries_) {
        if (e.logType == GuildLogType::MoneyDeposit ||
            e.logType == GuildLogType::MoneyWithdraw) {
            result.push_back(e);
        }
    }
    return result;
}

std::vector<GuildLogEntry> GuildLog::GetEventLog() const {
    std::vector<GuildLogEntry> result;
    for (auto& e : entries_) {
        switch (e.logType) {
            case GuildLogType::Invite:
            case GuildLogType::Remove:
            case GuildLogType::Promote:
            case GuildLogType::Demote:
            case GuildLogType::MOTD:
            case GuildLogType::Create:
            case GuildLogType::Disband:
                result.push_back(e);
                break;
            default:
                break;
        }
    }
    return result;
}

std::string GuildLog::FormatEntry(const GuildLogEntry& entry) {
    std::ostringstream o;
    o << "[" << GetLogTypeName(entry.logType) << "] ";
    o << entry.playerName;
    if (!entry.targetName.empty())
        o << " -> " << entry.targetName;
    if (entry.itemId > 0)
        o << " (item:" << entry.itemId << ")";
    if (entry.amount > 0)
        o << " amount:" << entry.amount;
    if (!entry.extra.empty())
        o << " (" << entry.extra << ")";
    return o.str();
}

std::string GuildLog::GetLogTypeName(GuildLogType type) {
    switch (type) {
        case GuildLogType::Invite:        return "Invite";
        case GuildLogType::Remove:        return "Remove";
        case GuildLogType::Promote:       return "Promote";
        case GuildLogType::Demote:        return "Demote";
        case GuildLogType::MOTD:          return "MOTD";
        case GuildLogType::Create:        return "Create";
        case GuildLogType::Disband:       return "Disband";
        case GuildLogType::BankDeposit:   return "BankDeposit";
        case GuildLogType::BankWithdraw:  return "BankWithdraw";
        case GuildLogType::BankMoveItem:  return "BankMoveItem";
        case GuildLogType::BankBuyTab:    return "BankBuyTab";
        case GuildLogType::MoneyDeposit:  return "MoneyDeposit";
        case GuildLogType::MoneyWithdraw: return "MoneyWithdraw";
    }
    return "Unknown";
}

}
