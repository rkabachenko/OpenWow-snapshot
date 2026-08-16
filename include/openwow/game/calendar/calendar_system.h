
#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "openwow/game/object_guid.h"

namespace openwow::game {

class WorldSession;

struct MonthInfo {
  uint32_t numDays = 0;
  uint32_t firstWeekday = 0;
};

struct CalendarVisibilityFilters {
  bool show_weekly_holidays = true;
  bool show_darkmoon = true;
  bool show_battleground_holidays = false;
  bool show_lockouts = true;
  bool show_resets = false;
};

struct CalendarRaidResetSchedule {
  std::uint32_t map_id = 0;
  std::string title;
  std::uint32_t first_reset_time = 0;
  std::uint32_t period_minutes = 0;
};

struct CalendarSystemEvent {
  uint64_t event_id = 0;
  uint64_t self_invite_id = 0;

  std::string title;
  std::string description;
  uint8_t type = 0;
  uint8_t repeat_option = 0;
  int32_t dungeon_id = -1;
  uint32_t map_id = 0;

  uint32_t holiday_sort_priority = 0;
  int32_t holiday_filter_type = -1;
  uint32_t time = 0;
  uint32_t end_time = 0;
  uint32_t flags = 0;
  uint64_t guild_id = 0;

  uint64_t creator_guid = 0;
  std::string creator_name;
  uint32_t sequence_index = 0;
  uint32_t sequence_total = 1;
  uint8_t invite_status = 0;
  uint8_t invite_mod_status = 0;

  uint8_t invite_type = 0;
  bool pending_invite = false;
  uint64_t pending_sender_guid = 0;

};

struct CalendarSystemInvite {
  uint64_t invite_id = 0;
  uint64_t event_id = 0;
  uint64_t invitee_guid = 0;
  std::string invitee_name;
  uint8_t status = 0;
  uint8_t rank = 0;
  uint32_t response_time = 0;
  bool can_moderate = true;

  uint8_t level = 0;
  uint8_t invite_type = 0;

  std::string notes;
  uint64_t sender_guid = 0;

  bool visible_in_pending_list = true;
  uint8_t class_id = 0;
};

struct CalendarContextEventInfo {
  uint64_t event_id = 0;
  uint64_t self_invite_id = 0;

  uint64_t selected_invitee_guid = 0;

  uint64_t creator_guid = 0;

  bool is_own_event = false;
  bool is_moderator = false;
  uint8_t invite_status = 0;
  uint8_t invite_type = 0;
  uint32_t sequence_index = 0;
  uint32_t sequence_total = 0;

  uint32_t map_id = 0;

  std::string title;
  std::string description;
  uint8_t event_type = 0;
  uint8_t repeat_option = 0;
  uint32_t month = 0;
  uint32_t day = 0;
  uint32_t year = 0;
  uint32_t hour = 0;
  uint32_t minute = 0;

  int32_t weekday = -1;
  int32_t time_flags = 0;

  uint32_t secondary_time_packed = 0x1fffffffu;
  uint32_t flags = 0;

  int32_t dungeon_id = -1;
  uint32_t max_invites = 0;

  bool local_edit = false;

  bool settings_changed = false;

  uint32_t invite_sort_criterion = 0;
  bool invite_sort_reverse = false;

};

enum class CalendarInviteLookupCompletionAction : std::uint8_t {
  kUpdateInviteList = 0,
  kUpdateInviteListWithSelection = 1,
  kUpdateEventNew = 2,
  kUpdateEventExisting = 3,
  kOpenEvent = 4,
};

struct CalendarPendingInviteNameQueryResolution {
  std::uint64_t event_id = 0;
  CalendarInviteLookupCompletionAction completion_action =
      CalendarInviteLookupCompletionAction::kUpdateInviteList;
  bool completed = false;
};

struct CalendarHolidayInfo {
  std::string name;
  std::string description;
  std::string texture;
  uint32_t start_time = 0;
  uint32_t end_time = 0;
};

struct CalendarHolidayPresentation {
  std::uint32_t holiday_id = 0;
  std::string name;
  std::string description;
  std::string texture;
};

struct CalendarHolidaySequenceSource {
  std::uint32_t holiday_id = 0;
  std::string title;
  std::string description;
  std::string texture;
  std::uint32_t flags = 0;
  std::uint32_t holiday_sort_priority = 0;
  std::int32_t holiday_filter_type = -1;
  std::array<std::uint32_t, 26> occurrence_packed_times{};
  std::int64_t sequence_offset_minutes = 0;
  std::int64_t sequence_duration_minutes = 24 * 60;
  std::int64_t repeat_step_minutes = 0;
};

struct CalendarRaidInfo {
  std::string name;
  uint32_t map_id = 0;
  uint64_t instance_id = 0;
  uint32_t reset_month = 0;
  uint32_t reset_day = 0;
  uint32_t reset_year = 0;
  uint8_t difficulty = 0;
  uint32_t max_players = 0;
};

class CalendarSystem {
public:
  static CalendarSystem &Get();

