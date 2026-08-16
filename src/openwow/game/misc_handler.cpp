
#include "openwow/game/misc_handler.h"

#include "openwow/core/storm_string.h"
#include "openwow/data/db_cache_instances.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/wdb_cache.h"
#include "openwow/data/wdb_persistence.h"
#include "openwow/game/tutorial_system.h"

#include <algorithm>
#include <cstdio>
#include <limits>
#include <utility>

namespace openwow::game {

std::optional<PlayTimeWarningDisplay>
ResolvePlayTimeWarningDisplay(const PlayTimeWarning &warning) {
  int message_id = 0;
  if ((warning.flags & 0x00001000u) != 0u) {
    message_id = 490;
  } else if ((warning.flags & 0x40000000u) != 0u) {
    message_id = 491;
  } else if ((warning.flags & 0x00002000u) != 0u) {
    message_id = 492;
  } else if ((warning.flags & 0x20000000u) != 0u) {
    message_id = 493;
  } else if ((warning.flags & 0x80000000u) != 0u) {
    return PlayTimeWarningDisplay{.system_message_id = 494};
  } else {
    return std::nullopt;
  }

  const auto hours = warning.remaining_seconds / 3600u;
  const auto minutes = (warning.remaining_seconds / 60u) % 60u;
  std::array<char, 30> remaining_time{};
  std::snprintf(remaining_time.data(), remaining_time.size(), "%2u:%02u",
                hours, minutes);
  return PlayTimeWarningDisplay{
      .system_message_id = message_id,
      .remaining_time = remaining_time.data(),
  };
}

namespace {

constexpr std::uint32_t kPageTextCacheInvalidationBit = 0x80000000u;
constexpr std::uint32_t kPageTextCacheEntryIdMask = 0x7FFFFFFFu;

[[nodiscard]] bool IsPageTextCacheInvalidation(const std::uint32_t raw_page_id) {
  return (raw_page_id & kPageTextCacheInvalidationBit) != 0;
}

[[nodiscard]] std::uint32_t NormalizePageTextCacheEntryId(
    const std::uint32_t raw_page_id) {
  return raw_page_id & kPageTextCacheEntryIdMask;
}

std::vector<std::uint8_t> SerializePageTextWdbRecord(const std::string &text,
                                                     const std::uint32_t next_page) {
  std::vector<std::uint8_t> bytes;
  bytes.reserve(text.size() + 5);
  bytes.insert(bytes.end(), text.begin(), text.end());
  bytes.push_back(0);
  bytes.push_back(static_cast<std::uint8_t>(next_page & 0xFF));
  bytes.push_back(static_cast<std::uint8_t>((next_page >> 8) & 0xFF));
  bytes.push_back(static_cast<std::uint8_t>((next_page >> 16) & 0xFF));
  bytes.push_back(static_cast<std::uint8_t>((next_page >> 24) & 0xFF));
  return bytes;
}

std::optional<PageTextResponse>
DeserializePageTextWdbRecord(const std::uint32_t page_id, const std::vector<std::uint8_t> &data) {
  const auto terminator = std::find(data.begin(), data.end(), 0);
  if (terminator == data.end()) {
    return std::nullopt;
  }

  const auto text_bytes = static_cast<std::size_t>(std::distance(data.begin(), terminator));
  if (data.size() < text_bytes + 5) {
    return std::nullopt;
  }

  const auto next_offset = text_bytes + 1;
  PageTextResponse response;
  response.page_id = page_id;
  response.text.assign(data.begin(), terminator);
  response.next_page = static_cast<std::uint32_t>(data[next_offset]) |
                       (static_cast<std::uint32_t>(data[next_offset + 1]) << 8) |
                       (static_cast<std::uint32_t>(data[next_offset + 2]) << 16) |
                       (static_cast<std::uint32_t>(data[next_offset + 3]) << 24);
  return response;
}

void ReadStockCString(PacketReader& reader, std::string& out) {
  if (!reader.ReadCString(out)) {
    out.clear();
  }
}

std::uint32_t ResolveCurrentPlayedTimeValue(
    const std::uint32_t snapshot_total_time,
    const MiscHandler::PlayedTimeClock::time_point snapshot_at,
    const MiscHandler::PlayedTimeClock::time_point now) {
  if (snapshot_at == MiscHandler::PlayedTimeClock::time_point{} ||
      now <= snapshot_at) {
    return snapshot_total_time;
  }

  const auto elapsed_seconds = std::chrono::duration_cast<std::chrono::seconds>(
      now - snapshot_at);
  if (elapsed_seconds.count() <= 0) {
    return snapshot_total_time;
  }

  const auto live_total_time =
      static_cast<std::uint64_t>(snapshot_total_time) +
      static_cast<std::uint64_t>(elapsed_seconds.count());
  return live_total_time >= std::numeric_limits<std::uint32_t>::max()
             ? std::numeric_limits<std::uint32_t>::max()
             : static_cast<std::uint32_t>(live_total_time);
}

[[nodiscard]] std::string LookupWhoAreaName(const openwow::data::dbc::DbcLoader *dbc,
                                            const std::uint32_t area_id) {
  if (dbc == nullptr) {
    return {};
  }

  const auto *entry = dbc->area_table().LookupEntry(area_id);
  if (entry == nullptr) {
    return {};
  }

  return std::string(entry->name);
}

[[nodiscard]] std::string LookupWhoClassName(const openwow::data::dbc::DbcLoader *dbc,
                                             const std::uint32_t class_id) {
  if (dbc == nullptr) {
    return {};
  }

  const auto *entry = dbc->chr_classes().LookupEntry(class_id);
  if (entry == nullptr) {
    return {};
  }

  return std::string(entry->name);
}

[[nodiscard]] std::string LookupWhoRaceName(const openwow::data::dbc::DbcLoader *dbc,
                                            const std::uint32_t race_id) {
  if (dbc == nullptr) {
    return {};
  }

  const auto *entry = dbc->chr_races().LookupEntry(race_id);
  if (entry == nullptr) {
    return {};
  }

  return std::string(entry->name);
}

int CompareWhoLocalizedName(const std::string &left, const std::string &right) {
  if (left.empty() || right.empty()) {
    return 0;
  }

  return core::SStrCmpNoCaseCollate(left.c_str(), right.c_str(), 0x7FFFFFFFu);
}

int CompareWhoSortKey(const WhoEntry &left, const WhoEntry &right,
                      const WhoSortField field,
                      const openwow::data::dbc::DbcLoader *dbc) {
  switch (field) {
  case WhoSortField::Zone:
    return CompareWhoLocalizedName(LookupWhoAreaName(dbc, left.zone_id),
                                   LookupWhoAreaName(dbc, right.zone_id));
  case WhoSortField::Level:
    if (left.level == right.level) {
      return 0;
    }
    return left.level < right.level ? -1 : 1;
  case WhoSortField::Class:
    return CompareWhoLocalizedName(LookupWhoClassName(dbc, left.class_id),
                                   LookupWhoClassName(dbc, right.class_id));
  case WhoSortField::Group:
    return 0;
  case WhoSortField::Name:
    return core::SStrCmpUTF8NoCase(left.name.c_str(), right.name.c_str(), 0x7FFFFFFFu);
  case WhoSortField::Race:
    return CompareWhoLocalizedName(LookupWhoRaceName(dbc, left.race_id),
                                   LookupWhoRaceName(dbc, right.race_id));
  case WhoSortField::Guild:
    return core::SStrCmpNoCaseCollate(left.guild_name.c_str(), right.guild_name.c_str(),
                                      0x7FFFFFFFu);
  }

  return 0;
}

[[nodiscard]] std::optional<WhoSortField> ParseWhoSortField(const std::string_view sort_type) {
  if (sort_type == "zone") {
    return WhoSortField::Zone;
  }
  if (sort_type == "level") {
    return WhoSortField::Level;
  }
  if (sort_type == "class") {
    return WhoSortField::Class;
  }
  if (sort_type == "group") {
    return WhoSortField::Group;
  }
  if (sort_type == "name") {
    return WhoSortField::Name;
  }
  if (sort_type == "race") {
    return WhoSortField::Race;
  }
  if (sort_type == "guild") {
    return WhoSortField::Guild;
  }

  return std::nullopt;
}

}

std::uint32_t MiscHandler::current_total_played_time() const {
  return ResolveCurrentPlayedTimeValue(
      played_time_.total_time, played_time_snapshot_at_, PlayedTimeClock::now());
}

void MiscHandler::SetPlayedTimeSnapshotTimeForTesting(
    const PlayedTimeClock::time_point captured_at) {
  played_time_snapshot_at_ = captured_at;
}

bool MiscHandler::HandleWeather(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  WeatherInfo decoded;
  if (!r.ReadU32(decoded.type))
    return false;
  if (!r.ReadFloat(decoded.grade))
    return false;
  if (!r.ReadU8(decoded.instant_transition))
    return false;
  weather_ = decoded;
  return true;
}

bool MiscHandler::HandleBindPointUpdate(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadFloat(bind_.x))
    return false;
  if (!r.ReadFloat(bind_.y))
    return false;
  if (!r.ReadFloat(bind_.z))
    return false;
  if (!r.ReadU32(bind_.map_id))
    return false;
  if (!r.ReadU32(bind_.area_id))
    return false;
  return true;
}

