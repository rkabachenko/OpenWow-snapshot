
#pragma once

#include <atomic>
#include <bitset>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>

namespace openwow::net {

enum class PacketDirection : std::uint8_t {
  kClientToServer = 0,
  kServerToClient = 1,
};

class PacketLog {
 public:

  static PacketLog& Get();

  void Initialize(const std::string& logDir = "Logs");

  void Shutdown();

  void SetEnabled(bool enabled);
  [[nodiscard]] bool IsEnabled() const noexcept;

  void SetOpcodeFilter(std::uint16_t opcode, bool filtered);

  [[nodiscard]] bool IsFiltered(std::uint16_t opcode) const;

  void LogPacket(PacketDirection dir,
                 std::uint16_t opcode,
                 const std::uint8_t* data,
                 std::size_t size,
                 const char* opcodeName = nullptr);

  static constexpr std::size_t kMaxFilteredOpcodes = 0x600;

 private:
  PacketLog() = default;
  ~PacketLog();

  PacketLog(const PacketLog&) = delete;
  PacketLog& operator=(const PacketLog&) = delete;

  void WriteTextEntry(PacketDirection dir,
                      std::uint16_t opcode,
                      const std::uint8_t* data,
                      std::size_t size,
                      const char* name);

  void WriteBinaryEntry(PacketDirection dir,
                        std::uint16_t opcode,
                        const std::uint8_t* data,
                        std::size_t size);

  void SetDefaultFilters();

  std::mutex mutex_;
  std::ofstream textFile_;
  std::ofstream binFile_;
  std::atomic<bool> enabled_{false};
  bool initialized_{false};
  std::bitset<kMaxFilteredOpcodes> filteredOpcodes_;
};

}
