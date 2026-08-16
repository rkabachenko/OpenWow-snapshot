#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"

#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/core/storm_string.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/chat_display.h"
#include "openwow/game/localization.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/game/tooltip_system.h"
#include "openwow/ui/game/ui_error_manager.h"

#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace openwow::ui::game {
namespace {

namespace game = ::openwow::game;

enum class SystemMessageRoute : std::uint8_t {
  kChatFrame,
  kUiInfo,
  kUiError,
  kConsole,
};

struct SystemMessageCatalogEntry {
  const char* global_string_key = nullptr;
  SystemMessageRoute route = SystemMessageRoute::kUiError;
  const char* sound_name = nullptr;
  std::uint32_t sound_mode = 0;
  std::uint32_t chat_type = 0;
};

constexpr std::size_t kSystemMessageCatalogSize = 730;
constexpr std::size_t kLastSystemMessageBufferSize = 3000;
constexpr std::uint32_t kNamedSystemMessageSoundMode = 68;

char g_last_system_message_text[kLastSystemMessageBufferSize] = {};

const auto kSystemMessageCatalog = [] {
  std::array<SystemMessageCatalogEntry, kSystemMessageCatalogSize> catalog{{
#include "openwow/ui/surfaces/game/runtime/retail_system_message_catalog.inl"
  }};
  return catalog;
}();

const SystemMessageCatalogEntry* LookupSystemMessageDescriptor(
    const int message_index) {
  if (message_index < 0 ||
      static_cast<std::size_t>(message_index) >= kSystemMessageCatalog.size()) {
    return nullptr;
  }
  return &kSystemMessageCatalog[static_cast<std::size_t>(message_index)];
}

const game::WorldSession* GetActiveSystemMessageSession() {
  const auto* manager = runtime::WorldUiRuntimeContext::FromActiveLua();
  return manager != nullptr ? manager->world_session() : nullptr;
}

const game::CGPlayer_C* GetActiveSystemMessagePlayer() {
  const auto* session = GetActiveSystemMessageSession();
  return session != nullptr ? session->objects().GetActivePlayer() : nullptr;
}

std::string ResolveSystemMessageFormat(
    const SystemMessageCatalogEntry& descriptor) {
  if (descriptor.global_string_key == nullptr ||
      descriptor.global_string_key[0] == '\0') {
    return {};
  }

  const auto* player = GetActiveSystemMessagePlayer();
  const int gender = player != nullptr && player->State().GetGender() == 1 ? 3 : 0;
  const auto* lua_state = ScriptEventDispatch::Get().GetLuaState();
  if (lua_state != nullptr) {
    auto resolved = game::ResolveLocalizedGlobalString(
        const_cast<lua_State*>(lua_state), descriptor.global_string_key, -1,
        gender);
    if (!resolved.empty()) {
      return resolved;
    }
  }

  auto& localization = game::Localization::Get();
  auto resolved = localization.GetString(descriptor.global_string_key,
                                         descriptor.global_string_key);
  if (player != nullptr && player->State().GetGender() == 1) {
    const std::string female_key =
        std::string(descriptor.global_string_key) + "_FEMALE";
    if (localization.HasString(female_key)) {
      resolved = localization.GetString(female_key, resolved);
    }
  }
  return resolved;
}

std::string FormatSystemMessage(const char* format, va_list arguments) {
  if (format == nullptr || format[0] == '\0') {
    return {};
  }

  std::array<char, kLastSystemMessageBufferSize> buffer{};
  va_list arguments_copy;
  va_copy(arguments_copy, arguments);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
  openwow::core::SStrPrintfV(buffer.data(), buffer.size(), format, arguments_copy);
#pragma clang diagnostic pop
  va_end(arguments_copy);
  return buffer.data();
}

void PlayNamedSystemMessageSound(const char* sound_name) {
  if (sound_name == nullptr || sound_name[0] == '\0' ||
      openwow::core::SStrCmpNoCase(sound_name, "NONE", 0x7FFFFFFFu) == 0) {
    return;
  }

  auto* session = GetActiveSystemMessageSession();
  const auto* dbc =
      session != nullptr ? session->GetDbcLoader()
                         : TooltipSystem::Get().GetDbcLoader();
  if (dbc == nullptr) {
    return;
  }

  const auto* entry =
      dbc->sound_entries().LookupByNameCaseInsensitive(sound_name);
  if (entry != nullptr) {
    if (session == nullptr) return;
    (void)session->sound_runtime().PlaySoundKit(
        entry->id, nullptr, nullptr);
  }
}

std::uint32_t ResolveSystemMessageSpeechSoundKit(
    const SystemMessageCatalogEntry& descriptor) {
  if (descriptor.sound_mode >= kNamedSystemMessageSoundMode) {
    return 0;
  }

  const auto* player = GetActiveSystemMessagePlayer();
  if (player == nullptr) {
    return 0;
  }

  const auto* session = GetActiveSystemMessageSession();
  if (session == nullptr) return 0;
  return session->sound_runtime()
      .GetEmotesTextSoundTable()
      .Lookup(descriptor.sound_mode, player->State().GetRace(), player->State().GetGender());
}

void PlaySystemMessageAudio(const SystemMessageCatalogEntry& descriptor) {
  if (descriptor.sound_mode == kNamedSystemMessageSoundMode) {
    PlayNamedSystemMessageSound(descriptor.sound_name);
    return;
  }
  if (!CVarSystem::Instance().GetCVarBool("Sound_EnableErrorSpeech")) {
    return;
  }

  const auto sound_kit_id = ResolveSystemMessageSpeechSoundKit(descriptor);
  if (sound_kit_id == 0) {
    return;
  }

  auto* session = GetActiveSystemMessageSession();
  if (session == nullptr) return;
  session->sound_runtime().PlayErrorSpeech(sound_kit_id);
}

void DispatchSystemMessage(const SystemMessageCatalogEntry& descriptor,
                           const std::string& message) {
  if (message.empty()) {
    return;
  }

  switch (descriptor.route) {
    case SystemMessageRoute::kChatFrame:
      if (const auto* session = GetActiveSystemMessageSession();
          session != nullptr) {
      game::ChatFrame_DisplayMessage(
          session->objects(), message.c_str(), static_cast<int>(descriptor.chat_type), nullptr, 0,
          nullptr, nullptr, nullptr, 0, 0, 0, 0, 0, nullptr);
      }
      return;
    case SystemMessageRoute::kUiInfo:
      openwow::ui::UIErrorManager::Get().AddInfoMessage(message);
      ScriptEventDispatch::Get().FireUiInfoMessage(message);
      return;
    case SystemMessageRoute::kUiError:
      openwow::ui::UIErrorManager::Get().AddErrorMessage(message);
      ScriptEventDispatch::Get().FireUiErrorMessage(message);
      return;
    case SystemMessageRoute::kConsole:
      return;
  }
}

}

