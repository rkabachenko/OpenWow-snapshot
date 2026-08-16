#include "openwow/net/wotlk/glue_startup_handlers.h"

#include "openwow/auth/srp6.h"
#include "openwow/game/account_data.h"
#include "openwow/game/account_data_runtime_sync.h"
#include "openwow/game/session_handler.h"
#include "openwow/net/client_services.h"
#include "openwow/net/wotlk/wow_client_connection.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <array>
#include <vector>

namespace openwow::net::wotlk {

namespace {

constexpr std::array<std::uint8_t, 20> kRetailGlueBotCheckDigest = {
    0x47, 0x0E, 0xCF, 0x29, 0x71, 0x0B, 0x87, 0x71, 0x74, 0xD4,
    0xE9, 0xFA, 0x1F, 0xFE, 0xFC, 0x77, 0xF7, 0x3C, 0xFB, 0x18,
};

constexpr std::array<std::uint8_t, 0x200> kRetailGlueBotCheckCiphertext = {
    0xBE, 0xF3, 0x21, 0x94, 0x86, 0x47, 0x10, 0x82, 0xD9, 0x3C, 0x97, 0x36,
    0x2B, 0xFE, 0xE1, 0xBE, 0x32, 0x1B, 0xCC, 0x3B, 0x58, 0xC4, 0xC1, 0x78,
    0xF6, 0xF7, 0xE5, 0xCD, 0xAA, 0xA7, 0x81, 0x7F, 0x3B, 0xE0, 0xE5, 0x25,
    0xB4, 0xB7, 0x14, 0x06, 0x63, 0xFA, 0xCD, 0xA2, 0x7A, 0x14, 0x4C, 0x41,
    0x83, 0x4B, 0xD7, 0xFF, 0xA4, 0x9A, 0xB5, 0xA8, 0x28, 0x57, 0x5B, 0xF5,
    0xDE, 0x75, 0xFA, 0xE4, 0x54, 0x60, 0xAE, 0x2E, 0x49, 0x52, 0xF8, 0x82,
    0x29, 0x9E, 0x10, 0xFE, 0x58, 0xEC, 0x14, 0x67, 0x62, 0xF6, 0xCB, 0x90,
    0x70, 0xF9, 0x02, 0xC9, 0x92, 0xEC, 0xE1, 0x33, 0xB7, 0xBC, 0x62, 0x7A,
    0x2B, 0x9E, 0xE5, 0xD8, 0xC2, 0xA3, 0x0C, 0x39, 0x4F, 0xAF, 0x93, 0xF0,
    0x07, 0xFE, 0x8E, 0xA2, 0x88, 0x30, 0xD9, 0xE4, 0x4C, 0x1F, 0x53, 0x11,
    0x79, 0x51, 0xFC, 0x57, 0x01, 0x69, 0x3A, 0x69, 0x65, 0xE3, 0x10, 0xCB,
    0x4C, 0xDE, 0x1E, 0x1C, 0x74, 0x7A, 0x5A, 0x7E, 0xCD, 0x71, 0x74, 0x12,
    0x0E, 0x74, 0x96, 0x57, 0xFC, 0xBA, 0x10, 0xEA, 0x07, 0x15, 0xE6, 0xE9,
    0x7A, 0x6D, 0xEC, 0x60, 0xA9, 0xD8, 0x62, 0x29, 0x2E, 0xC1, 0xDA, 0x73,
    0xFF, 0x31, 0xDD, 0x08, 0x91, 0x7E, 0x02, 0xA7, 0x79, 0x08, 0xC7, 0x99,
    0x2F, 0xB8, 0xDD, 0xC3, 0x28, 0xB5, 0x83, 0x27, 0x14, 0x2B, 0x7D, 0x34,
    0x44, 0x6C, 0x0D, 0x63, 0xF6, 0xA7, 0xE4, 0x28, 0x69, 0x0E, 0xD5, 0x89,
    0x3D, 0xD5, 0x74, 0x9A, 0xEC, 0xDD, 0x20, 0x61, 0x18, 0x96, 0x39, 0x19,
    0x68, 0xB0, 0xAE, 0xBB, 0xC4, 0xD8, 0xDF, 0xBF, 0x40, 0xFB, 0x03, 0x54,
    0x3C, 0xF9, 0x13, 0x98, 0xF6, 0xB8, 0xD1, 0x13, 0xC3, 0xF3, 0xB5, 0xD1,
    0xF0, 0x13, 0x71, 0x90, 0x65, 0x88, 0xCA, 0xEC, 0x28, 0x66, 0xCD, 0xF5,
    0x42, 0x45, 0x7A, 0x94, 0xA5, 0x61, 0x4C, 0x9F, 0xEC, 0x3D, 0x5C, 0xB9,
    0x15, 0xD8, 0xD3, 0x5B, 0x14, 0xB5, 0x9C, 0x47, 0x4C, 0xF1, 0x8E, 0x30,
    0xCC, 0xEB, 0xA9, 0x7B, 0x1A, 0xD8, 0x4C, 0xDC, 0xC3, 0x16, 0xFE, 0xA1,
    0x6A, 0x93, 0xFC, 0xC6, 0x35, 0x1D, 0x75, 0x25, 0x5F, 0xC0, 0xC8, 0xD7,
    0xF7, 0x04, 0xE1, 0xC6, 0x3D, 0xA8, 0xAE, 0xF2, 0x3D, 0x6C, 0x6A, 0x2B,
    0x40, 0x83, 0xC6, 0xE5, 0xEF, 0x1B, 0x8E, 0xF1, 0x9F, 0x7C, 0x85, 0xE3,
    0xF0, 0xEE, 0xDB, 0x55, 0x2A, 0xA4, 0x7C, 0xDE, 0x78, 0xFA, 0x96, 0x83,
    0xF2, 0x09, 0x69, 0x6C, 0x4E, 0x79, 0x07, 0xA9, 0xC9, 0x42, 0x4A, 0x8D,
    0x18, 0x10, 0x39, 0x8E, 0xF4, 0xC0, 0xB9, 0x4C, 0x13, 0xAD, 0x43, 0xA5,
    0xA0, 0xEA, 0x25, 0xA6, 0x9C, 0x43, 0xA6, 0xBE, 0xE5, 0xFE, 0xA3, 0xC0,
    0x23, 0xEB, 0x54, 0x37, 0x73, 0x9E, 0x1E, 0x5F, 0xF1, 0x11, 0x9B, 0x96,
    0x7C, 0xFA, 0x05, 0xB8, 0xC4, 0x04, 0x03, 0x30, 0x05, 0xB1, 0x41, 0x50,
    0xDA, 0xA9, 0x8D, 0x16, 0x7C, 0xB2, 0xDD, 0x87, 0x01, 0xC4, 0x09, 0x3C,
    0x19, 0x96, 0xD6, 0xA1, 0x68, 0x22, 0xEF, 0xC9, 0xFE, 0xD9, 0x48, 0xC7,
    0x03, 0x58, 0xEC, 0x8F, 0x2C, 0x5C, 0x4C, 0x46, 0x4A, 0xE0, 0x0C, 0x36,
    0x78, 0x94, 0x7F, 0x4F, 0x9A, 0x23, 0x7E, 0xE0, 0xB2, 0x77, 0xE4, 0x56,
    0x61, 0x1A, 0x0B, 0x9B, 0x4E, 0x9C, 0x01, 0x93, 0xA5, 0x26, 0x87, 0xD9,
    0xC0, 0x3D, 0x61, 0x28, 0xFC, 0x67, 0x94, 0x28, 0xE4, 0x1E, 0x2B, 0x04,
    0x6B, 0x03, 0x91, 0x5D, 0x5B, 0xEA, 0xDD, 0x9D, 0x17, 0xDC, 0xB5, 0x77,
    0x09, 0xA8, 0x37, 0xA1, 0xAC, 0x32, 0xA7, 0x56, 0xB6, 0xBC, 0x98, 0xE0,
    0x4A, 0x62, 0x9A, 0x79, 0xE1, 0x87, 0xC0, 0x1D, 0xC0, 0xF9, 0xCF, 0xE1,
    0xF3, 0x88, 0x86, 0x0E, 0x44, 0x20, 0x81, 0x2A,
};

constexpr std::uint32_t kRetailGlueBotDetectedValue = 0x7634287Du;

struct Rc4State {
  std::array<std::uint8_t, 256> s{};
  std::uint8_t i{0};
  std::uint8_t j{0};

