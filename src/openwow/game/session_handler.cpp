
#include "openwow/game/session_handler.h"

#include "openwow/data/db_cache_instances.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/network/protocol/wotlk/world_packet.h"

#include <utility>

namespace openwow::game {

using openwow::diagnostics::Log;
using openwow::diagnostics::LogLevel;

namespace {

void DispatchGameTimeMinute(
    const openwow::core::ida::GameTimeData& current_time,
    void* const context) {
  auto& session = *static_cast<SessionHandler*>(context);
  session.game_time_callbacks().Dispatch(GameTimeCallbackMoment{
      .minute = current_time.minute,
      .hour = current_time.hour,
      .weekday = current_time.weekday,
      .day = current_time.day,
      .month = current_time.month,
      .year = current_time.year,
      .top_bits = current_time.field_24,
  });
}

}

SessionHandler::SessionHandler(
    openwow::core::ida::GameTimeData* const shared_game_time) noexcept
    : game_time_(shared_game_time != nullptr ? shared_game_time
                                             : &owned_game_time_) {}

void SessionHandler::BindWorldPacketHandlers(
    net::wotlk::MainThreadPacketDispatcher& dispatcher,
    SessionPacketEffects effects) {
  using Opcode = net::wotlk::Opcode;

  packet_registrations_.clear();
  packet_effects_ = std::move(effects);
  const auto bind =
      [this, &dispatcher](
          const Opcode opcode,
          net::wotlk::MainThreadPacketDispatcher::Handler handler) {
        packet_registrations_.push_back(
            dispatcher.Register(opcode, "session", std::move(handler)));
      };

  bind(Opcode::SMSG_ACCOUNT_DATA_TIMES,
       [this](const net::wotlk::WorldPacket& packet) {
         if (!HandleAccountDataTimes(packet.payload.data(),
                                     packet.payload.size())) {
           return false;
         }
         if (packet_effects_.account_data_times) {
           packet_effects_.account_data_times(account_data());
         }
         return true;
       });
  bind(Opcode::SMSG_LOGOUT_RESPONSE,
       [this](const net::wotlk::WorldPacket& packet) {
         if (!HandleLogoutResponse(packet.payload.data(), packet.payload.size())) {
           return false;
         }
         if (packet_effects_.logout_response) {
           packet_effects_.logout_response(logout_response());
         }
         return true;
       });
  bind(Opcode::SMSG_LOGOUT_COMPLETE,
       [this](const net::wotlk::WorldPacket& packet) {
         return HandleLogoutComplete(packet.payload.data(),
                                     packet.payload.size());
       });
  bind(Opcode::SMSG_UPDATE_ACCOUNT_DATA,
       [this](const net::wotlk::WorldPacket& packet) {
         if (!HandleUpdateAccountData(packet.payload.data(),
                                      packet.payload.size())) {
           return false;
         }
         if (last_account_data_update().has_value() &&
             packet_effects_.account_data_update) {
           packet_effects_.account_data_update(*last_account_data_update());
         }
         return true;
       });
  bind(Opcode::SMSG_CLIENTCACHE_VERSION,
       [this](const net::wotlk::WorldPacket& packet) {
         if (!HandleClientCacheVersion(packet.payload.data(),
                                       packet.payload.size())) {
           return false;
         }
         if (packet_effects_.client_cache_version) {
           packet_effects_.client_cache_version(client_cache_version());
         }
         return true;
       });
  bind(Opcode::SMSG_UPDATE_ACCOUNT_DATA_COMPLETE,
       [this](const net::wotlk::WorldPacket& packet) {
         return HandleUpdateAccountDataComplete(packet.payload.data(),
                                                packet.payload.size());
       });
}

bool SessionHandler::HandleLoginSetTimeSpeed(const std::uint8_t* data,
                                               std::size_t len) {
  PacketReader r(data, len);

  std::uint32_t packed_time = 0;
  float new_speed = 0.0f;
  std::uint32_t timezone_hint = 0;
  if (!r.ReadU32(packed_time)) return false;
  if (!r.ReadFloat(new_speed)) return false;
  if (!r.ReadU32(timezone_hint)) return false;
  if (r.Remaining() != 0) return false;

  SetPackedGameTime(packed_time, timezone_hint, true);
  (void)openwow::core::ida::GameTime_SetSpeed(*game_time_, new_speed);

  return true;
}

bool SessionHandler::HandleGameTimeSet(const std::uint8_t* data,
                                       std::size_t len) {
  PacketReader r(data, len);
  std::uint32_t packed_time = 0;
  std::uint32_t auxiliary = 0;
  if (!r.ReadU32(packed_time)) return false;
  if (!r.ReadU32(auxiliary)) return false;
  if (r.Remaining() != 0) return false;
  SetPackedGameTime(packed_time, auxiliary, true);
  return true;
}

bool SessionHandler::HandleGameSpeedSet(const std::uint8_t* data,
                                        std::size_t len) {
  PacketReader r(data, len);
  float game_speed = 0.0f;
  if (!r.ReadFloat(game_speed)) return false;
  if (r.Remaining() != 0) return false;
  (void)openwow::core::ida::GameTime_SetSpeed(*game_time_, game_speed);
  return true;
}

bool SessionHandler::HandleAccountDataTimes(const std::uint8_t* data,
                                              std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU32(acct_data_.server_time)) return false;

