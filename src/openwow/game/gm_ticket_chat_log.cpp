
#include "openwow/game/gm_ticket_chat_log.h"

#include "openwow/core/storm_string.h"
#include "openwow/network/serialization/zlib_compression.h"

#include <array>
#include <cinttypes>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <vector>

namespace openwow::game {

namespace {

constexpr std::array<int, 22> kReportableChatTypes = {
    1,  2,  3,  4,  5,  6,  7,  9,  10, 11, 17,
    23, 24, 39, 40, 44, 45, 47, 51, 53, 54, 55,
};

constexpr std::uint32_t kUnfilteredComplaintLineId = 0;

}

GMTicketChatLog& GMTicketChatLog::Get() {
  static GMTicketChatLog instance;
  return instance;
}

bool GMTicketChatLog::IsReportableChatType(const int chat_type) {
  for (const int reportable_type : kReportableChatTypes) {
    if (reportable_type == chat_type) {
      return true;
    }
  }
  return false;
}

int GMTicketChatLog::ComplaintPriority(const int chat_type,
                                       const std::uint32_t channel_lookup_id) {
  switch (chat_type) {
    case 7:
    case 47:
      return 0;
    case 17:
      return channel_lookup_id == 0 ? 2 : 1;
    case 45:
      return 3;
    case 44:
      return 4;
    case 39:
      return 5;
    case 40:
      return 6;
    case 3:
      return 7;
    case 51:
      return 8;
    case 2:
      return 9;
    case 6:
      return 10;
    case 1:
      return 11;
    case 10:
      return 12;
    case 23:
      return 13;
    case 24:
      return 14;
    default:
      return -1;
  }
}

std::string GMTicketChatLog::FormatEntry(
    const int chat_type,
    const char* channel_name,
    const char* player_name,
    const std::uint64_t sender_guid,
    const std::uint64_t active_player_guid,
    const char* text) {
  std::array<char, 3000> buffer{};
  const char* channel = channel_name != nullptr ? channel_name : "";
  const char* player = player_name != nullptr ? player_name : "";
  const char* message = text != nullptr ? text : "";
  std::snprintf(
      buffer.data(), buffer.size(),
      "Type: [%d], Channel: [%s], Player Name: [%s], Sender GUID: [%016" PRIX64
      "], Active player: [%016" PRIX64 "], Text: [%s]",
      chat_type, channel, player, sender_guid, active_player_guid, message);
  return std::string(buffer.data());
}

std::string GMTicketChatLog::CopyWithStormLimit(const char* text,
                                                const std::size_t limit_including_nul) {
  if (text == nullptr || limit_including_nul == 0) {
    return {};
  }

  const std::size_t max_length = limit_including_nul - 1;
  std::string result(text);
  if (result.size() > max_length) {
    result.resize(max_length);
  }
  return result;
}

std::uint32_t GMTicketChatLog::CurrentUnixTime() {
  return static_cast<std::uint32_t>(std::time(nullptr));
}

void GMTicketChatLog::RecordDisplayMessage(const int chat_type,
                                           const char* channel_name,
                                           const char* player_name,
                                           const std::uint64_t sender_guid,
                                           const std::uint32_t aux_value,
                                           const std::uint32_t channel_lookup_id,
                                           const std::uint32_t line_id,
                                           const std::uint64_t active_player_guid,
                                           const char* text) {
  if (text == nullptr || !IsReportableChatType(chat_type)) {
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  entries_[next_entry_index_] = Entry{
      .formatted_line = FormatEntry(chat_type, channel_name, player_name, sender_guid,
                                    active_player_guid, text),
      .message = CopyWithStormLimit(text, 3000),
      .sender_name = CopyWithStormLimit(player_name, 48),
      .sender_guid = sender_guid,
      .aux_value = aux_value,
      .channel_lookup_id = channel_lookup_id,
      .line_id = line_id,
      .chat_type = chat_type,
      .recorded_at = CurrentUnixTime(),
  };
  next_entry_index_ = (next_entry_index_ + 1) % kEntryCount;
}

std::optional<ComplaintableChatRecord> GMTicketChatLog::FindComplaintRecordByLineId(
    const std::uint32_t line_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::optional<ComplaintableChatRecord> best_record;
  int best_priority = -1;

  for (std::size_t offset = 0; offset < kEntryCount; ++offset) {
    const Entry& entry = entries_[(next_entry_index_ + offset) % kEntryCount];
    const int priority =
        ComplaintPriority(entry.chat_type, entry.channel_lookup_id);
    if (entry.formatted_line.empty() || priority < 0 ||
        (line_id != kUnfilteredComplaintLineId && entry.line_id != line_id)) {
      continue;
    }
    if (best_record.has_value() && priority >= best_priority) {
      continue;
    }

    best_priority = priority;
    best_record = ComplaintableChatRecord{
        .line_id = entry.line_id,
        .sender_guid = entry.sender_guid,
        .aux_value = entry.aux_value,
        .channel_lookup_id = entry.channel_lookup_id,
        .recorded_at = entry.recorded_at,
        .chat_type = entry.chat_type,
        .sender_name = entry.sender_name,
        .message = entry.message,
        .formatted_line = entry.formatted_line,
    };
  }

  return best_record;
}

std::optional<ComplaintableChatRecord> GMTicketChatLog::FindComplaintRecordBySenderAndText(
    const char* player_name, const char* text) const {
  if (player_name == nullptr || player_name[0] == '\0') {
    return std::nullopt;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  std::optional<ComplaintableChatRecord> best_record;
  int best_priority = -1;

  for (std::size_t offset = 0; offset < kEntryCount; ++offset) {
    const Entry& entry = entries_[(next_entry_index_ + offset) % kEntryCount];
    const int priority = ComplaintPriority(entry.chat_type, entry.channel_lookup_id);
    if (entry.formatted_line.empty() || priority < 0) {
      continue;
    }
    if (openwow::core::SStrCmpNoCase(
            entry.sender_name.c_str(), player_name, 0x7FFFFFFFu) != 0) {
      continue;
    }
    if (text != nullptr &&
        openwow::core::SStrCmpNoCase(entry.message.c_str(), text, 0x7FFFFFFFu) != 0) {
      continue;
    }
    if (best_record.has_value() && priority >= best_priority) {
      continue;
    }

    best_priority = priority;
    best_record = ComplaintableChatRecord{
        .line_id = entry.line_id,
        .sender_guid = entry.sender_guid,
        .aux_value = entry.aux_value,
        .channel_lookup_id = entry.channel_lookup_id,
        .recorded_at = entry.recorded_at,
        .chat_type = entry.chat_type,
        .sender_name = entry.sender_name,
        .message = entry.message,
        .formatted_line = entry.formatted_line,
    };
  }

  return best_record;
}

GMTicketChatLogPayload GMTicketChatLog::BuildPayload() const {
  std::lock_guard<std::mutex> lock(mutex_);

  GMTicketChatLogPayload payload;
  const std::uint32_t now = CurrentUnixTime();
  std::string concatenated;

  for (std::size_t offset = 0; offset < kEntryCount; ++offset) {
    const Entry& entry = entries_[(next_entry_index_ + offset) % kEntryCount];
    if (entry.formatted_line.empty()) {
      continue;
    }

    if (!concatenated.empty()) {
      concatenated.push_back('\n');
    }
    concatenated += entry.formatted_line;

    payload.line_ages.push_back(now - entry.recorded_at);
  }

  payload.line_count = static_cast<std::uint32_t>(payload.line_ages.size());
  if (concatenated.empty()) {
    return payload;
  }

  std::vector<std::uint8_t> uncompressed(concatenated.begin(), concatenated.end());
  uncompressed.push_back(0);
  payload.decompressed_size = static_cast<std::uint32_t>(uncompressed.size());
  payload.compressed_lines =
      openwow::network::serialization::CompressZlib(
          uncompressed.data(), uncompressed.size());
  return payload;
}

void GMTicketChatLog::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  for (Entry& entry : entries_) {
    entry = {};
  }
  next_entry_index_ = 0;
}

}