  void Init(const std::uint8_t* key, const std::size_t key_len) {
    for (std::size_t idx = 0; idx < s.size(); ++idx) {
      s[idx] = static_cast<std::uint8_t>(idx);
    }

    std::uint8_t swap_index = 0;
    for (std::size_t idx = 0; idx < s.size(); ++idx) {
      swap_index = static_cast<std::uint8_t>(
          swap_index + s[idx] + key[idx % key_len]);
      std::swap(s[idx], s[swap_index]);
    }
  }

  void Process(std::uint8_t* data, const std::size_t size) {
    for (std::size_t idx = 0; idx < size; ++idx) {
      i = static_cast<std::uint8_t>(i + 1);
      j = static_cast<std::uint8_t>(j + s[i]);
      std::swap(s[i], s[j]);
      const auto key_stream =
          s[static_cast<std::uint8_t>(s[i] + s[j])];
      data[idx] ^= key_stream;
    }
  }
};

std::array<std::uint32_t, 8> BuildAccountDataTimes(
    const game::SessionHandler& handler) {
  std::array<std::uint32_t, 8> times{};
  const auto& parsed = handler.account_data();
  std::size_t timestamp_index = 0;
  for (int slot = 0; slot < 8 && timestamp_index < parsed.timestamps.size(); ++slot) {
    if ((parsed.mask & (1u << slot)) == 0) {
      continue;
    }

    times[slot] = parsed.timestamps[timestamp_index++];
  }
  return times;
}

bool VerifyRetailGlueBotCheck(const std::vector<std::uint8_t>& payload) {
  if (payload.size() != 16) {
    return false;
  }

  Rc4State rc4;
  rc4.Init(payload.data(), payload.size());

  auto decoded = kRetailGlueBotCheckCiphertext;
  rc4.Process(decoded.data(), decoded.size());
  return openwow::auth::SHA1Hash(decoded.data(), decoded.size()) ==
         kRetailGlueBotCheckDigest;
}

bool HandleAccountDataTimes(const WorldPacket& pkt,
                            const GlueStartupDispatchContext& context) {
  game::SessionHandler parser;
  if (!parser.HandleAccountDataTimes(pkt.payload.data(), pkt.payload.size())) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "Glue startup: malformed SMSG_ACCOUNT_DATA_TIMES");
    return true;
  }

