#pragma once

#include "openwow/network/protocol/wotlk/world_packet.h"
#include "openwow/ui/glue/glue_lua_value.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace openwow::net::wotlk {

struct CharRenameResult {
  std::uint8_t result_code{0};
  std::uint64_t guid{0};
  std::string new_name;
};

struct DeclinedNamesResult {
  std::uint32_t result_code{0};
  std::uint64_t guid{0};
};

struct CharCustomizeResult {
  std::uint8_t result_code{0};
  std::uint64_t guid{0};
  std::string name;
  std::uint8_t gender{0};
  std::uint8_t skin{0};
  std::uint8_t face{0};
  std::uint8_t hair_style{0};
  std::uint8_t hair_color{0};
  std::uint8_t facial_hair{0};
};

struct RealmSplitResult {
  std::uint32_t realm_id{0};
  std::uint32_t split_state{0};
  std::string date;
};

struct KickReasonResult {
  std::uint8_t reason{0};
  std::array<std::uint8_t, 16> token_seed_key{};
  bool has_token_seed_key{false};
};

struct CharFactionChangeResult {
  std::uint8_t result_code{0};
  std::uint64_t guid{0};
  std::string name;
  std::uint8_t race{0};
  std::uint8_t gender{0};
  std::uint8_t skin{0};
  std::uint8_t face{0};
  std::uint8_t hair_style{0};
  std::uint8_t hair_color{0};
  std::uint8_t facial_hair{0};
};

bool ParseCharRename(const WorldPacket& pkt, CharRenameResult& out);
bool ParseDeclinedNamesResult(const WorldPacket& pkt, DeclinedNamesResult& out);
bool ParseCharCustomize(const WorldPacket& pkt, CharCustomizeResult& out);
bool ParseRealmSplit(const WorldPacket& pkt, RealmSplitResult& out);
bool ParseKickReason(const WorldPacket& pkt, KickReasonResult& out);
bool ParseCharFactionChange(const WorldPacket& pkt, CharFactionChangeResult& out);

const char* CharFactionChangeErrorKey(std::uint8_t code);

const char* CharResultGenericErrorKey(std::uint8_t code);

using GlueEventFn = std::function<void(const std::string&, const std::vector<openwow::ui::glue::GlueLuaValue>&)>;

using CharacterUpdateFn = std::function<void(std::uint64_t guid,
                                             const std::string& name,
                                             std::uint8_t race,
                                             std::uint8_t gender,
                                             std::uint8_t skin,
                                             std::uint8_t face,
                                             std::uint8_t hair_style,
                                             std::uint8_t hair_color,
                                             std::uint8_t facial_hair)>;

using DisconnectFn = std::function<void(std::uint8_t reason)>;
using GlueGlobalStringFn = std::function<std::string(const std::string&)>;
using GlueBoolQueryFn = std::function<bool()>;
using MarkDeclinedCharacterFn = std::function<bool(std::uint64_t guid)>;
using RetryEnterWorldFn = std::function<void()>;
using ProcessLegacyTokenSeedFn =
    std::function<void(const std::string&, const std::uint8_t*)>;

struct GlueHandlerCallbacks {
  GlueEventFn fire_event;
  CharacterUpdateFn update_character;
  DisconnectFn request_disconnect;
  GlueGlobalStringFn get_global_string;
  GlueBoolQueryFn is_entering_world;
  MarkDeclinedCharacterFn mark_declined_character;
  RetryEnterWorldFn retry_enter_world;
  ProcessLegacyTokenSeedFn process_legacy_token_seed;
};

void HandleCharRename(const WorldPacket& pkt, const GlueHandlerCallbacks& cb);
void HandleDeclinedNamesResult(const WorldPacket& pkt, const GlueHandlerCallbacks& cb);
void HandleCharCustomize(const WorldPacket& pkt, const GlueHandlerCallbacks& cb);
void HandleRealmSplit(const WorldPacket& pkt, const GlueHandlerCallbacks& cb);
void HandleKickReason(const WorldPacket& pkt, const GlueHandlerCallbacks& cb);
void HandleCharFactionChange(const WorldPacket& pkt, const GlueHandlerCallbacks& cb);

}
