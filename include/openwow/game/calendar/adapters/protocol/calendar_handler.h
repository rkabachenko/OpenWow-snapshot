
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "openwow/game/object_guid.h"
#include "openwow/game/packet_reader.h"

namespace openwow::game {

struct CalendarInvite {
  std::uint64_t event_id = 0;
  std::uint64_t invite_id = 0;
  std::uint8_t status = 0;
  std::uint8_t rank = 0;
  std::uint8_t invite_type = 0;
  ObjectGuid sender{ObjectGuid(0)};
};

struct CalendarEvent {
  std::uint64_t event_id = 0;
  std::string title;
  std::uint32_t type = 0;
  std::uint32_t event_time = 0;
  std::uint32_t flags = 0;
  std::int32_t dungeon_id = 0;
  ObjectGuid creator{ObjectGuid(0)};
};

struct CalendarInstanceLockout {
  std::uint32_t map_id = 0;
  std::uint32_t difficulty = 0;
  std::uint32_t reset_time_remaining = 0;
  std::uint64_t instance_id = 0;
};

struct CalendarResetTime {
  std::int32_t map_id = 0;
  std::int32_t period = 0;
  std::int32_t offset = 0;
};

struct CalendarHolidayEntry {
  std::uint32_t holiday_id = 0;
  std::uint32_t selection_mask = 0;
  std::uint32_t loop_mode = 0;
  std::uint32_t priority = 0;
  std::uint32_t calendar_filter_type = 0;
  std::array<std::uint32_t, 26> occurrence_packed_times{};
  std::array<std::uint32_t, 10> sequence_duration_hours{};
  std::array<std::uint32_t, 10> sequence_team_masks{};
  std::string texture_path_override;
};

struct CalendarData {
  std::vector<CalendarInvite> invites;
  std::vector<CalendarEvent> events;
  std::uint32_t server_time = 0;
  std::uint32_t zone_time = 0;
  std::vector<CalendarInstanceLockout> lockouts;
  std::uint32_t relation_time = 0;
  std::vector<CalendarResetTime> reset_times;
  std::vector<CalendarHolidayEntry> holidays;
  std::uint32_t holiday_count = 0;
};

struct CalendarNumPending {
  std::uint32_t pending_invites = 0;
};

struct CalendarCommandResult {
  std::uint32_t command = 0;
  std::string command_context;
  std::string player_name;
  std::uint32_t error = 0;
};

enum class CalendarCommandErrorArgument : std::uint8_t {
  kNone,
  kPlayerName,
  kNumericLimit,
};

struct CalendarCommandErrorDisplay {
  std::string_view localization_key;
  CalendarCommandErrorArgument argument =
      CalendarCommandErrorArgument::kNone;
  std::uint32_t numeric_limit = 0;
};

[[nodiscard]] std::optional<CalendarCommandErrorDisplay>
ResolveCalendarCommandErrorDisplay(std::uint32_t error_code);

struct CalendarEventInvite {
  ObjectGuid invitee{ObjectGuid(0)};
  std::uint64_t event_id = 0;
  std::uint64_t invite_id = 0;
  std::uint8_t level = 0;
  std::uint8_t status = 0;
  std::uint8_t invite_type = 0;
  std::uint32_t response_time = 0;
  std::uint8_t pending_action_result = 0;
};

struct CalendarEventInviteAlert {
  std::uint64_t event_id = 0;
  std::string title;
  std::uint32_t event_time = 0;
  std::uint32_t flags = 0;
  std::uint32_t type = 0;
  std::int32_t dungeon_id = 0;
  std::uint64_t invite_id = 0;
  std::uint8_t status = 0;
  std::uint8_t rank = 0;
  ObjectGuid creator{ObjectGuid(0)};
  ObjectGuid sender{ObjectGuid(0)};
};

struct CalendarEventStatus {
  ObjectGuid invitee{ObjectGuid(0)};
  std::uint64_t event_id = 0;
  std::uint32_t event_time = 0;
  std::uint32_t flags = 0;
  std::uint8_t status = 0;
  std::uint8_t pending_action_result = 0;
  std::uint32_t status_time = 0;
};

struct CalendarRaidLockoutAdded {
  std::uint32_t current_time = 0;
  std::uint32_t map_id = 0;
  std::uint32_t difficulty = 0;
  std::uint32_t reset_time_remaining = 0;
  std::uint64_t instance_id = 0;
};

struct CalendarEventInviteRemoved {
  ObjectGuid invitee{ObjectGuid(0)};
  std::uint64_t event_id = 0;
  std::uint32_t flags = 0;
  std::uint8_t pending_action_result = 0;
};

struct CalendarEventInviteRemovedAlert {
  std::uint64_t event_id = 0;
  std::uint32_t date = 0;
  std::uint32_t flags = 0;
  std::uint8_t status = 0;
};

struct CalendarEventInviteStatusAlert {
  std::uint64_t event_id = 0;
  std::uint32_t date = 0;
  std::uint32_t flags = 0;
  std::uint8_t status = 0;
};

struct CalendarEventModeratorStatusAlert {
  ObjectGuid invitee{ObjectGuid(0)};
  std::uint64_t event_id = 0;
  std::uint8_t rank = 0;
  std::uint8_t pending_action_result = 0;
};

struct CalendarEventRemovedAlert {
  std::uint8_t pending_action_result = 0;
  std::uint64_t event_id = 0;
  std::string title;
  std::uint32_t date = 0;
};

struct CalendarEventUpdatedAlert {
  std::uint8_t pending_action_result = 0;
  std::uint64_t event_id = 0;
  std::uint32_t original_date = 0;
  std::uint32_t flags = 0;
  std::uint32_t new_date = 0;
  std::uint8_t event_type = 0;
  std::uint32_t dungeon_id = 0;
  std::string title;
  std::string description;
  std::uint8_t repeat_option = 0;
  std::uint32_t max_invites = 0;
  std::uint32_t second_packed_time = 0;
};

struct CalendarInviteListEntry {
  ObjectGuid guid{ObjectGuid(0)};
  std::uint8_t level = 0;
};

struct CalendarRaidLockoutRemoved {
  std::int32_t map_id = 0;
  std::int32_t difficulty = 0;
  std::uint32_t reset_time = 0;
  std::uint64_t instance_id = 0;
};

struct CalendarRaidLockoutUpdated {
  std::uint32_t current_time = 0;
  std::uint32_t map_id = 0;
  std::uint32_t difficulty = 0;
  std::uint32_t old_time = 0;
  std::uint32_t new_time = 0;
};

struct CalendarEventInviteNotes {
  ObjectGuid invitee{ObjectGuid(0)};
  std::uint64_t event_id = 0;
  std::string notes;
  std::uint8_t pending_action_result = 0;
};

struct CalendarEventInviteNotesAlert {
  std::uint64_t event_id = 0;
  std::string notes;
};

struct CalendarSendEventInvite {
  ObjectGuid invitee{ObjectGuid(0)};
  std::uint8_t level = 0;
  std::uint8_t status = 0;
  std::uint8_t rank = 0;
  std::uint8_t invite_type = 0;
  std::uint64_t invite_id = 0;
  std::uint32_t status_time = 0;
  std::string notes;
};

struct CalendarSendEvent {
  std::uint8_t send_type = 0;
  ObjectGuid creator{ObjectGuid(0)};
  std::uint64_t event_id = 0;
  std::string title;
  std::string description;
  std::uint8_t event_type = 0;
  std::uint8_t repeat_option = 0;
  std::uint32_t max_invites = 0;
  std::int32_t dungeon_id = 0;
  std::uint32_t flags = 0;
  std::uint32_t event_time = 0;
  std::uint32_t unk_time = 0;
  std::uint64_t guild_id = 0;
  std::vector<CalendarSendEventInvite> invites;
};

class CalendarHandler {
public:
  bool HandleSendCalendar(const std::uint8_t *data, std::size_t len);
  bool HandleSendEvent(const std::uint8_t *data, std::size_t len);
  bool HandleSendNumPending(const std::uint8_t *data, std::size_t len);
  bool HandleCommandResult(const std::uint8_t *data, std::size_t len);
  bool HandleEventInvite(const std::uint8_t *data, std::size_t len);
  bool HandleEventInviteAlert(const std::uint8_t *data, std::size_t len);
  bool HandleEventStatus(const std::uint8_t *data, std::size_t len);
  bool HandleRaidLockoutAdded(const std::uint8_t *data, std::size_t len);
  bool HandleEventInviteRemoved(const std::uint8_t *data, std::size_t len);
  bool HandleEventInviteRemovedAlert(const std::uint8_t *data, std::size_t len);
  bool HandleEventInviteStatusAlert(const std::uint8_t *data, std::size_t len);
  bool HandleEventModeratorStatusAlert(const std::uint8_t *data, std::size_t len);
  bool HandleEventRemovedAlert(const std::uint8_t *data, std::size_t len);
  bool HandleEventUpdatedAlert(const std::uint8_t *data, std::size_t len);
  bool HandleClearPendingAction(const std::uint8_t *data, std::size_t len);
  bool HandleFilterGuild(const std::uint8_t *data, std::size_t len);
  bool HandleArenaTeam(const std::uint8_t *data, std::size_t len);
  bool HandleRaidLockoutRemoved(const std::uint8_t *data, std::size_t len);
  bool HandleRaidLockoutUpdated(const std::uint8_t *data, std::size_t len);
  bool HandleEventInviteNotes(const std::uint8_t *data, std::size_t len);
  bool HandleEventInviteNotesAlert(const std::uint8_t *data, std::size_t len);

