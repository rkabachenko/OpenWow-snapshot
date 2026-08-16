#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

struct ComplaintableChatRecord {
  std::uint32_t line_id = 0;
  std::uint64_t sender_guid = 0;
  std::uint32_t aux_value = 0;
  std::uint32_t channel_lookup_id = 0;
  std::uint32_t recorded_at = 0;
  int chat_type = 0;
  std::string sender_name;
  std::string message;
  std::string formatted_line;
};

struct GMTicketChatLogPayload {
  std::uint32_t line_count = 0;
  std::vector<std::uint32_t> line_ages;
  std::uint32_t decompressed_size = 0;
  std::vector<std::uint8_t> compressed_lines;
};

class GMTicketChatLog {
 public:
  static GMTicketChatLog& Get();

  void RecordDisplayMessage(int chat_type,
                            const char* channel_name,
                            const char* player_name,
                            std::uint64_t sender_guid,
                            std::uint32_t aux_value,
                            std::uint32_t channel_lookup_id,
                            std::uint32_t line_id,
                            std::uint64_t active_player_guid,
                            const char* text);

  [[nodiscard]] std::optional<ComplaintableChatRecord> FindComplaintRecordByLineId(
      std::uint32_t line_id) const;
  [[nodiscard]] std::optional<ComplaintableChatRecord> FindComplaintRecordBySenderAndText(
      const char* player_name, const char* text = nullptr) const;
  [[nodiscard]] GMTicketChatLogPayload BuildPayload() const;

  void Clear();

 private:
  struct Entry {
    std::string formatted_line;
    std::string message;
    std::string sender_name;
    std::uint64_t sender_guid = 0;
    std::uint32_t aux_value = 0;
    std::uint32_t channel_lookup_id = 0;
    std::uint32_t line_id = 0;
    int chat_type = 0;
    std::uint32_t recorded_at = 0;
  };

  static bool IsReportableChatType(int chat_type);
  static int ComplaintPriority(int chat_type, std::uint32_t channel_lookup_id);
  static std::string FormatEntry(
      int chat_type, const char* channel_name, const char* player_name,
      std::uint64_t sender_guid, std::uint64_t active_player_guid, const char* text);
  static std::string CopyWithStormLimit(const char* text, std::size_t limit_including_nul);
  static std::uint32_t CurrentUnixTime();

  static constexpr std::size_t kEntryCount = 60;

  std::array<Entry, kEntryCount> entries_{};
  std::size_t next_entry_index_ = 0;
  mutable std::mutex mutex_;
};

}
