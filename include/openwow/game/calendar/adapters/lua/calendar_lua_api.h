
#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaOpenCalendar(lua_State* L);
int LuaCalendarGetNumDayEvents(lua_State* L);
int LuaCalendarGetDayEvent(lua_State* L);
int LuaCalendarGetMonth(lua_State* L);
int LuaCalendarSetMonth(lua_State* L);
int LuaCalendarGetMinDate(lua_State* L);
int LuaCalendarGetMaxDate(lua_State* L);
int LuaCalendarGetDate(lua_State* L);
int LuaCalendarGetNumPendingInvites(lua_State* L);
int LuaCalendarGetAbsMonth(lua_State* L);
int LuaCalendarSetAbsMonth(lua_State* L);
int LuaCalendarCanAddEvent(lua_State* L);
int LuaCalendarNewEvent(lua_State* L);
int LuaCalendarNewGuildAnnouncement(lua_State* L);
int LuaCalendarNewGuildEvent(lua_State* L);
int LuaCalendarGetEventIndex(lua_State* L);
int LuaCalendarGetEventInfo(lua_State* L);

int LuaCalendarAddEvent(lua_State* L);
int LuaCalendarCloseEvent(lua_State* L);
int LuaCalendarContextDeselectEvent(lua_State* L);
int LuaCalendarContextEventClipboard(lua_State* L);
int LuaCalendarContextEventComplain(lua_State* L);
int LuaCalendarContextEventCopy(lua_State* L);
int LuaCalendarContextEventGetCalendarType(lua_State* L);
int LuaCalendarContextEventPaste(lua_State* L);
int LuaCalendarContextEventRemove(lua_State* L);
int LuaCalendarContextEventSignUp(lua_State* L);
int LuaCalendarContextInviteAvailable(lua_State* L);
int LuaCalendarContextInviteModeratorStatus(lua_State* L);
int LuaCalendarContextInviteRemove(lua_State* L);
int LuaCalendarContextInviteTentative(lua_State* L);
int LuaCalendarContextSelectEvent(lua_State* L);
int LuaCalendarContextGetEventIndex(lua_State* L);
int LuaCalendarEventAvailable(lua_State* L);
int LuaCalendarEventCanEdit(lua_State* L);
int LuaCalendarEventClearAutoApprove(lua_State* L);
int LuaCalendarEventClearLocked(lua_State* L);
int LuaCalendarEventClearModerator(lua_State* L);
int LuaCalendarEventDecline(lua_State* L);
int LuaCalendarEventSetAutoApprove(lua_State* L);
int LuaCalendarEventSetDate(lua_State* L);
int LuaCalendarEventSetSize(lua_State* L);
int LuaCalendarEventSetDescription(lua_State* L);
int LuaCalendarEventSetLocked(lua_State* L);
int LuaCalendarEventSetModerator(lua_State* L);
int LuaCalendarEventSetRepeatOption(lua_State* L);
int LuaCalendarEventSetTextureID(lua_State* L);
int LuaCalendarEventSetTime(lua_State* L);
int LuaCalendarEventSetTitle(lua_State* L);
int LuaCalendarEventSetType(lua_State* L);
int LuaCalendarEventSignUp(lua_State* L);
int LuaCalendarEventSortInvites(lua_State* L);
int LuaCalendarEventTentative(lua_State* L);
int LuaCalendarEventInvite(lua_State* L);
int LuaCalendarEventRemoveInvite(lua_State* L);
int LuaCalendarEventSelectInvite(lua_State* L);
int LuaCalendarIsActionPending(lua_State* L);
int LuaCalendarMassInviteArenaTeam(lua_State* L);
int LuaCalendarMassInviteGuild(lua_State* L);
int LuaCalendarDefaultGuildFilter(lua_State* L);
int LuaCalendarOpenEvent(lua_State* L);
int LuaCalendarRemoveEvent(lua_State* L);
int LuaCalendarUpdateEvent(lua_State* L);
int LuaCalendarEventGetCalendarType(lua_State* L);
int LuaCalendarEventGetInvite(lua_State* L);
int LuaCalendarEventGetInviteResponseTime(lua_State* L);
int LuaCalendarEventGetInviteSortCriterion(lua_State* L);
int LuaCalendarEventGetNumInvites(lua_State* L);
int LuaCalendarEventGetSelectedInvite(lua_State* L);
int LuaCalendarEventGetStatusOptions(lua_State* L);
int LuaCalendarEventGetTextures(lua_State* L);
int LuaCalendarEventGetTypes(lua_State* L);
int LuaCalendarEventHasPendingInvite(lua_State* L);
int LuaCalendarGetMaxCreateDate(lua_State* L);
int LuaCalendarGetMonthNames(lua_State* L);
int LuaCalendarGetWeekdayNames(lua_State* L);

int LuaCalendarContextEventCanComplain(lua_State* L);
int LuaCalendarContextEventCanEdit(lua_State* L);
int LuaCalendarContextInviteDecline(lua_State* L);
int LuaCalendarContextInviteIsPending(lua_State* L);
int LuaCalendarContextInviteStatus(lua_State* L);
int LuaCalendarContextInviteType(lua_State* L);
int LuaCalendarEventCanModerate(lua_State* L);
int LuaCalendarEventSetLockoutDate(lua_State* L);
int LuaCalendarEventSetStatus(lua_State* L);
int LuaCalendarGetDayEventSequenceInfo(lua_State* L);
int LuaCalendarGetHolidayInfo(lua_State* L);
int LuaCalendarGetRaidInfo(lua_State* L);

int LuaCalendarGetMinHistoryDate(lua_State* L);
int LuaCalendarEventIsModerator(lua_State* L);
int LuaCalendarEventGetRepeatOptions(lua_State* L);
int LuaCalendarEventHaveSettingsChanged(lua_State* L);
int LuaCalendarCanSendInvite(lua_State* L);
int LuaCalendarGetFirstPendingInvite(lua_State* L);
int LuaCalendarEventSetLockoutTime(lua_State* L);

}