  const CalendarData &calendar_data() const {
    return calendar_data_;
  }
  const CalendarNumPending &num_pending() const {
    return num_pending_;
  }
  const CalendarCommandResult &last_command_result() const {
    return last_command_result_;
  }
  const CalendarEventInvite &last_invite() const {
    return last_invite_;
  }
  const CalendarEventInviteAlert &last_invite_alert() const {
    return last_invite_alert_;
  }
  const CalendarEventStatus &last_event_status() const {
    return last_event_status_;
  }
  const CalendarRaidLockoutAdded &last_lockout_added() const {
    return last_lockout_added_;
  }
  [[nodiscard]] const std::optional<CalendarEventInviteRemoved> &last_invite_removed() const {
    return last_invite_removed_;
  }
  [[nodiscard]] const std::optional<CalendarEventInviteRemovedAlert> &
  last_invite_removed_alert() const {
    return last_invite_removed_alert_;
  }
  [[nodiscard]] const std::optional<CalendarEventInviteStatusAlert> &
  last_invite_status_alert() const {
    return last_invite_status_alert_;
  }
  [[nodiscard]] const std::optional<CalendarEventModeratorStatusAlert> &
  last_moderator_status_alert() const {
    return last_moderator_status_alert_;
  }
  [[nodiscard]] const std::optional<CalendarEventRemovedAlert> &last_event_removed_alert() const {
    return last_event_removed_alert_;
  }
  [[nodiscard]] const std::optional<CalendarEventUpdatedAlert> &last_event_updated_alert() const {
    return last_event_updated_alert_;
  }
  [[nodiscard]] bool clear_pending_action() const {
    return clear_pending_action_;
  }
  [[nodiscard]] const std::vector<CalendarInviteListEntry> &invite_list_entries() const {
    return invite_list_entries_;
  }
  [[nodiscard]] const std::optional<CalendarRaidLockoutRemoved> &last_lockout_removed() const {
    return last_lockout_removed_;
  }
  [[nodiscard]] const std::optional<CalendarRaidLockoutUpdated> &last_lockout_updated() const {
    return last_lockout_updated_;
  }
  [[nodiscard]] const std::optional<CalendarEventInviteNotes> &last_invite_notes() const {
    return last_invite_notes_;
  }
  [[nodiscard]] const std::optional<CalendarEventInviteNotesAlert> &
  last_invite_notes_alert() const {
    return last_invite_notes_alert_;
  }
  [[nodiscard]] const std::optional<CalendarSendEvent> &last_send_event() const {
    return last_send_event_;
  }

