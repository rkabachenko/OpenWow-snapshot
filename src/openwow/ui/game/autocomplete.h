#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::ui::game {

class AutoComplete {
public:
  using ContextBits = std::uint32_t;
  using TimestampProvider = std::uint32_t (*)();

  enum class RecentTargetKind : std::uint8_t {
    kPlayerGuid,
    kPresenceId,
  };

  static AutoComplete &Get();

  enum class Context : std::uint8_t {
    kChat = 0,
    kMail,
    kInvite,
    kGuild,
    kTrade,
    kAny,
  };

  void AddPlayerName(const std::string &name, bool is_friend = false, bool is_guild = false);
  void AddPlayerNameWithContextBits(const std::string &name, ContextBits context_bits,
                                    bool is_friend = false, bool is_guild = false);
  void AddChannelName(const std::string &name);
  void AddChannelNameWithContextBits(const std::string &name, ContextBits context_bits);

  void AddFromWhoResults(const std::vector<std::string> &names);

  void AddFromGuildRoster(const std::vector<std::string> &names);

  [[nodiscard]] std::vector<std::string> GetCompletions(const std::string &prefix,
                                                        std::size_t max_results = 10) const;

  [[nodiscard]] std::vector<std::string> GetCompletions(const std::string &prefix, Context context,
                                                        std::size_t max_results = 10) const;

  [[nodiscard]] std::vector<std::string> GetLuaCompletions(const std::string &prefix,
                                                           ContextBits include_bits,
                                                           ContextBits exclude_bits,
                                                           std::size_t max_results = 10) const;

  void TouchRecentPlayerGuid(std::uint64_t raw_guid, ContextBits context_bits,
                             bool update_timestamp, std::string_view name = {});
  void TouchRecentPresenceId(std::int32_t presence_id, ContextBits context_bits,
                             bool update_timestamp, std::string_view name = {});
  bool UpdateRecentPlayerGuidName(std::uint64_t raw_guid, std::string_view name,
                                  bool overwrite_existing);
  bool UpdateRecentPresenceIdName(std::int32_t presence_id, std::string_view name,
                                  bool overwrite_existing);
  void ClearRecentPlayerNameContextBits(std::string_view name, ContextBits context_bits);
  void ClearRecentPlayerGuidContextBits(std::uint64_t raw_guid, ContextBits context_bits);
  void ClearRecentPlayerContextBits(ContextBits context_bits);
  void ClearRecentPresenceIdContextBits(std::int32_t presence_id, ContextBits context_bits);
  void ClearRecentPresenceId(std::int32_t presence_id);
  void ClearRecentPresenceTargets();
  void SetTimestampProviderForTesting(TimestampProvider provider);
  void ResetTimestampProviderForTesting();
  [[nodiscard]] std::optional<std::int32_t>
  GetRecentPresenceIdForName(std::string_view name) const;
  [[nodiscard]] std::vector<std::string>
  GetRecentLuaCompletions(std::string_view text, ContextBits include_bits,
                          ContextBits exclude_bits, std::size_t max_results,
                          std::size_t cursor_position, bool allow_full_match) const;
  [[nodiscard]] std::vector<std::string>
  GetRecentPlayerLuaCompletions(std::string_view text, ContextBits include_bits,
                                ContextBits exclude_bits, std::size_t max_results,
                                std::size_t cursor_position, bool allow_full_match) const;
  [[nodiscard]] std::vector<std::string>
  GetRecentPresenceLuaCompletions(std::string_view text, ContextBits include_bits,
                                  ContextBits exclude_bits, std::size_t max_results,
                                  std::size_t cursor_position, bool allow_full_match) const;

  struct RecentTarget {
    RecentTargetKind kind = RecentTargetKind::kPlayerGuid;
    std::uint64_t identifier = 0;
    std::string name;
    ContextBits context_bits = 0;
    std::uint32_t timestamp = 0;
  };

  void Clear();

  [[nodiscard]] std::size_t GetNumEntries() const;

private:
  AutoComplete();

  struct NameEntry {
    std::string name;
    bool is_friend = false;
    bool is_guild = false;
    bool is_channel = false;
    ContextBits context_bits = 0;
    std::uint32_t hit_count = 0;
  };

  NameEntry *FindEntry(const std::string &name);
  const NameEntry *FindEntry(const std::string &name) const;
  RecentTarget *FindRecentTarget(RecentTargetKind kind, std::uint64_t identifier);
  const RecentTarget *FindRecentTarget(RecentTargetKind kind, std::uint64_t identifier) const;
  void TouchRecentTarget(RecentTargetKind kind, std::uint64_t identifier,
                         ContextBits context_bits, bool update_timestamp,
                         std::string_view name);
  bool UpdateRecentTargetName(RecentTargetKind kind, std::uint64_t identifier,
                              std::string_view name, bool overwrite_existing);
  [[nodiscard]] std::vector<std::string>
  GetRecentLuaCompletionsForKind(std::string_view text, ContextBits include_bits,
                                 ContextBits exclude_bits, std::size_t max_results,
                                 std::size_t cursor_position, bool allow_full_match,
                                 std::optional<RecentTargetKind> kind) const;

  static bool PrefixMatch(const std::string &name, const std::string &prefix);

  static bool MatchesContext(const NameEntry &entry, Context context);

  std::vector<NameEntry> entries_;
  std::vector<RecentTarget> recent_targets_;
  TimestampProvider timestamp_provider_;
  mutable std::mutex mutex_;
};

}