  std::uint8_t unk;
  if (!r.ReadU8(unk)) return false;
  if (!r.ReadU32(acct_data_.mask)) return false;

  acct_data_.timestamps.clear();
  for (int i = 0; i < 8; ++i) {
    if (acct_data_.mask & (1u << i)) {
      std::uint32_t ts;
      if (!r.ReadU32(ts)) return false;
      acct_data_.timestamps.push_back(ts);
    }
  }
  return true;
}

bool SessionHandler::HandleTransferPending(const std::uint8_t* data,
                                             std::size_t len) {
  PacketReader r(data, len);
  TransferPendingInfo parsed{};
  if (!r.ReadU32(parsed.map_id)) return false;
  if (r.Remaining() != 0) {
    parsed.has_map_change_details =
        r.ReadU32(parsed.transport_entry) && r.ReadU32(parsed.previous_map_id);
    if (!parsed.has_map_change_details) {
      parsed.transport_entry = 0;
      parsed.previous_map_id = kTransferPendingNoPreviousMap;
    }
  }
  transfer_ = parsed;
  return true;
}

bool SessionHandler::HandleNewWorld(const std::uint8_t* data,
                                      std::size_t len) {
  PacketReader r(data, len);
  NewWorldInfo parsed{};
  if (!r.ReadU32(parsed.map_id)) return false;
  if (!r.ReadFloat(parsed.x)) return false;
  if (!r.ReadFloat(parsed.y)) return false;
  if (!r.ReadFloat(parsed.z)) return false;
  if (!r.ReadFloat(parsed.orientation)) return false;
  parsed.fully_consumed = (r.Remaining() == 0);
  new_world_ = parsed;
  return true;
}

bool SessionHandler::HandleLogoutResponse(const std::uint8_t* data,
                                            std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU32(logout_resp_.result)) return false;
  std::uint8_t instant;
  if (!r.ReadU8(instant)) return false;
  logout_resp_.instant = (instant != 0);
  return true;
}

bool SessionHandler::HandleLogoutComplete(const std::uint8_t* ,
                                            std::size_t ) {
  logout_complete_ = true;
  return true;
}

bool SessionHandler::ConsumeLogoutComplete() {
  const bool complete = logout_complete_;
  logout_complete_ = false;
  return complete;
}

bool SessionHandler::ReadMoveFlagPacket(const std::uint8_t* data,
                                          std::size_t len,
                                          MoveFlagInfo& out) {
  PacketReader r(data, len);
  if (!r.ReadPackedGuid(out.guid)) return false;
  if (!r.ReadU32(out.counter)) return false;
  return true;
}