  void Clear();

private:
  CalendarData calendar_data_{};
  CalendarNumPending num_pending_{};
  CalendarCommandResult last_command_result_{};
  CalendarEventInvite last_invite_{};
  CalendarEventInviteAlert last_invite_alert_{};
  CalendarEventStatus last_event_status_{};
  CalendarRaidLockoutAdded last_lockout_added_{};
  std::optional<CalendarEventInviteRemoved> last_invite_removed_;
  std::optional<CalendarEventInviteRemovedAlert> last_invite_removed_alert_;
  std::optional<CalendarEventInviteStatusAlert> last_invite_status_alert_;
  std::optional<CalendarEventModeratorStatusAlert> last_moderator_status_alert_;
  std::optional<CalendarEventRemovedAlert> last_event_removed_alert_;
  std::optional<CalendarEventUpdatedAlert> last_event_updated_alert_;
  bool clear_pending_action_ = false;
  std::vector<CalendarInviteListEntry> invite_list_entries_;
  std::optional<CalendarRaidLockoutRemoved> last_lockout_removed_;
  std::optional<CalendarRaidLockoutUpdated> last_lockout_updated_;
  std::optional<CalendarEventInviteNotes> last_invite_notes_;
  std::optional<CalendarEventInviteNotesAlert> last_invite_notes_alert_;
  std::optional<CalendarSendEvent> last_send_event_;
};

}
