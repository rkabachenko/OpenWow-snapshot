#include "openwow/net/protocol/opcode_dispatch.h"

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <sstream>

namespace openwow::net {

void OpcodeDispatch::RegisterHandler(std::uint16_t opcode,
                                     OpcodeHandler handler,
                                     std::string name) {
  std::lock_guard lock(mutex_);
  handlers_[opcode] = Entry{std::move(handler), std::move(name)};
}

bool OpcodeDispatch::UnregisterHandler(std::uint16_t opcode) {
  std::lock_guard lock(mutex_);
  return handlers_.erase(opcode) > 0;
}

bool OpcodeDispatch::Dispatch(std::uint16_t opcode,
                              const std::vector<std::uint8_t>& payload) {
  std::lock_guard lock(mutex_);
  ++dispatch_count_;

  auto it = handlers_.find(opcode);
  if (it != handlers_.end() && it->second.handler) {
    it->second.handler(payload);
    return true;
  }

  if (default_handler_) {
    default_handler_(payload);
    return true;
  }

  ++unhandled_count_;
  return false;
}

std::uint32_t OpcodeDispatch::DispatchAll(
    const std::vector<std::pair<std::uint16_t, std::vector<std::uint8_t>>>& packets) {
  std::lock_guard lock(mutex_);
  std::uint32_t dispatched = 0;

  for (const auto& [opcode, payload] : packets) {
    ++dispatch_count_;

    auto it = handlers_.find(opcode);
    if (it != handlers_.end() && it->second.handler) {
      it->second.handler(payload);
      ++dispatched;
      continue;
    }

    if (default_handler_) {
      default_handler_(payload);
      ++dispatched;
      continue;
    }

    ++unhandled_count_;
  }

  return dispatched;
}

bool OpcodeDispatch::HasHandler(std::uint16_t opcode) const {
  std::lock_guard lock(mutex_);
  return handlers_.count(opcode) > 0;
}

std::optional<std::string> OpcodeDispatch::GetHandlerName(
    std::uint16_t opcode) const {
  std::lock_guard lock(mutex_);
  auto it = handlers_.find(opcode);
  if (it == handlers_.end()) return std::nullopt;
  return it->second.name;
}

std::size_t OpcodeDispatch::GetRegisteredCount() const {
  std::lock_guard lock(mutex_);
  return handlers_.size();
}

std::vector<std::uint16_t> OpcodeDispatch::GetRegisteredOpcodes() const {
  std::lock_guard lock(mutex_);
  std::vector<std::uint16_t> result;
  result.reserve(handlers_.size());
  for (const auto& [op, _] : handlers_) {
    result.push_back(op);
  }
  std::sort(result.begin(), result.end());
  return result;
}

std::vector<std::pair<std::uint16_t, std::string>> OpcodeDispatch::GetAllHandlerNames() const {
  std::lock_guard lock(mutex_);
  std::vector<std::pair<std::uint16_t, std::string>> result;
  result.reserve(handlers_.size());
  for (const auto& [op, entry] : handlers_) {
    result.emplace_back(op, entry.name);
  }
  std::sort(result.begin(), result.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
  return result;
}

std::string OpcodeDispatch::FormatRegisteredOpcodes() const {
  auto names = GetAllHandlerNames();
  if (names.empty()) return "(no registered opcodes)";

  std::ostringstream oss;
  oss << names.size() << " registered opcodes:\n";
  for (const auto& [op, name] : names) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "  0x%04X", static_cast<unsigned>(op));
    oss << buf << " - " << name << "\n";
  }
  return oss.str();
}

std::uint32_t OpcodeDispatch::GetUnhandledCount() const {
  std::lock_guard lock(mutex_);
  return unhandled_count_;
}

std::uint64_t OpcodeDispatch::GetDispatchCount() const {
  std::lock_guard lock(mutex_);
  return dispatch_count_;
}

void OpcodeDispatch::SetDefaultHandler(OpcodeHandler handler) {
  std::lock_guard lock(mutex_);
  default_handler_ = std::move(handler);
}

void OpcodeDispatch::ClearAll() {
  std::lock_guard lock(mutex_);
  handlers_.clear();
  default_handler_ = nullptr;
  unhandled_count_ = 0;
  dispatch_count_ = 0;
}

}
