
#include "openwow/game/cgunit_guild_tabard_callback.h"

#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgunit.h"

namespace openwow::game {

bool CGUnit_GuildCacheCallback_RefreshTabardTextures(
    const std::uint32_t guild_id, const ObjectGuid& unit_guid,
    const GuildManager& guild_manager, ObjectManager& object_manager,
    const bool resolved) {
  if (guild_id == 0) {
    return false;
  }

  if (!resolved) {
    return false;
  }

  const GuildInfo* info = guild_manager.FindCachedGuildInfo(guild_id);
  if (info == nullptr) {
    return false;
  }

  CGUnit_C* unit = object_manager.GetMutableUnit(unit_guid);
  if (unit == nullptr) {
    return false;
  }

  if (!HasResolvedGuildEmblem(info->emblem)) {
    return false;
  }

  if (!unit->Presentation().HasCharacterModelVisual()) {
    return false;
  }

  unit->Presentation().SetCharacterVisualTabardEmblem(info->emblem);
  return true;
}

}
