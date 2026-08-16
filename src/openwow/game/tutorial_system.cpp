
#include "openwow/game/tutorial_system.h"

#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/misc_handler.h"
#include "openwow/game/versioned_base93_cvar_codec.h"
#include "openwow/net/client_services_packet_sender.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/game/tooltip_system.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

extern "C" {
#include <lua.hpp>
}

namespace openwow::game {

namespace {

constexpr std::string_view kFlaggedTutorialsCVarName = "flaggedTutorials";

}

TutorialSystem& TutorialSystem::Instance() {
  static TutorialSystem instance;
  return instance;
}

int TutorialSystem::GetNextCompletedTutorial(const int tutorial_id) const {
  bool found = false;
  for (int i = 0; i < kMaxFlaggedTutorials; ++i) {
    if (found && flagged_tutorials_[i] != 0) {
      return flagged_tutorials_[i];
    }
    if (flagged_tutorials_[i] == tutorial_id) {
      found = true;
    }
  }
  return kMaxFlaggedTutorials;
}

int TutorialSystem::GetPrevCompletedTutorial(const int tutorial_id) const {

  constexpr int kInitializedTutorialBitCount =
      static_cast<int>(kTutorialFlagWordCount * 32u);
  const int adjacent_bit_count =
      completed_bits_initialized_ ? kInitializedTutorialBitCount : 0;
  bool found = adjacent_bit_count == tutorial_id;
  for (int i = kMaxFlaggedTutorials - 1; i >= 0; --i) {
    if (found && flagged_tutorials_[i] != 0) {
      return flagged_tutorials_[i];
    }
    if (flagged_tutorials_[i] == tutorial_id) {
      found = true;
    }
  }
  return 0;
}

void TutorialSystem::SaveFlaggedTutorials() {
  std::vector<std::uint32_t> flagged_ids;
  flagged_ids.reserve(kMaxFlaggedTutorials);

  for (const int flagged_tutorial : flagged_tutorials_) {
    if (flagged_tutorial == 0) {
      break;
    }
    flagged_ids.push_back(static_cast<std::uint32_t>(flagged_tutorial));
  }

  openwow::ui::game::CVarSystem::Instance().SetCVar(
      std::string(kFlaggedTutorialsCVarName),
      detail::EncodeVersionedBase93Values(flagged_ids), true);
}

void TutorialSystem::LoadFlaggedTutorials() {
  flagged_tutorials_.fill(0);

  auto& cvars = openwow::ui::game::CVarSystem::Instance();
  const std::string encoded =
      cvars.GetCVar(std::string(kFlaggedTutorialsCVarName));
  if (detail::VersionedBase93NeedsCanonicalRewrite(encoded)) {
    const auto decoded = detail::DecodeVersionedBase93Payload(
        detail::GetVersionedBase93Payload(encoded));
    cvars.SetCVar(std::string(kFlaggedTutorialsCVarName),
                  detail::EncodeVersionedBase93Values(decoded), true);
  }

  const auto decoded = detail::DecodeVersionedBase93Payload(
      detail::GetVersionedBase93Payload(encoded));
  const auto copy_count = std::min(decoded.size(), flagged_tutorials_.size());
  for (std::size_t i = 0; i < copy_count; ++i) {
    flagged_tutorials_[i] = static_cast<int>(decoded[i]);
  }
}

void TutorialSystem::InitializeFromServerFlags(
    const std::span<const std::uint32_t> flags) {
  seen_bits_.fill(0);
  completed_bits_.fill(0);

  const auto copy_count = std::min(flags.size(), seen_bits_.size());
  std::copy_n(flags.begin(), copy_count, seen_bits_.begin());
  std::copy_n(flags.begin(), copy_count, completed_bits_.begin());

  seen_bits_initialized_ = true;
  completed_bits_initialized_ = true;
}

void TutorialSystem::ClearTutorials() {
  if (seen_bits_initialized_) {
    seen_bits_.fill(0xFFFFFFFF);
  }
  if (completed_bits_initialized_) {
    completed_bits_.fill(0xFFFFFFFF);
  }

  if (!completed_bits_initialized_) {
    for (std::uint32_t tutorial_index = 0;
         tutorial_index < static_cast<std::uint32_t>(kMaxFlaggedTutorials);
         ++tutorial_index) {
      QueueFlaggedTutorialId(tutorial_index + 1);
    }
  }

  (void)openwow::net::ClientServices__SendPacket(
      MiscHandler::BuildTutorialClear());
}

void TutorialSystem::ResetTutorials() {
  if (seen_bits_initialized_) {
    seen_bits_.fill(0);
  }
  if (completed_bits_initialized_) {
    completed_bits_.fill(0);
  }
  flagged_tutorials_.fill(0);

  (void)openwow::net::ClientServices__SendPacket(
      MiscHandler::BuildTutorialReset());
}

bool TutorialSystem::CanResetTutorials() const {
  if (!seen_bits_initialized_) {
    return false;
  }

  return std::any_of(seen_bits_.begin(), seen_bits_.end(),
                     [](const std::uint32_t word) { return word != 0; });
}

void TutorialSystem::TriggerTutorial(std::uint32_t tutorial_index) {
  if (!seen_bits_initialized_) return;

  const auto word_idx = tutorial_index >> 5;
  const auto bit = 1u << (tutorial_index & 0x1F);

  if (word_idx >= seen_bits_.size()) return;
  if (seen_bits_[word_idx] & bit) return;

  const bool show_tutorials =
      openwow::ui::game::CVarSystem::Instance().GetCVarBool("showTutorials");
  if (show_tutorials) {
    PlayNamedTutorialSound("TutorialPopup");
    openwow::ui::game::ScriptEventDispatch::Get().FireEventArgs(
        "TUTORIAL_TRIGGER", {static_cast<int>(tutorial_index + 1)});
    seen_bits_[word_idx] |= bit;
    return;
  }

  seen_bits_[word_idx] |= bit;
  FlagTutorial(tutorial_index);
}

void TutorialSystem::FlagTutorial(std::uint32_t tutorial_index) {
  if (!seen_bits_initialized_ || !completed_bits_initialized_) {
    return;
  }

  const auto word_idx = tutorial_index >> 5;
  if (word_idx >= seen_bits_.size() || word_idx >= completed_bits_.size()) {
    return;
  }

  const auto bit = 1u << (tutorial_index & 0x1F);
  if ((completed_bits_[word_idx] & bit) != 0) {
    return;
  }

  seen_bits_[word_idx] |= bit;
  completed_bits_[word_idx] |= bit;
  QueueFlaggedTutorialId(tutorial_index + 1);
  (void)openwow::net::ClientServices__SendPacket(
      MiscHandler::BuildTutorialFlag(tutorial_index));
}

void TutorialSystem::QueueFlaggedTutorialId(std::uint32_t tutorial_id) {
  for (int i = 0; i < kMaxFlaggedTutorials; ++i) {
    if (flagged_tutorials_[i] == static_cast<int>(tutorial_id)) return;
    if (flagged_tutorials_[i] == 0) {
      flagged_tutorials_[i] = static_cast<int>(tutorial_id);
      return;
    }
  }
}

void TutorialSystem::PlayNamedTutorialSound(const std::string_view sound_name) {
  if (named_sound_playback_callback_) {
    named_sound_playback_callback_(sound_name);
    return;
  }
  lua_State* const lua =
      openwow::ui::game::ScriptEventDispatch::Get().GetLuaState();
  if (lua == nullptr) return;
  const int top = lua_gettop(lua);
  lua_getglobal(lua, "PlaySound");
  if (lua_isfunction(lua, -1) != 0) {
    lua_pushlstring(lua, sound_name.data(), sound_name.size());
    (void)lua_pcall(lua, 1, 0, 0);
  }
  lua_settop(lua, top);
}

bool TutorialSystem::IsTutorialSeen(std::uint32_t index) const {
  if (!seen_bits_initialized_) return false;
  const auto word_idx = index >> 5;
  if (word_idx >= seen_bits_.size()) return false;
  return (seen_bits_[word_idx] & (1u << (index & 0x1F))) != 0;
}

bool TutorialSystem::IsTutorialCompleted(std::uint32_t index) const {
  if (!completed_bits_initialized_) return false;
  const auto word_idx = index >> 5;
  if (word_idx >= completed_bits_.size()) return false;
  return (completed_bits_[word_idx] & (1u << (index & 0x1F))) != 0;
}

void TutorialSystem::MarkTutorialSeen(std::uint32_t index) {
  if (!seen_bits_initialized_) return;
  const auto word_idx = index >> 5;
  if (word_idx >= seen_bits_.size()) return;
  seen_bits_[word_idx] |= (1u << (index & 0x1F));
}

void TutorialSystem::Reset() {
  flagged_tutorials_.fill(0);
  seen_bits_.fill(0);
  completed_bits_.fill(0);
  seen_bits_initialized_ = false;
  completed_bits_initialized_ = false;
}

void TutorialSystem::SetNamedSoundPlaybackCallbackForTests(
    NamedSoundPlaybackCallback callback) {
  named_sound_playback_callback_ = std::move(callback);
}

void TutorialSystem::ResetNamedSoundPlaybackCallbackForTests() {
  named_sound_playback_callback_ = {};
}

}