bool MiscHandler::HandlePlayerBound(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU64(player_bound_.binder_guid))
    return false;
  if (!r.ReadU32(player_bound_.area_id))
    return false;
  return true;
}

bool MiscHandler::HandlePlayedTime(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU32(played_time_.total_time))
    return false;
  if (!r.ReadU32(played_time_.level_time))
    return false;
  if (!r.ReadU8(played_time_.show_in_chat))
    return false;
  played_time_snapshot_at_ = PlayedTimeClock::now();
  return true;
}

bool MiscHandler::HandleWho(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  std::uint32_t wire_display_count = 0;
  WhoListInfo parsed;
  if (!r.ReadU32(wire_display_count))
    return false;
  if (!r.ReadU32(parsed.match_count))
    return false;

  parsed.wire_display_count = wire_display_count;
  parsed.display_count =
      std::min(wire_display_count, WhoListInfo::kMaxDisplayed);
  parsed.entries.reserve(parsed.display_count);
  for (std::uint32_t i = 0; i < wire_display_count; ++i) {
    WhoEntry entry;
    auto &e = entry;
    if (!r.ReadCString(e.name))
      return false;
    if (!r.ReadCString(e.guild_name))
      return false;
    if (!r.ReadU32(e.level))
      return false;
    if (!r.ReadU32(e.class_id))
      return false;
    if (!r.ReadU32(e.race_id))
      return false;
    if (!r.ReadU8(e.gender))
      return false;
    if (!r.ReadU32(e.zone_id))
      return false;
    if (i < WhoListInfo::kMaxDisplayed) {
      parsed.entries.push_back(std::move(entry));
    }
  }

  who_list_ = std::move(parsed);
  return true;
}

