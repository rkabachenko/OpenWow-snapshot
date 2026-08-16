#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::net {

using OpcodeHandler = std::function<void(const std::vector<std::uint8_t>& payload)>;

class OpcodeDispatch {
 public:
  OpcodeDispatch() = default;

  void RegisterHandler(std::uint16_t opcode, OpcodeHandler handler,
                       std::string name);

  bool UnregisterHandler(std::uint16_t opcode);

  bool Dispatch(std::uint16_t opcode,
                const std::vector<std::uint8_t>& payload);

  [[nodiscard]] bool HasHandler(std::uint16_t opcode) const;

  [[nodiscard]] std::optional<std::string> GetHandlerName(
      std::uint16_t opcode) const;

  [[nodiscard]] std::size_t GetRegisteredCount() const;

  [[nodiscard]] std::vector<std::uint16_t> GetRegisteredOpcodes() const;

  [[nodiscard]] std::uint32_t GetUnhandledCount() const;

  [[nodiscard]] std::uint64_t GetDispatchCount() const;

  void SetDefaultHandler(OpcodeHandler handler);

  void ClearAll();

  std::uint32_t DispatchAll(
      const std::vector<std::pair<std::uint16_t, std::vector<std::uint8_t>>>& packets);

  [[nodiscard]] std::vector<std::pair<std::uint16_t, std::string>> GetAllHandlerNames() const;

  [[nodiscard]] std::string FormatRegisteredOpcodes() const;

 private:
  struct Entry {
    OpcodeHandler handler;
    std::string name;
  };

  mutable std::mutex mutex_;
  std::unordered_map<std::uint16_t, Entry> handlers_;
  OpcodeHandler default_handler_;
  std::uint32_t unhandled_count_{0};
  std::uint64_t dispatch_count_{0};
};

}
