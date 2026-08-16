#include "openwow/ui/surfaces/game/adapters/held_cursor/held_cursor_presentation.h"

#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/ui/cursor_gx_system.h"
#include "openwow/ui/game/script_event_dispatch.h"

#include <string_view>

namespace openwow::ui::game {
namespace {

openwow::ui::CursorRuntimeTextureMode ToRuntimeTextureMode(
    const openwow::game::actions::held_cursor::TextureMode mode) {
  using TextureMode = openwow::game::actions::held_cursor::TextureMode;
  switch (mode) {
    case TextureMode::None:
      return openwow::ui::CursorRuntimeTextureMode::kNone;
    case TextureMode::HeldTexture:
      return openwow::ui::CursorRuntimeTextureMode::kHeldTexture;
    case TextureMode::CopiedPath:
      return openwow::ui::CursorRuntimeTextureMode::kCopiedPath;
    case TextureMode::DirectPath:
      return openwow::ui::CursorRuntimeTextureMode::kDirectPath;
  }
  return openwow::ui::CursorRuntimeTextureMode::kNone;
}

std::string_view SoundKitName(
    const openwow::game::actions::held_cursor::Sound sound) {
  using Sound = openwow::game::actions::held_cursor::Sound;
  switch (sound) {
    case Sound::CursorGrabObject:
      return "INTERFACESOUND_CURSORGRABOBJECT";
    case Sound::CursorDropObject:
      return "INTERFACESOUND_CURSORDROPOBJECT";
    case Sound::Coins:
      return "LOOTWINDOWCOINSOUND";
    case Sound::SpellbookPickup:
      return "igSpellBookSpellIconPickup";
    case Sound::SpellbookDrop:
      return "igSpellBookSpellIconDrop";
    case Sound::None:
      return {};
  }
  return {};
}

}

void HeldCursorPresentation::ClearHeldOverlay() {
  openwow::ui::ClearRuntimeCursorTexture();
}

void HeldCursorPresentation::PresentHeldOverlay(
    std::string texture_path,
    const openwow::game::actions::held_cursor::TextureMode mode) {
  openwow::ui::Cursor_LoadRuntimeTexturePath(texture_path,
                                             ToRuntimeTextureMode(mode));
}

void HeldCursorPresentation::PlaySound(
    const openwow::game::actions::held_cursor::Sound sound) {
  if (const auto name = SoundKitName(sound); !name.empty()) {
    static_cast<void>(sound_.PlaySoundKitByName(name));
  }
}

void HeldCursorPresentation::ShowGrid(
    const openwow::game::actions::held_cursor::Grid grid) {
  using Grid = openwow::game::actions::held_cursor::Grid;
  switch (grid) {
    case Grid::ActionBar:
      events_.FireActionbarShowGrid();
      break;
    case Grid::PetBar:
      events_.FirePetBarShowGrid();
      break;
    case Grid::None:
      break;
  }
}

void HeldCursorPresentation::HideGrid(
    const openwow::game::actions::held_cursor::Grid grid) {
  using Grid = openwow::game::actions::held_cursor::Grid;
  switch (grid) {
    case Grid::ActionBar:
      events_.FireActionbarHideGrid();
      break;
    case Grid::PetBar:
      events_.FirePetBarHideGrid();
      break;
    case Grid::None:
      break;
  }
}

void HeldCursorPresentation::PublishCursorUpdate() {
  events_.FireCursorUpdate();
}

}
