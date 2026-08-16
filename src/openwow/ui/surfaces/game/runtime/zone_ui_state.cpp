#include "openwow/ui/surfaces/game/runtime/zone_ui_state.h"

#include "openwow/data/streaming_init.h"
#include "openwow/game/channel_manager.h"
#include "openwow/game/chat_cache.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/world_scene_state.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/game_ui_core.h"
#include "openwow/ui/game/minimap_system.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/game/world_map_system.h"

#include <algorithm>

namespace openwow::ui::game {
namespace {

bool AssignZoneText(std::string &destination, const std::string_view source) {
  if (destination == source) {
    return false;
  }

  destination.assign(source);
  return true;
}

std::uint32_t NonNegativeId(const std::int32_t value) {
  return static_cast<std::uint32_t>(std::max(value, 0));
}

}

void ZoneUiState::Apply(const ZoneUiUpdate &update, openwow::game::WorldSession *session) {
  const bool area_changed = location_.area != update.location.area;
  const bool was_first_zone = location_.area == AreaId{};
  location_ = update.location;

  if (was_first_zone && session != nullptr) {
    (void)world_map_.UpdatePlayerPosition(*session);
  }

  const bool zone_text_changed = AssignZoneText(zone_text_, update.text.zone);
  const bool subzone_text_changed = AssignZoneText(subzone_text_, update.text.subzone);
  const bool text_changed = zone_text_changed || subzone_text_changed;

  if (!update.text.real_zone.empty() && real_zone_text_ != update.text.real_zone) {
    real_zone_text_.assign(update.text.real_zone);
    if ((session == nullptr ||
         !session->spell_visual().cinematic_active()) &&
        (session == nullptr || !openwow::game::CGPlayer_C_IsLoading(*session))) {
      openwow::game::ChannelManager::Get().AutoJoinDefaults(
          static_cast<std::uint32_t>(location_.area.value()));
    }
  }

  const std::string &minimap_text = subzone_text_.empty() ? zone_text_ : subzone_text_;
  if (session != nullptr) {
    auto& scene_state = session->scene_state();
    scene_state.SetMapId(NonNegativeId(location_.map.value()));
    scene_state.SetZoneId(NonNegativeId(location_.zone.value()));
    scene_state.SetSubZoneId(NonNegativeId(location_.area.value()));
    scene_state.SetZoneText(zone_text_);
    scene_state.SetSubZoneText(subzone_text_);
    scene_state.SetRealZoneText(real_zone_text_);
    scene_state.SetMinimapZoneText(minimap_text);
    openwow::game::TrySyncLoadedChatChannels(*session);
  }
  minimap_.SetZoneText(zone_text_, subzone_text_);

  auto &dispatch = ScriptEventDispatch::Get();
  if (area_changed) {
    dispatch.FireZoneChangedNewArea();
  } else if (text_changed) {
    if (update.is_instance) {
      dispatch.FireSubzoneChanged();
    } else {
      dispatch.FireZoneChanged();
    }
  }

  if (openwow::data::IsOnlineModeActive() && (area_changed || text_changed)) {
    GameUI_SetBackgroundZoneState(location_.area.value(), location_.zone.value());
  }

  dispatch.FireEvent(events::SPELL_UPDATE_USABLE);
}

void ZoneUiState::Reset() {
  location_ = ZoneUiLocation{};
  zone_text_.clear();
  subzone_text_.clear();
  real_zone_text_.clear();

  minimap_.SetZoneText({}, {});
}

}