bool SessionHandler::HandleForceMoveRoot(const std::uint8_t* data,
                                           std::size_t len) {
  if (!ReadMoveFlagPacket(data, len, last_root_)) return false;
  rooted_ = true;
  return true;
}

bool SessionHandler::HandleForceMoveUnroot(const std::uint8_t* data,
                                             std::size_t len) {
  if (!ReadMoveFlagPacket(data, len, last_unroot_)) return false;
  rooted_ = false;
  return true;
}

bool SessionHandler::HandleMoveKnockBack(const std::uint8_t* data,
                                            std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadPackedGuid(last_kb_.guid)) return false;
  if (!r.ReadU32(last_kb_.counter)) return false;
  if (!r.ReadFloat(last_kb_.cos_angle)) return false;
  if (!r.ReadFloat(last_kb_.sin_angle)) return false;
  if (!r.ReadFloat(last_kb_.speed_xy)) return false;
  if (!r.ReadFloat(last_kb_.speed_z)) return false;
  return true;
}

bool SessionHandler::HandleMoveSetCanFly(const std::uint8_t* data,
                                           std::size_t len) {
  if (!ReadMoveFlagPacket(data, len, last_can_fly_)) return false;
  can_fly_ = true;
  return true;
}

bool SessionHandler::HandleMoveUnsetCanFly(const std::uint8_t* data,
                                             std::size_t len) {
  if (!ReadMoveFlagPacket(data, len, last_unset_fly_)) return false;
  can_fly_ = false;
  return true;
}

bool SessionHandler::HandleStartMirrorTimer(const std::uint8_t* data,
                                              std::size_t len) {
  PacketReader r(data, len);
  std::uint32_t type;
  if (!r.ReadU32(type)) return false;
  mirror_start_.type = static_cast<MirrorTimerType>(type);
  if (!r.ReadU32(mirror_start_.value)) return false;
  if (!r.ReadU32(mirror_start_.max_value)) return false;
  if (!r.ReadI32(mirror_start_.scale)) return false;
  std::uint8_t paused;
  if (!r.ReadU8(paused)) return false;
  mirror_start_.paused = (paused != 0);
  if (!r.ReadU32(mirror_start_.spell_id)) return false;
  return true;
}

bool SessionHandler::HandleStopMirrorTimer(const std::uint8_t* data,
                                             std::size_t len) {
  PacketReader r(data, len);
  std::uint32_t type;
  if (!r.ReadU32(type)) return false;
  mirror_stop_.type = static_cast<MirrorTimerType>(type);
  return true;
}

bool SessionHandler::HandleSetProficiency(const std::uint8_t* data,
                                            std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU8(proficiency_.item_class)) return false;
  if (!r.ReadU32(proficiency_.subclass_mask)) return false;
  if (proficiency_.item_class < proficiency_masks_.size()) {
    proficiency_masks_[proficiency_.item_class] = proficiency_.subclass_mask;
  }
  return true;
}

bool SessionHandler::HandleStandStateUpdate(const std::uint8_t* data,
                                              std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU8(stand_state_.state)) return false;
  return true;
}

bool SessionHandler::HandleUpdateComboPoints(const std::uint8_t* data,
                                               std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadPackedGuid(combo_.target)) return false;
  if (!r.ReadU8(combo_.points)) return false;
  return true;
}

bool SessionHandler::HandlePlaySound(const std::uint8_t* data,
                                       std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU32(last_sound_.sound_id)) return false;
  return true;
}

bool SessionHandler::HandleUpdateAccountData(const std::uint8_t* data,
                                              std::size_t len) {
  PacketReader r(data, len);
  AccountDataUpdate update;
  if (!r.ReadU64(update.guid)) return false;
  if (!r.ReadU32(update.type)) return false;
  if (!r.ReadU32(update.time)) return false;
  if (!r.ReadU32(update.decompressed_size)) return false;

  auto remaining = r.Remaining();
  std::vector<std::uint8_t> cdata(remaining);
  if (!r.ReadBytes(cdata.data(), remaining)) return false;
  update.compressed_data = std::move(cdata);

  last_account_data_update_ = std::move(update);
  return true;
}