  const auto times = BuildAccountDataTimes(parser);
  auto& account_data = openwow::game::AccountData::Get();
  account_data.SetAccountDataTimes(times);
  account_data.SetNextUploadSequence(parser.account_data().server_time);

  if (!context.send_packet) {
    return true;
  }

  const auto& cvars = openwow::ui::game::CVarSystem::Instance();
  if (!cvars.GetCVarBool("synchronizeSettings")) {
    return true;
  }

  const struct SyncSlot {
    openwow::game::AccountDataType type;
    const char* cvar_name;
  } sync_slots[] = {
      {openwow::game::AccountDataType::GlobalConfig, "synchronizeConfig"},
      {openwow::game::AccountDataType::GlobalBindings, "synchronizeBindings"},
      {openwow::game::AccountDataType::GlobalMacros, "synchronizeMacros"},
  };

  for (const auto& slot : sync_slots) {
    const auto index = static_cast<std::size_t>(slot.type);
    if (times[index] == 0) {
      continue;
    }

    if (slot.cvar_name != nullptr && cvars.Exists(slot.cvar_name) &&
        !cvars.GetCVarBool(slot.cvar_name)) {
      continue;
    }

    if (!account_data.MarkServerDownloadPending(slot.type)) {
      continue;
    }

    WorldPacket request(Opcode::CMSG_REQUEST_ACCOUNT_DATA);
    request.AppendU32(static_cast<std::uint32_t>(slot.type));
    (void)context.send_packet(request);
  }

