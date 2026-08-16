
#include "openwow/game/server_message_handler.h"

#include <algorithm>
#include <cstdio>

namespace openwow::game {

void ServerMessageHandler::PushMessage(ServerMsgType type,
                                       const std::string& message,
                                       std::uint32_t timeRemaining) {
  if (messages_.size() >= kMaxStored) {
    messages_.erase(messages_.begin());
  }

  ServerMessageHandlerEntry entry;
  entry.type          = type;
  entry.message       = message;
  entry.timeRemaining = timeRemaining;
  entry.timestamp     = clock_;
  entry.read          = false;
  messages_.push_back(std::move(entry));

  if (type == ServerMsgType::ShutdownTime ||
      type == ServerMsgType::RestartTime) {
    shutdownCancelled_ = false;
  }

  if (type == ServerMsgType::ShutdownCancelled ||
      type == ServerMsgType::RestartCancelled) {
    shutdownCancelled_ = true;
  }
}

std::vector<ServerMessageHandlerEntry> ServerMessageHandler::GetMessages()
    const {
  return messages_;
}

std::optional<ServerMessageHandlerEntry>
ServerMessageHandler::GetLastMessage() const {
  if (messages_.empty()) return std::nullopt;
  return messages_.back();
}

bool ServerMessageHandler::HasShutdownPending() const {
  if (shutdownCancelled_) return false;
  for (auto it = messages_.rbegin(); it != messages_.rend(); ++it) {
    if (it->type == ServerMsgType::ShutdownTime ||
        it->type == ServerMsgType::RestartTime) {
      return true;
    }
  }
  return false;
}

std::uint32_t ServerMessageHandler::GetShutdownTime() const {
  for (auto it = messages_.rbegin(); it != messages_.rend(); ++it) {
    if (it->type == ServerMsgType::ShutdownTime ||
        it->type == ServerMsgType::RestartTime) {
      return it->timeRemaining;
    }
  }
  return 0;
}

std::string ServerMessageHandler::GetShutdownFormatted() const {
  const auto secs = GetShutdownTime();
  const auto mm   = secs / 60;
  const auto ss   = secs % 60;
  char buf[64];
  std::snprintf(buf, sizeof(buf), "Server restart in %02u:%02u", mm, ss);
  return std::string(buf);
}

void ServerMessageHandler::Update(float deltaTime) {
  clock_ += static_cast<double>(deltaTime);

  for (auto& e : messages_) {
    if ((e.type == ServerMsgType::ShutdownTime ||
         e.type == ServerMsgType::RestartTime) &&
        e.timeRemaining > 0) {

      const auto decrement =
          static_cast<std::uint32_t>(deltaTime);
      if (decrement >= e.timeRemaining) {
        e.timeRemaining = 0;
      } else {
        e.timeRemaining -= decrement;
      }
    }
  }
}

bool ServerMessageHandler::IsShutdownCancelled() const {
  return shutdownCancelled_;
}

void ServerMessageHandler::CancelShutdown() {
  shutdownCancelled_ = true;
}

std::uint32_t ServerMessageHandler::GetMessageCount() const {
  return static_cast<std::uint32_t>(messages_.size());
}

void ServerMessageHandler::ClearAll() {
  messages_.clear();
  shutdownCancelled_ = false;
}

std::vector<ServerMessageHandlerEntry>
ServerMessageHandler::GetMessagesByType(ServerMsgType type) const {
  std::vector<ServerMessageHandlerEntry> result;
  for (const auto& e : messages_) {
    if (e.type == type) result.push_back(e);
  }
  return result;
}

bool ServerMessageHandler::HasUnreadMessages() const {
  return std::any_of(messages_.begin(), messages_.end(),
                     [](const ServerMessageHandlerEntry& e) {
                       return !e.read;
                     });
}

void ServerMessageHandler::MarkAllRead() {
  for (auto& e : messages_) {
    e.read = true;
  }
}

}
