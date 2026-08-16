
#include "openwow/net/adapters/diagnostics/packet_log.h"

#include "openwow/network/protocol/wotlk/opcodes.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace openwow::net {

PacketLog& PacketLog::Get() {
  static PacketLog instance;
  return instance;
}

PacketLog::~PacketLog() { Shutdown(); }

void PacketLog::Initialize(const std::string& logDir) {
  std::lock_guard lock(mutex_);
  if (initialized_) return;

  auto& cvars = openwow::ui::game::CVarSystem::Instance();
  cvars.RegisterCVar("packetLog", "0", openwow::ui::game::CVarFlags::NoSave,
                     "Enable packet logging (0/1)");
  cvars.RegisterCVar("packetLogBinary", "0", openwow::ui::game::CVarFlags::NoSave,
                     "Also write binary .pkt file (0/1)");

  enabled_.store(cvars.GetCVarBool("packetLog"), std::memory_order_relaxed);

  std::error_code ec;
  std::filesystem::create_directories(logDir, ec);

  auto textPath = std::filesystem::path(logDir) / "PacketLog.txt";
  textFile_.open(textPath, std::ios::out | std::ios::trunc);
  if (textFile_.is_open()) {
    textFile_ << "# OpenWoW Packet Log — started "
              << __DATE__ << " " << __TIME__ << "\n"
              << "# Format: [timestamp] DIR opcode NAME (size bytes)\n"
              << "#   hex dump follows\n\n";
    textFile_.flush();
  }

  if (cvars.GetCVarBool("packetLogBinary")) {
    auto binPath = std::filesystem::path(logDir) / "PacketLog.pkt";
    binFile_.open(binPath, std::ios::out | std::ios::binary | std::ios::trunc);
    if (binFile_.is_open()) {

      const char magic[] = {'P', 'K', 'T', '\x01'};
      binFile_.write(magic, 4);
      const std::uint32_t build = 12340;
      binFile_.write(reinterpret_cast<const char*>(&build), sizeof(build));
      binFile_.flush();
    }
  }

  SetDefaultFilters();
  initialized_ = true;

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "PacketLog: initialized (enabled=" +
                         std::string(enabled_.load() ? "true" : "false") + ")");
}

void PacketLog::Shutdown() {
  std::lock_guard lock(mutex_);
  if (!initialized_) return;

  if (textFile_.is_open()) {
    textFile_ << "\n# PacketLog shutdown.\n";
    textFile_.close();
  }
  if (binFile_.is_open()) {
    binFile_.close();
  }

  initialized_ = false;
  enabled_.store(false, std::memory_order_relaxed);
}

void PacketLog::SetEnabled(bool enabled) {
  enabled_.store(enabled, std::memory_order_relaxed);
}

bool PacketLog::IsEnabled() const noexcept {
  return enabled_.load(std::memory_order_relaxed);
}

void PacketLog::SetOpcodeFilter(std::uint16_t opcode, bool filtered) {
  if (opcode >= kMaxFilteredOpcodes) return;
  std::lock_guard lock(mutex_);
  filteredOpcodes_.set(opcode, filtered);
}

bool PacketLog::IsFiltered(std::uint16_t opcode) const {
  if (opcode >= kMaxFilteredOpcodes) return false;

  return filteredOpcodes_.test(opcode);
}

void PacketLog::SetDefaultFilters() {
  using Op = wotlk::Opcode;

  auto Filter = [this](wotlk::Opcode op) {
    filteredOpcodes_.set(wotlk::OpcodeValue(op), true);
  };

  Filter(Op::MSG_MOVE_HEARTBEAT);
  Filter(Op::MSG_MOVE_SET_FACING);
  Filter(Op::SMSG_PONG);
  Filter(Op::CMSG_PING);
  Filter(Op::SMSG_UPDATE_OBJECT);
}

void PacketLog::LogPacket(PacketDirection dir,
                          std::uint16_t opcode,
                          const std::uint8_t* data,
                          std::size_t size,
                          const char* opcodeName) {

  if (!enabled_.load(std::memory_order_relaxed)) return;

  if (opcode < kMaxFilteredOpcodes && filteredOpcodes_.test(opcode)) return;

  if (!opcodeName && opcode < wotlk::kNumOpcodes) {
    opcodeName = wotlk::OpcodeName(opcode);
  }
  if (!opcodeName) opcodeName = "UNKNOWN";

  std::lock_guard lock(mutex_);

  if (textFile_.is_open()) {
    WriteTextEntry(dir, opcode, data, size, opcodeName);
  }
  if (binFile_.is_open()) {
    WriteBinaryEntry(dir, opcode, data, size);
  }
}

void PacketLog::WriteTextEntry(PacketDirection dir,
                               std::uint16_t opcode,
                               const std::uint8_t* data,
                               std::size_t size,
                               const char* name) {

  using Clock = std::chrono::system_clock;
  const auto now = Clock::now();
  const auto tt  = Clock::to_time_t(now);
  const auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now.time_since_epoch()) % 1000;

  std::tm tm_buf{};
#ifdef _WIN32
  localtime_s(&tm_buf, &tt);
#else
  localtime_r(&tt, &tm_buf);
#endif

  const char* dirStr = (dir == PacketDirection::kClientToServer) ? "CMSG" : "SMSG";

  textFile_ << '[' << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S")
            << '.' << std::setw(3) << std::setfill('0') << ms.count()
            << "] " << dirStr
            << " 0x" << std::hex << std::uppercase
            << std::setw(4) << std::setfill('0') << opcode
            << std::dec << " " << name
            << " (" << size << " bytes)\n";

  for (std::size_t offset = 0; offset < size; offset += 16) {
    textFile_ << "  "
              << std::hex << std::setw(4) << std::setfill('0') << offset
              << ':';

    for (std::size_t j = 0; j < 16; ++j) {
      if (j == 8) textFile_ << ' ';
      if (offset + j < size) {
        textFile_ << ' '
                  << std::hex << std::uppercase
                  << std::setw(2) << std::setfill('0')
                  << static_cast<unsigned>(data[offset + j]);
      } else {
        textFile_ << "   ";
      }
    }

    textFile_ << "  |";
    for (std::size_t j = 0; j < 16 && offset + j < size; ++j) {
      char c = static_cast<char>(data[offset + j]);
      textFile_ << (c >= 0x20 && c < 0x7F ? c : '.');
    }
    textFile_ << "|\n";
  }

  textFile_ << std::dec;
  textFile_.flush();
}

void PacketLog::WriteBinaryEntry(PacketDirection dir,
                                 std::uint16_t opcode,
                                 const std::uint8_t* data,
                                 std::size_t size) {

  const auto now_ms = static_cast<std::uint32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());

  const auto dirByte = static_cast<std::uint8_t>(dir);
  const std::uint32_t sizeWithOpcode = static_cast<std::uint32_t>(size + 4);
  const std::uint32_t opcodeU32 = opcode;

  binFile_.write(reinterpret_cast<const char*>(&dirByte), 1);
  binFile_.write(reinterpret_cast<const char*>(&now_ms), 4);
  binFile_.write(reinterpret_cast<const char*>(&sizeWithOpcode), 4);
  binFile_.write(reinterpret_cast<const char*>(&opcodeU32), 4);
  if (size > 0 && data) {
    binFile_.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
  }
  binFile_.flush();
}

}