  [[nodiscard]] bool HasRequestedInitialSnapshot() const;
  void MarkInitialSnapshotRequested();

  void ReplaceEvents(const std::vector<CalendarSystemEvent> &events);
  void SetMonthEvents(uint32_t month, uint32_t year,
                      const std::vector<CalendarSystemEvent> &events);
  [[nodiscard]] std::vector<CalendarSystemEvent> GetMonthEvents(uint32_t month,
                                                                uint32_t year) const;

  [[nodiscard]] std::vector<CalendarSystemEvent> GetDayEvents(uint32_t month, uint32_t day,
                                                              uint32_t year) const;
  [[nodiscard]] size_t GetNumDayEvents(uint32_t month, uint32_t day, uint32_t year) const;

  void SetEventDetails(uint64_t eventId, const CalendarSystemEvent &event,
                       const std::vector<CalendarSystemInvite> &invites);
  void UpsertEventSummary(const CalendarSystemEvent &event);
  [[nodiscard]] const CalendarSystemEvent *GetEvent(uint64_t eventId) const;
  [[nodiscard]] std::vector<CalendarSystemInvite> GetEventInvites(uint64_t eventId) const;
  bool UpsertEventInvite(const CalendarSystemInvite &invite);
  bool SetEventInviteeName(uint64_t eventId, uint64_t inviteeGuid, const std::string &name);
  bool SetEventSelfInviteState(uint64_t eventId, uint64_t selfInviteId, uint8_t inviteStatus);
  bool SetEventInviteStatus(uint64_t eventId, uint8_t inviteStatus);
  bool SetInviteStatusForInvitee(uint64_t eventId, uint64_t inviteeGuid, uint8_t inviteStatus,
                                 std::optional<uint32_t> responseTime = std::nullopt);
  bool SetInviteNotesForInvitee(uint64_t eventId, uint64_t inviteeGuid, const std::string &notes);
  bool SetInviteModeratorRankForInvitee(uint64_t eventId, uint64_t inviteeGuid, uint8_t rank);
  bool SetDayEventModeratorFlag(uint64_t eventId, bool isModerator);
  bool RemoveEventInviteByGuid(uint64_t eventId, uint64_t inviteeGuid);

  void SetPendingInvites(const std::vector<CalendarSystemInvite> &invites);

  bool SetPendingInviteCount(size_t count);
  [[nodiscard]] size_t GetNumPendingInvites() const;
  [[nodiscard]] const CalendarSystemInvite *GetPendingInvite(size_t index) const;
  [[nodiscard]] bool HasPendingInviteForEvent(uint64_t eventId) const;
  [[nodiscard]] size_t GetFirstPendingInviteIndex(uint32_t month, uint32_t day,
                                                  uint32_t year) const;
  bool AddPendingInviteForEvent(uint64_t eventId, uint64_t senderGuid, bool visibleInPendingList);
  bool RemovePendingInvitesForEvent(uint64_t eventId);
  bool RefreshPendingInviteVisibility(const std::unordered_set<uint64_t> &ignoredGuids);

  bool UpdateEventFromAlert(uint64_t eventId, uint32_t old_time,
                            const CalendarSystemEvent &updated_event);
  [[nodiscard]] bool HasEvent(uint64_t eventId) const;
  bool RemoveEventById(uint64_t eventId);

  void SetViewMonth(uint32_t month, uint32_t year);
  [[nodiscard]] uint32_t GetViewMonth() const;
  [[nodiscard]] uint32_t GetViewYear() const;
  [[nodiscard]] bool HasViewMonthSelection() const;
  void SetActionPending(bool pending);
  [[nodiscard]] bool IsActionPending() const;
  void SyncCurrentTime(uint32_t packedTime);
  [[nodiscard]] std::optional<uint32_t> GetCurrentTimePacked() const;
  [[nodiscard]] bool HasIndexedDayEventNameReference(uint64_t playerGuid) const;
  void ClearServerEventSummariesForNoGuild();

  void Reset();