void MiscHandler::SortWhoResults(const openwow::data::dbc::DbcLoader *dbc) {
  std::sort(who_list_.entries.begin(), who_list_.entries.end(),
            [this, dbc](const WhoEntry &left, const WhoEntry &right) {
              for (const auto &criterion : who_sort_order_) {
                int result = CompareWhoSortKey(left, right, criterion.field, dbc);
                if (result == 0) {
                  continue;
                }

                if (criterion.descending) {
                  result = -result;
                }

                return result < 0;
              }

              return false;
            });
}

void MiscHandler::UpdateWhoSortOrder(const std::string_view sort_type) {

  const WhoSortField requested =
      ParseWhoSortField(sort_type).value_or(WhoSortField::Name);

  const auto it =
      std::find_if(who_sort_order_.begin(), who_sort_order_.end(),
                   [requested](const WhoSortCriterion &criterion) {
                     return criterion.field == requested;
                   });
  if (it == who_sort_order_.end()) {
    return;
  }

  WhoSortCriterion next_primary = *it;
  if (it == who_sort_order_.begin()) {
    next_primary.descending = !next_primary.descending;
  } else {
    std::move_backward(who_sort_order_.begin(), it, it + 1);
  }

  who_sort_order_.front() = next_primary;
}

bool MiscHandler::HandleMotd(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);

  std::uint32_t line_count;
  if (!r.ReadU32(line_count))
    return false;

  motd_.lines.clear();
  motd_.lines.resize(line_count);
  for (std::uint32_t i = 0; i < line_count; ++i) {
    if (!r.ReadCString(motd_.lines[i]))
      return false;
  }
  return true;
}

