
#include "openwow/game/server_message.h"

#include <algorithm>
#include <cmath>

namespace openwow::game {

void ServerMessageSystem::AddMessage(ServerMessageEntry entry) {

    if (entry.messageType == ServerMessageType::Shutdown ||
        entry.messageType == ServerMessageType::Restart) {
        entry.isUrgent = true;
    }

    if (messages_.size() >= max_messages_) {
        messages_.erase(messages_.begin());
    }
    messages_.push_back(std::move(entry));
}

const std::vector<ServerMessageEntry>& ServerMessageSystem::GetMessages() const {
    return messages_;
}

std::optional<ServerMessageEntry> ServerMessageSystem::GetLatestMessage() const {
    if (messages_.empty()) return std::nullopt;
    return messages_.back();
}

std::vector<ServerMessageEntry> ServerMessageSystem::GetMessagesByType(
    ServerMessageType type) const {
    std::vector<ServerMessageEntry> result;
    result.reserve(8);
    for (const auto& m : messages_) {
        if (m.messageType == type) result.push_back(m);
    }
    return result;
}

std::vector<ServerMessageEntry> ServerMessageSystem::GetUrgentMessages() const {
    std::vector<ServerMessageEntry> result;
    result.reserve(8);
    for (const auto& m : messages_) {
        if (m.isUrgent) result.push_back(m);
    }
    return result;
}

bool ServerMessageSystem::HasShutdownWarning() const {
    return std::any_of(messages_.begin(), messages_.end(),
                       [](const auto& m) {
                           return m.messageType == ServerMessageType::Shutdown;
                       });
}

std::uint32_t ServerMessageSystem::GetShutdownTime() const {
    for (auto it = messages_.rbegin(); it != messages_.rend(); ++it) {
        if (it->messageType == ServerMessageType::Shutdown) {
            return it->timeRemaining;
        }
    }
    return 0;
}

bool ServerMessageSystem::HasRestartWarning() const {
    return std::any_of(messages_.begin(), messages_.end(),
                       [](const auto& m) {
                           return m.messageType == ServerMessageType::Restart;
                       });
}

std::uint32_t ServerMessageSystem::GetRestartTime() const {
    for (auto it = messages_.rbegin(); it != messages_.rend(); ++it) {
        if (it->messageType == ServerMessageType::Restart) {
            return it->timeRemaining;
        }
    }
    return 0;
}

bool ServerMessageSystem::HasMaintenanceWarning() const {
    return std::any_of(messages_.begin(), messages_.end(),
                       [](const auto& m) {
                           return m.messageType == ServerMessageType::Maintenance;
                       });
}

std::uint32_t ServerMessageSystem::GetMessageCount() const {
    return static_cast<std::uint32_t>(messages_.size());
}

void ServerMessageSystem::ClearMessages() {
    messages_.clear();
}

void ServerMessageSystem::SetMaxMessages(std::uint32_t max) {
    max_messages_ = max;
}

std::string ServerMessageSystem::FormatTimeRemaining(std::uint32_t seconds) {
    if (seconds == 0) return "0 seconds";

    if (seconds >= 3600) {
        const std::uint32_t hours = seconds / 3600;
        return std::to_string(hours) + (hours == 1 ? " hour" : " hours");
    }
    if (seconds >= 60) {
        const std::uint32_t minutes = seconds / 60;
        return std::to_string(minutes) + (minutes == 1 ? " minute" : " minutes");
    }
    return std::to_string(seconds) + (seconds == 1 ? " second" : " seconds");
}

void ServerMessageSystem::Update(float dt) {
    for (auto& m : messages_) {
        if (m.timeRemaining > 0) {
            m.timestamp += dt;
            auto elapsed_sec = static_cast<std::uint32_t>(m.timestamp);
            if (elapsed_sec > 0) {
                if (elapsed_sec >= m.timeRemaining) {
                    m.timeRemaining = 0;
                } else {
                    m.timeRemaining -= elapsed_sec;
                }
                m.timestamp -= static_cast<float>(elapsed_sec);
            }
        }
    }
}

void ServerMessageSystem::Reset() {
    messages_.clear();
    max_messages_ = 50;
}

}
