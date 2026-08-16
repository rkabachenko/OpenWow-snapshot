
#include "openwow/game/pet_equip_suppress_aura.h"

#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgplayer.h"

namespace openwow::game {

bool HasActivePlayerPetEquipSuppressAura(const ObjectManager &obj_mgr) {

  const auto *player = obj_mgr.GetActivePlayer();
  if (!player)
    return false;

  if (!player->IsPlayer())
    return false;
  return player->Auras().TestConditionBit(kAuraConditionBit_PetEquipSuppressed);
}

}