bool MiscHandler::HandleTutorialFlags(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  for (auto &flag : tutorials_) {
    if (!r.ReadU32(flag))
      return false;
  }
  tutorial_flags_initialized_ = true;
  TutorialSystem::Instance().InitializeFromServerFlags(tutorials_);
  return true;
}

bool MiscHandler::HandleDuelRequested(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU64(duel_req_.flag_guid))
    return false;
  if (!r.ReadU64(duel_req_.challenger_guid))
    return false;
  return true;
}

bool MiscHandler::HandleDuelWinner(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);

  duel_winner_ = DuelWinnerInfo{};
  (void)r.ReadU8(duel_winner_.win_type);
  ReadStockCString(r, duel_winner_.winner_name);
  ReadStockCString(r, duel_winner_.loser_name);
  return true;
}

bool MiscHandler::HandleDuelComplete(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  std::uint8_t canceled;
  if (!r.ReadU8(canceled))
    return false;
  duel_complete_ = (canceled == 0);
  return true;
}

bool MiscHandler::HandleEmote(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU32(last_emote_.emote_id))
    return false;
  if (!r.ReadU64(last_emote_.guid))
    return false;
  return true;
}

bool MiscHandler::HandleTextEmote(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU64(last_text_emote_.source_guid))
    return false;
  if (!r.ReadU32(last_text_emote_.text_emote_id))
    return false;
  if (!r.ReadU32(last_text_emote_.emote_num))
    return false;
  if (!r.ReadU32(last_text_emote_.name_len))
    return false;
  if (!r.ReadCString(last_text_emote_.target_name))
    return false;
  return true;
}

bool MiscHandler::HandleNotification(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadCString(notification_))
    return false;
  return true;
}

bool MiscHandler::HandleExplorationExperience(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU32(exploration_.area_id))
    return false;
  if (!r.ReadU32(exploration_.experience))
    return false;
  return true;
}

bool MiscHandler::HandleDeathReleaseLoc(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU32(death_loc_.map_id))
    return false;
  if (!r.ReadFloat(death_loc_.x))
    return false;
  if (!r.ReadFloat(death_loc_.y))
    return false;
  if (!r.ReadFloat(death_loc_.z))
    return false;
  return true;
}

bool MiscHandler::HandleCorpseReclaimDelay(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU32(corpse_delay_.delay_ms))
    return false;
  return true;
}

net::wotlk::WorldPacket MiscHandler::BuildPlayedTimeRequest(bool show_in_chat) {
  net::wotlk::WorldPacket pkt(net::wotlk::Opcode::CMSG_PLAYED_TIME);
  pkt.AppendU8(show_in_chat ? 1 : 0);
  return pkt;
}

net::wotlk::WorldPacket MiscHandler::BuildTutorialFlag(std::uint32_t flag_index) {
  net::wotlk::WorldPacket pkt(net::wotlk::Opcode::CMSG_TUTORIAL_FLAG);
  pkt.AppendU32(flag_index);
  return pkt;
}