  return true;
}

bool HandleAccountDataUpdate(const WorldPacket& pkt,
                             const GlueStartupDispatchContext& context) {
  game::SessionHandler parser;
  if (!parser.HandleUpdateAccountData(pkt.payload.data(), pkt.payload.size())) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "Glue startup: malformed SMSG_UPDATE_ACCOUNT_DATA");
    return true;
  }

  const auto& update = parser.last_account_data_update();
  if (!update.has_value() || update->type >= 8) {
    return true;
  }

  const auto type = static_cast<openwow::game::AccountDataType>(update->type);
  if (!openwow::game::ShouldApplyAccountDataUpdate(
          type, update->guid, context.current_character_guid)) {
    return true;
  }

  const auto resolution =
      openwow::game::AccountData::Get().ResolveServerDownload(type,
                                                              update->time);
  if (!resolution.had_pending_request || !resolution.should_apply_payload) {
    return true;
  }

  const auto decompressed = openwow::game::AccountData::Decompress(
      update->compressed_data, update->decompressed_size);
  openwow::game::AccountData::Get().SetAccountData(
      type, update->time, decompressed);
  return true;
}

bool HandleAccountDataUpdateComplete(const WorldPacket& pkt) {
  game::SessionHandler parser;
  if (!parser.HandleUpdateAccountDataComplete(pkt.payload.data(),
                                              pkt.payload.size())) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "Glue startup: malformed SMSG_UPDATE_ACCOUNT_DATA_COMPLETE");
  }
  return true;
}

bool HandleCheckForBots(const WorldPacket& pkt,
                        const GlueStartupDispatchContext& context) {
  if (!VerifyRetailGlueBotCheck(pkt.payload)) {
    return true;
  }

  if (!context.send_packet) {
    return true;
  }

  WorldPacket response(Opcode::CMSG_BOT_DETECTED2);
  response.AppendU32(kRetailGlueBotDetectedValue);
  (void)context.send_packet(response);
  return true;
}

}

bool HandleGlueStartupPacket(const WorldPacket& pkt,
                             const GlueStartupDispatchContext& context) {
  switch (pkt.GetOpcode()) {
    case Opcode::SMSG_MOTD:

      return false;
    case Opcode::SMSG_ACCOUNT_DATA_TIMES:
      return HandleAccountDataTimes(pkt, context);
    case Opcode::SMSG_UPDATE_ACCOUNT_DATA:
      return HandleAccountDataUpdate(pkt, context);
    case Opcode::SMSG_UPDATE_ACCOUNT_DATA_COMPLETE:
      return HandleAccountDataUpdateComplete(pkt);
    case Opcode::SMSG_CHECK_FOR_BOTS:
      return HandleCheckForBots(pkt, context);
    default:
      return false;
  }
}

void AccountData_UnregisterOpcodeHandlers() {
  auto* conn = openwow::net::ClientServices::GetConnectionObject();
  if (!conn) {
    return;
  }

  openwow::net::WowClientConnection_UnregisterOpcodeHandler(
      conn, static_cast<int>(Opcode::SMSG_ACCOUNT_DATA_TIMES));
  openwow::net::WowClientConnection_UnregisterOpcodeHandler(
      conn, static_cast<int>(Opcode::SMSG_UPDATE_ACCOUNT_DATA));
  openwow::net::WowClientConnection_UnregisterOpcodeHandler(
      conn, static_cast<int>(Opcode::SMSG_UPDATE_ACCOUNT_DATA_COMPLETE));
  openwow::net::WowClientConnection_UnregisterOpcodeHandler(
      conn, static_cast<int>(Opcode::SMSG_CHECK_FOR_BOTS));
}

}