const char* GetLastSystemMessageText() {
  return g_last_system_message_text;
}

void DisplaySystemMessage(const int message_index, ...) {
  const auto* descriptor = LookupSystemMessageDescriptor(message_index);
  if (descriptor == nullptr) {
    return;
  }

  const auto format = ResolveSystemMessageFormat(*descriptor);
  if (format.empty()) {
    return;
  }

  PlaySystemMessageAudio(*descriptor);

  va_list arguments;
  va_start(arguments, message_index);
  const auto message = FormatSystemMessage(format.c_str(), arguments);
  va_end(arguments);

  openwow::core::SStrCopy(g_last_system_message_text, message.c_str(),
                          kLastSystemMessageBufferSize);
  DispatchSystemMessage(*descriptor, message);
}

void PetNameCache_HandlePetRenameResult(const int result_code) {
  int message_index = 598;
  switch (result_code) {
    case 2:
      message_index = 599;
      break;
    case 3:
      message_index = 600;
      break;
    case 4:
      message_index = 601;
      break;
    case 6:
      message_index = 602;
      break;
    case 7:
      message_index = 603;
      break;
    case 8:
      message_index = 604;
      break;
    case 11:
      message_index = 605;
      break;
    case 12:
      message_index = 606;
      break;
    case 13:
      message_index = 607;
      break;
    case 14:
      message_index = 608;
      break;
    case 15:
      message_index = 609;
      break;
    case 16:
      message_index = 610;
      break;
    default:
      break;
  }
  DisplaySystemMessage(message_index);
}

void DisplaySetPlayerDeclinedNamesResult(const unsigned int result_index) {
  static constexpr std::array<int, 4> kMessageIndices = {221, 222, 223, 730};
  if (result_index < kMessageIndices.size()) {
    DisplaySystemMessage(kMessageIndices[result_index]);
  }
}

}