net::wotlk::WorldPacket MiscHandler::BuildTutorialClear() {
  net::wotlk::WorldPacket pkt(net::wotlk::Opcode::CMSG_TUTORIAL_CLEAR);
  return pkt;
}

net::wotlk::WorldPacket MiscHandler::BuildTutorialReset() {
  net::wotlk::WorldPacket pkt(net::wotlk::Opcode::CMSG_TUTORIAL_RESET);
  return pkt;
}

bool MiscHandler::HandleDuelCountdown(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU32(duel_countdown_.countdown_ms))
    return false;
  return true;
}

bool MiscHandler::HandleDuelOutOfBounds() {
  duel_oob_ = true;
  return true;
}

bool MiscHandler::HandleDuelInBounds() {
  duel_oob_ = false;
  return true;
}

bool MiscHandler::HandleDurabilityDamageDeath() {
  durability_death_ = true;
  return true;
}

bool MiscHandler::HandlePlayMusic(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  PlayMusicInfo decoded;
  if (!r.ReadU32(decoded.sound_kit_id))
    return false;
  last_play_music_ = decoded;
  return true;
}

bool MiscHandler::HandlePlayObjectSound(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  PlayObjectSoundInfo decoded;
  if (!r.ReadU32(decoded.sound_kit_id) ||
      !r.ReadU64(decoded.source_guid))
    return false;
  last_play_object_sound_ = decoded;
  return true;
}

bool MiscHandler::HandleGameObjectCustomAnim(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  GameObjectCustomAnim anim;
  if (!r.ReadU64(anim.guid))
    return false;
  if (!r.ReadU32(anim.anim_id))
    return false;
  last_go_custom_anim_ = anim;
  return true;
}

bool MiscHandler::HandleGameObjectDespawnAnim(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU64(last_go_despawn_guid_))
    return false;
  return true;
}

bool MiscHandler::HandleGameObjectResetState(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU64(last_go_reset_guid_))
    return false;
  return true;
}

bool MiscHandler::HandleGameObjectPageText(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU64(last_go_page_text_guid_))
    return false;
  return true;
}

bool MiscHandler::HandleAreaTriggerMessage(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  AreaTriggerMessage msg;
  if (!r.ReadU32(msg.length))
    return false;
  if (!r.ReadCString(msg.message))
    return false;
  last_area_trigger_msg_ = std::move(msg);
  return true;
}

bool MiscHandler::HandleZoneUnderAttack(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU32(last_zone_under_attack_))
    return false;
  return true;
}

bool MiscHandler::HandleForcedDeathUpdate() {
  forced_death_ = true;
  return true;
}

bool MiscHandler::HandlePreResurrect(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadPackedGuid(last_pre_resurrect_guid_))
    return false;
  return true;
}

bool MiscHandler::HandleCameraShake(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  CameraShake shake;
  if (!r.ReadU32(shake.effect_id))
    return false;
  if (!r.ReadU32(shake.sound_id))
    return false;
  last_camera_shake_ = shake;
  return true;
}

bool MiscHandler::HandlePageTextQueryResponse(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  std::uint32_t raw_page_id = 0;
  if (!r.ReadU32(raw_page_id))
    return false;

  const std::uint32_t page_id = NormalizePageTextCacheEntryId(raw_page_id);
  if (IsPageTextCacheInvalidation(raw_page_id)) {
    if (page_id == 0) {
      return true;
    }
    pending_page_text_queries_.erase(page_id);
    page_text_cache_.erase(page_id);
    if (last_page_text_.has_value() && last_page_text_->page_id == page_id) {
      last_page_text_.reset();
    }

    auto &cache = db_cache_runtime_.cache();
    if (cache.InvalidateEntry(openwow::data::WDBCacheType::PageText, page_id)) {
      db_cache_runtime_.persistence().SetDirty(
          openwow::data::WDBCacheType::PageText);
    }
    return true;
  }
  if (page_id == 0) {
    return true;
  }

  PageTextResponse resp;
  resp.page_id = page_id;
  if (!r.ReadCString(resp.text))
    return false;
  if (!r.ReadU32(resp.next_page))
    return false;
  pending_page_text_queries_.erase(resp.page_id);
  page_text_cache_[resp.page_id] = resp;
  last_page_text_ = resp;

  auto &cache = db_cache_runtime_.cache();
  cache.UpdateEntry(
      openwow::data::WDBCacheType::PageText, resp.page_id,
      SerializePageTextWdbRecord(resp.text, resp.next_page),
      openwow::data::wdb_format::kVersion_PageText);
  db_cache_runtime_.persistence().SetDirty(
      openwow::data::WDBCacheType::PageText);
  if (!cache.Has(openwow::data::WDBCacheType::PageText, resp.page_id)) {
    page_text_cache_.erase(resp.page_id);
    if (last_page_text_.has_value() &&
        last_page_text_->page_id == resp.page_id) {
      last_page_text_.reset();
    }
  }
  return true;
}