  void SetOpenedEvent(const CalendarContextEventInfo &info);
  void ClearOpenedEvent();
  [[nodiscard]] const CalendarContextEventInfo *GetOpenedEvent() const;

  void SetContextMenuEvent(const CalendarContextEventInfo &info);
  void ClearContextMenuEvent();
  [[nodiscard]] const CalendarContextEventInfo *GetContextMenuEvent() const;

  void SetContextEvent(const CalendarContextEventInfo &info);
  void ClearContextEvent();
  [[nodiscard]] const CalendarContextEventInfo *GetContextEvent() const;

  bool UpdateContextEventFromUpdatedAlert(std::uint64_t event_id,
                                          std::uint32_t flags,
                                          std::uint32_t new_date,
                                          std::uint8_t event_type,
                                          std::uint32_t dungeon_id,
                                          const std::string &title,
                                          const std::string &description,
                                          std::uint8_t repeat_option,
                                          std::uint32_t max_invites,
                                          std::uint32_t second_packed_time);

  void SetHolidaySequenceSources(const std::vector<CalendarHolidaySequenceSource> &sources,
                                 const std::vector<CalendarHolidayPresentation> &presentations);
  void SetHolidayOccurrences(const std::vector<CalendarSystemEvent> &occurrences,
                             const std::vector<CalendarHolidayPresentation> &presentations);
  [[nodiscard]] std::optional<CalendarHolidayPresentation>
  GetHolidayPresentation(std::uint32_t holiday_id) const;
  [[nodiscard]] std::optional<CalendarHolidayInfo>
  GetHolidayInfo(uint32_t month, uint32_t day, uint32_t year, uint32_t index) const;
  [[nodiscard]] std::string GetHolidayName(std::uint32_t holiday_id) const;
  [[nodiscard]] std::string GetHolidayTexture(std::uint32_t holiday_id) const;

  void SetRaidInfoList(const std::vector<CalendarRaidInfo> &raids);
  void AddRaidInfo(const CalendarRaidInfo &raid);
  bool RemoveRaidInfo(uint32_t mapId, std::optional<int32_t> difficulty,
                      std::optional<uint64_t> instanceId, std::optional<uint32_t> resetPackedTime);
  [[nodiscard]] std::optional<CalendarRaidInfo> GetRaidInfo(uint32_t index) const;
  [[nodiscard]] size_t GetNumRaidInfo() const;
  bool AddRaidLockoutEvent(const CalendarSystemEvent &event);
  bool RemoveRaidLockoutEvents(uint32_t mapId, std::optional<int32_t> difficulty,
                               std::optional<uint64_t> instanceId,
                               std::optional<uint32_t> resetPackedTime);
  bool RemoveAllRaidLockoutEvents();
  bool UpdateRaidLockoutEventTime(uint32_t mapId, uint32_t difficulty, uint32_t oldPackedTime,
                                  uint32_t newPackedTime);
  void SetRaidResetSchedules(const std::vector<CalendarRaidResetSchedule> &schedules);

  void SetVisibilityFilters(const CalendarVisibilityFilters &filters);

  void ApplyInviteSortRequest(uint32_t criterion, bool toggle_reverse);
  [[nodiscard]] std::string GetSortCriterion() const;
  [[nodiscard]] bool GetSortReverse() const;

  bool SortInvitesByCriterion(uint64_t eventId, int criterion, bool reverse,
                              const WorldSession *session = nullptr);
  void SetPendingInviteListNameQueries(uint64_t eventId, const std::vector<uint64_t> &guids,
                                       CalendarInviteLookupCompletionAction completion_action =
                                           CalendarInviteLookupCompletionAction::kUpdateInviteList);
  void TrackPendingInviteListNameQuery(uint64_t eventId, uint64_t guid,
                                       CalendarInviteLookupCompletionAction completion_action =
                                           CalendarInviteLookupCompletionAction::kUpdateInviteList);
  void ClearPendingInviteListNameQueries();
  [[nodiscard]] bool HasPendingInviteListNameQueries(uint64_t eventId) const;
  [[nodiscard]] std::optional<CalendarPendingInviteNameQueryResolution>
  ResolvePendingInviteListNameQuery(uint64_t guid);
  void TrackPendingEventListNameQuery(uint64_t guid);
  [[nodiscard]] bool ResolvePendingEventListNameQuery(uint64_t guid);

  void SetClipboardEvent(const CalendarContextEventInfo &info);
  void ClearClipboard();
  [[nodiscard]] const CalendarContextEventInfo *GetClipboardEvent() const;
  [[nodiscard]] bool HasClipboardEvent() const;