bool SessionHandler::HandleClientCacheVersion(const std::uint8_t* data,
                                               std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU32(client_cache_version_)) return false;
  return true;
}

void SessionHandler::Clear() {
  *game_time_ = openwow::core::ida::GameTimeData{};
  game_time_callbacks_.Clear();
  acct_data_ = AccountDataTimesInfo{};
  transfer_ = TransferPendingInfo{};
  new_world_ = NewWorldInfo{};
  logout_resp_ = LogoutResponseInfo{};
  logout_complete_ = false;
  last_root_ = MoveFlagInfo{};
  last_unroot_ = MoveFlagInfo{};
  last_kb_ = KnockBackInfo{};
  last_can_fly_ = MoveFlagInfo{};
  last_unset_fly_ = MoveFlagInfo{};
  rooted_ = false;
  can_fly_ = false;
  mirror_start_ = MirrorTimerStart{};
  mirror_stop_ = MirrorTimerStop{};
  proficiency_ = ProficiencyInfo{};
  proficiency_masks_.fill(0);
  stand_state_ = StandStateInfo{};
  combo_ = ComboPointInfo{};
  last_sound_ = PlaySoundInfo{};

  last_account_data_update_.reset();
  client_cache_version_ = 0;

  last_account_data_complete_.reset();
  last_declined_names_result_.reset();
  last_game_time_update_.reset();
}

bool SessionHandler::HandleUpdateAccountDataComplete(const std::uint8_t* data,
                                                     std::size_t len) {
  PacketReader r(data, len);
  AccountDataComplete info{};
  if (!r.ReadU32(info.type)) return false;
  if (!r.ReadU32(info.unk)) return false;
  last_account_data_complete_ = info;
  return true;
}

bool SessionHandler::HandleDeclinedNamesResult(const std::uint8_t* data,
                                               std::size_t len) {
  PacketReader r(data, len);
  DeclinedNamesResult info{};
  if (!r.ReadU32(info.result)) return false;
  if (info.result == 0 && !r.ReadU64(info.guid)) return false;
  last_declined_names_result_ = info;
  return true;
}

bool SessionHandler::HandleGameTimeUpdate(const std::uint8_t* data,
                                          std::size_t len) {
  PacketReader r(data, len);
  GameTimeUpdate info{};
  if (!r.ReadU32(info.time)) return false;
  if (!r.ReadU32(info.unk)) return false;
  if (r.Remaining() != 0) return false;

  const auto incoming = openwow::core::ida::GameTimeData_FromPacked(
      info.time, info.unk);
  openwow::core::ida::GameTime_Sync(
      *game_time_, incoming, false, DispatchGameTimeMinute, this);
  last_game_time_update_ = info;
  return true;
}

GameTimeInfo SessionHandler::game_time() const {
  return {
      .packed_time = openwow::core::ida::GameTimeData_ToPacked(*game_time_),
      .game_speed = game_time_->time_speed,
      .tz_hint = static_cast<std::uint32_t>(game_time_->tz_offset),
  };
}

void SessionHandler::SetPackedGameTime(const std::uint32_t packed_time,
                                       const std::uint32_t timezone_hint,
                                       const bool notify_current_minute) {
  const auto incoming = openwow::core::ida::GameTimeData_FromPacked(
      packed_time, timezone_hint);
  openwow::core::ida::GameTime_Set(
      *game_time_, incoming, notify_current_minute,
      DispatchGameTimeMinute, this);
}

float SessionHandler::GetGameTimeHourOfDay() const {
  return static_cast<float>(
      openwow::core::ida::GameTime_GetNormalizedTimeOfDay(game_time_) * 24.0);
}

void SessionHandler::AdvanceGameTime(const float dt_seconds) {
  openwow::core::ida::GameTime_Advance(
      *game_time_, dt_seconds, DispatchGameTimeMinute, this);
}

}