const PageTextResponse *MiscHandler::FindCachedPageText(const std::uint32_t page_id) const {
  const auto it = page_text_cache_.find(page_id);
  return it == page_text_cache_.end() ? nullptr : &it->second;
}

bool MiscHandler::HydrateRetailPageTextCache(openwow::data::WDBCache &cache) {
  std::unordered_map<std::uint32_t, PageTextResponse> hydrated;
  const auto keys = cache.GetKeysInPersistenceOrder(
      openwow::data::WDBCacheType::PageText);
  hydrated.reserve(keys.size());

  for (const auto page_id : keys) {
    const auto record = cache.Get(openwow::data::WDBCacheType::PageText,
                                  page_id);
    auto parsed = record.has_value()
                      ? DeserializePageTextWdbRecord(page_id, record->data)
                      : std::nullopt;
    if (page_id == 0 || !parsed.has_value()) {
      cache.ClearType(openwow::data::WDBCacheType::PageText);
      ClearPageTextCacheForClientCacheVersion();
      return false;
    }

    auto canonical = SerializePageTextWdbRecord(parsed->text,
                                                 parsed->next_page);
    if (canonical != record->data) {
      cache.Insert(openwow::data::WDBCacheType::PageText, page_id,
                   std::move(canonical), record->version);
    }
    hydrated.insert_or_assign(page_id, std::move(*parsed));
  }

  page_text_cache_ = std::move(hydrated);
  pending_page_text_queries_.clear();
  last_page_text_.reset();
  return true;
}

void MiscHandler::ClearPageTextCacheForClientCacheVersion() {
  page_text_cache_.clear();
  pending_page_text_queries_.clear();
  last_page_text_.reset();
}

void MiscHandler::MarkPageTextQueryPending(const std::uint32_t page_id) {
  if (page_id != 0) {
    pending_page_text_queries_.insert(page_id);
  }
}

void MiscHandler::ClearPendingPageTextQueriesOnLogout() {
  pending_page_text_queries_.clear();
}

bool MiscHandler::HandlePauseMirrorTimer(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  PauseMirrorTimer info;
  if (!r.ReadU32(info.timer_type))
    return false;
  std::uint8_t paused;
  if (!r.ReadU8(paused))
    return false;
  info.paused = (paused != 0);
  last_pause_mirror_timer_ = info;
  return true;
}

bool MiscHandler::HandleOverrideLight(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  OverrideLight info;
  if (!r.ReadU32(info.env_light))
    return false;
  if (!r.ReadU32(info.override_light))
    return false;
  if (!r.ReadU32(info.transition_ms))
    return false;
  if (r.Remaining() != 0u)
    return false;
  last_override_light_ = info;
  return true;
}