  void MarkEventActionSent();
  [[nodiscard]] bool CanSendEventAction() const;

  void MarkInviteSent();
  [[nodiscard]] bool CanSendInvite(bool bypass_cooldown = false) const;

  [[nodiscard]] static MonthInfo GetMonthInfo(uint32_t month, uint32_t year);

  void SetEventInviteStatus(uint32_t inviteIndex, uint32_t status);

  [[nodiscard]] static std::string FormatCalendarDateTime(uint32_t packedTime,
                                                          bool use24Hour = false);

  [[nodiscard]] static const char *GetEventTypeString(uint16_t flags);

  [[nodiscard]] static const char *GetHolidayPhaseString(uint8_t flags, int sequenceIndex,
                                                         uint32_t sequenceTotal);

  [[nodiscard]] static const char *GetInviteModStatusString(uint8_t modStatus);

  [[nodiscard]] static bool IsGuildSignupEvent(uint32_t eventFlags, uint8_t typeFlags);

  void SelectInviteByIndex(uint64_t eventId, std::size_t inviteIndex);

  [[nodiscard]] int GetSelectedInviteIndex() const;

  void SetSelectedInviteId(uint64_t inviteId);
  [[nodiscard]] uint64_t GetSelectedInviteId() const;

private:
  CalendarSystem() = default;

  bool initial_snapshot_requested_ = false;

  struct MonthKey {
    uint32_t month, year;
    bool operator==(const MonthKey &o) const {
      return month == o.month && year == o.year;
    }
  };
  struct MonthHash {
    size_t operator()(const MonthKey &k) const {
      return std::hash<uint64_t>()((static_cast<uint64_t>(k.year) << 32) | k.month);
    }
  };

  std::unordered_map<MonthKey, std::vector<CalendarSystemEvent>, MonthHash> month_events_;
  std::unordered_map<uint32_t, std::vector<CalendarSystemEvent>> day_events_;
  std::unordered_map<uint64_t, CalendarSystemEvent> events_;
  std::unordered_map<uint64_t, std::vector<CalendarSystemInvite>> event_invites_;
  std::vector<CalendarHolidaySequenceSource> holiday_sequence_sources_;
  std::vector<CalendarSystemEvent> holiday_occurrences_;
  std::unordered_map<std::uint32_t, CalendarHolidayPresentation> holiday_presentations_;
  std::vector<CalendarSystemInvite> pending_;
  size_t pending_invite_count_ = 0;
  uint32_t view_month_ = 1;
  uint32_t view_year_ = 2009;
  bool has_view_month_selection_ = false;
  bool action_pending_ = false;
  std::optional<std::int64_t> current_time_anchor_ns_since_2000_;
  uint32_t current_time_anchor_tick_ms_ = 0;
  mutable std::mutex mutex_;

  std::optional<CalendarContextEventInfo> opened_event_;

  std::optional<CalendarContextEventInfo> context_menu_event_;

  std::optional<CalendarContextEventInfo> context_event_;

  std::vector<CalendarRaidInfo> raid_infos_;
  std::vector<CalendarRaidResetSchedule> raid_reset_schedules_;

  CalendarVisibilityFilters visibility_filters_{};

  uint64_t pending_invite_name_query_event_id_ = 0;
  std::unordered_set<uint64_t> pending_invite_name_query_guids_;
  std::optional<CalendarInviteLookupCompletionAction> pending_invite_name_query_action_;
  std::unordered_set<uint64_t> pending_event_list_name_query_guids_;
  std::optional<CalendarContextEventInfo> clipboard_event_;

  uint32_t last_event_action_sent_ms_ = 0;

  uint32_t last_invite_sent_ms_ = 0;

  uint64_t selected_invite_id_ = 0;

  void RebuildMonthEventsLocked();
  [[nodiscard]] bool HasPendingInviteEntryLocked(uint64_t eventId) const;
  [[nodiscard]] bool HasPendingInviteForEventLocked(uint64_t eventId) const;
  void ClearSelectionStateForEventLocked(uint64_t eventId);
  void RefreshSelectionStateForEventLocked(const CalendarSystemEvent &event, bool update_title);
};

void ClearCalendarActionPending(CalendarSystem &calendar_system);
[[nodiscard]] bool IsCalendarPendingInviteVisible(const WorldSession &session,
                                                   std::uint64_t sender_guid);

bool SyncActivePlayerCalendarGuildState(WorldSession &session, std::uint32_t guild_id);

}