bool MiscHandler::HandleMirrorImageData(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  MirrorImageData info{};
  if (!r.ReadU64(info.guid))
    return false;
  if (!r.ReadU32(info.display_id))
    return false;
  if (!r.ReadU8(info.race))
    return false;

  if (info.race == 0) {

    info.is_creature = true;
    last_mirror_image_ = info;
    return true;
  }

  info.is_creature = false;
  if (!r.ReadU8(info.gender))
    return false;
  if (!r.ReadU8(info.class_id))
    return false;
  if (!r.ReadU8(info.skin))
    return false;
  if (!r.ReadU8(info.face))
    return false;
  if (!r.ReadU8(info.hair_style))
    return false;
  if (!r.ReadU8(info.hair_color))
    return false;
  if (!r.ReadU8(info.facial_hair))
    return false;
  if (!r.ReadU32(info.guild_id))
    return false;
  for (int i = 0; i < 11; ++i) {
    if (!r.ReadU32(info.item_display[i]))
      return false;
  }

  last_mirror_image_ = info;
  return true;
}

bool MiscHandler::HandleMountResult(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU32(mount_result_))
    return false;
  return true;
}

bool MiscHandler::HandleDismountResult(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU32(dismount_result_))
    return false;
  return true;
}

bool MiscHandler::HandleMountSpecialAnim(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU64(mount_special_guid_))
    return false;
  return true;
}

bool MiscHandler::HandleFishEscaped() {
  fish_escaped_ = true;
  return true;
}

bool MiscHandler::HandleFishNotHooked() {
  fish_not_hooked_ = true;
  return true;
}

bool MiscHandler::HandleBinderConfirm(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU64(binder_confirm_guid_))
    return false;
  return true;
}

bool MiscHandler::HandleBindZoneReply(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU32(bind_zone_id_))
    return false;
  return true;
}

bool MiscHandler::HandlePlayerBindError() {
  bind_error_ = true;
  return true;
}

bool MiscHandler::HandleCrossedInebriationThreshold(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  InebriationThreshold info;
  if (!r.ReadU64(info.guid))
    return false;
  if (!r.ReadU32(info.threshold))
    return false;
  if (!r.ReadU32(info.item_id))
    return false;
  last_inebriation_ = info;
  return true;
}

bool MiscHandler::HandleSetFactionAtWar(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  FactionAtWar info;
  if (!r.ReadU32(info.faction_index))
    return false;
  if (!r.ReadU8(info.flags))
    return false;
  last_faction_at_war_ = info;
  return true;
}

bool MiscHandler::HandlePlayerSkinned(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU8(player_skinned_))
    return false;
  return true;
}

bool MiscHandler::HandleTalentsInvoluntarilyReset(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU8(talents_reset_is_pet_))
    return false;
  return true;
}

bool MiscHandler::HandleToggleXpGain() {

  return true;
}

bool MiscHandler::HandleKickReason(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  KickReason info;
  if (!r.ReadU8(info.reason))
    return false;
  if (!r.ReadCString(info.text))
    return false;
  last_kick_reason_ = std::move(info);
  return true;
}

bool MiscHandler::HandleNpcWontTalk(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU64(npc_wont_talk_guid_))
    return false;
  return true;
}

bool MiscHandler::HandleDelayGhostTeleport(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU8(delay_ghost_teleport_))
    return false;
  return true;
}

bool MiscHandler::HandleClearFarSightImmediate() {
  clear_far_sight_ = true;
  return true;
}

bool MiscHandler::HandleCorpseMapPositionResponse(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  CorpseMapPosition pos;
  if (!r.ReadFloat(pos.x))
    return false;
  if (!r.ReadFloat(pos.y))
    return false;
  if (!r.ReadFloat(pos.z))
    return false;
  if (!r.ReadFloat(pos.orientation))
    return false;
  last_corpse_map_pos_ = pos;
  return true;
}

bool MiscHandler::HandleCorpseNotInInstance() {
  corpse_not_in_instance_ = true;
  return true;
}

bool MiscHandler::HandleGhosteeGone(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU8(ghostee_gone_))
    return false;
  return true;
}

bool MiscHandler::HandleOpenContainer(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU64(open_container_guid_))
    return false;
  return true;
}

bool MiscHandler::HandlePlayTimeWarning(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  PlayTimeWarning info;
  if (!r.ReadU32(info.flags))
    return false;
  if (!r.ReadU32(info.remaining_seconds))
    return false;
  last_play_time_warning_ = info;
  return true;
}

bool MiscHandler::HandleProposeLevelGrant(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU64(propose_level_grant_guid_))
    return false;
  return true;
}

bool MiscHandler::HandleReferAFriendExpired() {
  raf_expired_ = true;
  return true;
}

bool MiscHandler::HandleReferAFriendFailure(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  RafFailure info;
  if (!r.ReadU32(info.reason))
    return false;
  if (r.Remaining() > 0) {
    (void)r.ReadCString(info.name);
  }
  last_raf_failure_ = std::move(info);
  return true;
}

bool MiscHandler::HandleInvalidPromotionCode() {
  invalid_promo_code_ = true;
  return true;
}

bool MiscHandler::HandleWorldStateTimerUpdate(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU32(world_state_timer_))
    return false;
  return true;
}

void MiscHandler::Clear() {
  weather_ = WeatherInfo{};
  bind_ = BindPointInfo{};
  player_bound_ = PlayerBoundInfo{};
  played_time_ = PlayedTimeInfo{};
  played_time_snapshot_at_ = PlayedTimeClock::time_point{};
  who_list_ = WhoListInfo{};
  who_client_filter_ = WhoClientFilterInfo{};
  has_who_client_filter_ = false;
  who_sort_order_ = {{
      {WhoSortField::Zone, false},
      {WhoSortField::Level, false},
      {WhoSortField::Class, false},
      {WhoSortField::Group, false},
      {WhoSortField::Name, false},
      {WhoSortField::Race, false},
      {WhoSortField::Guild, false},
  }};
  motd_ = MotdInfo{};
  tutorials_.fill(0);
  tutorial_flags_initialized_ = false;
  TutorialSystem::Instance().Reset();
  duel_req_ = DuelRequestInfo{};
  duel_winner_ = DuelWinnerInfo{};
  duel_complete_ = false;
  duel_countdown_ = {};
  duel_oob_ = false;
  durability_death_ = false;
  last_emote_ = EmoteInfo{};
  last_text_emote_ = TextEmoteInfo{};
  notification_.clear();
  exploration_ = ExplorationExpInfo{};
  death_loc_ = DeathReleaseLoc{};
  corpse_delay_ = CorpseReclaimDelay{};
  last_play_music_ = {};
  last_play_object_sound_ = {};

  last_go_custom_anim_.reset();
  last_go_despawn_guid_ = 0;
  last_go_reset_guid_ = 0;
  last_go_page_text_guid_ = 0;
  last_area_trigger_msg_.reset();
  last_zone_under_attack_ = 0;
  forced_death_ = false;
  last_pre_resurrect_guid_ = ObjectGuid(0);
  last_camera_shake_.reset();

  last_page_text_.reset();

  pending_page_text_queries_.clear();
  last_pause_mirror_timer_.reset();
  last_override_light_.reset();
  last_mirror_image_.reset();

  mount_result_ = 0;
  dismount_result_ = 0;
  mount_special_guid_ = 0;
  fish_escaped_ = false;
  fish_not_hooked_ = false;
  binder_confirm_guid_ = 0;
  bind_zone_id_ = 0;
  bind_error_ = false;
  last_inebriation_.reset();
  last_faction_at_war_.reset();
  player_skinned_ = 0;
  talents_reset_is_pet_ = 0;
  last_kick_reason_.reset();

  npc_wont_talk_guid_ = 0;
  delay_ghost_teleport_ = 0;
  clear_far_sight_ = false;
  last_corpse_map_pos_.reset();
  corpse_not_in_instance_ = false;
  ghostee_gone_ = 0;
  open_container_guid_ = 0;
  last_play_time_warning_.reset();
  propose_level_grant_guid_ = 0;
  raf_expired_ = false;
  last_raf_failure_.reset();
  invalid_promo_code_ = false;
  world_state_timer_ = 0;
}

}
