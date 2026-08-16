
#include "openwow/game/actions/bindings/application/binding_profiles.h"
#include "openwow/game/actions/bindings/adapters/platform/sdl_binding_input.h"
#include "openwow/game/actions/bindings/adapters/persistence/retail_binding_text_codec.h"
#include "openwow/game/actions/bindings/adapters/persistence/binding_profile_storage.h"
#include "openwow/game/actions/bindings/adapters/retail/modified_click_adapter.h"
#include "openwow/game/c_input_control.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/foundation/text/ascii.h"
#include "openwow/vfs/virtual_file_system.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/script_event_dispatch.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace openwow::game {

namespace {

constexpr std::string_view kLuaFrameOverrideOwnerPrefix = "lua-frame:";
constexpr std::uint8_t kPendingAccountBindingMask =
    static_cast<std::uint8_t>((1u << 1) | (1u << 2));

constexpr std::uint8_t BindingSetMask(const BindingProfileScope mode) {
  return static_cast<std::uint8_t>(1u << static_cast<unsigned int>(mode));
}

constexpr std::string_view kToggleMouseBindingCommand = "TOGGLEMOUSE";

bool EqualsIgnoreCase(const std::string_view lhs, const std::string_view rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }

  for (std::size_t index = 0; index < lhs.size(); ++index) {
    const auto left = static_cast<unsigned char>(lhs[index]);
    const auto right = static_cast<unsigned char>(rhs[index]);
    if (std::tolower(left) != std::tolower(right)) {
      return false;
    }
  }

  return true;
}

constexpr std::string_view kRecognizedBindingCommands[] = {
    BindingAction::kMoveForward,
    BindingAction::kMoveBackward,
    BindingAction::kTurnLeft,
    BindingAction::kTurnRight,
    BindingAction::kStrafeLeft,
    BindingAction::kStrafeRight,
    BindingAction::kJump,
    BindingAction::kSitStand,
    BindingAction::kToggleAutoRun,
    BindingAction::kPitchUp,
    BindingAction::kPitchDown,
    BindingAction::kToggleRun,
    BindingAction::kActionButton1,
    BindingAction::kActionButton2,
    BindingAction::kActionButton3,
    BindingAction::kActionButton4,
    BindingAction::kActionButton5,
    BindingAction::kActionButton6,
    BindingAction::kActionButton7,
    BindingAction::kActionButton8,
    BindingAction::kActionButton9,
    BindingAction::kActionButton10,
    BindingAction::kActionButton11,
    BindingAction::kActionButton12,
    BindingAction::kMultiBar1_1,
    BindingAction::kMultiBar1_2,
    BindingAction::kMultiBar1_3,
    BindingAction::kMultiBar1_4,
    BindingAction::kMultiBar1_5,
    BindingAction::kMultiBar1_6,
    BindingAction::kTargetNearestEnemy,
    BindingAction::kTargetPreviousEnemy,
    BindingAction::kTargetNearestFriend,
    BindingAction::kAssistTarget,
    BindingAction::kFocusTarget,
    BindingAction::kToggleCharacter,
    BindingAction::kToggleSpellBook,
    BindingAction::kToggleTalents,
    BindingAction::kToggleQuestLog,
    BindingAction::kToggleSocial,
    BindingAction::kToggleWorldMap,
    BindingAction::kMinimapZoomIn,
    BindingAction::kMinimapZoomOut,
    BindingAction::kOpenChat,
    BindingAction::kOpenChatSlash,
    BindingAction::kReply,
    BindingAction::kToggleBag1,
    BindingAction::kToggleBag2,
    BindingAction::kToggleBag3,
    BindingAction::kToggleBag4,
    BindingAction::kToggleBackpack,
    BindingAction::kEnemyNameplates,
    BindingAction::kFriendlyNameplates,
    BindingAction::kAllNameplates,
    BindingAction::kToggleGameMenu,
    BindingAction::kScreenshot,
    "OPENALLBAGS",
    "TOGGLEGUILDTAB",
    "TOGGLESHEATH",
    "TOGGLEUI",
};

std::string UppercaseAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return value;
}

std::string NormalizeOverrideBindingKey(std::string key) {
  return UppercaseAscii(std::move(key));
}

bool StartsWithNoCase(std::string_view value, std::string_view prefix) {
  if (value.size() < prefix.size()) {
    return false;
  }
  for (std::size_t i = 0; i < prefix.size(); ++i) {
    const auto lhs = static_cast<unsigned char>(value[i]);
    const auto rhs = static_cast<unsigned char>(prefix[i]);
    if (std::toupper(lhs) != std::toupper(rhs)) {
      return false;
    }
  }
  return true;
}

bool EqualsNoCase(std::string_view lhs, std::string_view rhs) {
  return lhs.size() == rhs.size() && StartsWithNoCase(lhs, rhs);
}

bool HasModifierSeparator(std::string_view value) {
  return value.find('-') != std::string_view::npos;
}

int CountBits(unsigned int mask) {
  int count = 0;
  while (mask != 0) {
    count += static_cast<int>(mask & 1u);
    mask >>= 1u;
  }
  return count;
}

struct ParsedModifierLookupKey {
  std::vector<std::string> modifiers;
  std::string base_key;
};

ParsedModifierLookupKey ParseModifierLookupKey(std::string_view key_combo) {
  ParsedModifierLookupKey parsed;
  std::string_view remaining = key_combo;
  while (parsed.modifiers.size() < 3) {
    const auto separator = remaining.find('-');
    if (separator == std::string_view::npos || separator == 0) {
      break;
    }
    parsed.modifiers.emplace_back(
        UppercaseAscii(std::string(remaining.substr(0, separator + 1))));
    remaining.remove_prefix(separator + 1);
  }
  parsed.base_key = UppercaseAscii(std::string(remaining));
  return parsed;
}

std::uint8_t ParseBindingModifierBitsImpl(std::string_view key_combo,
                                          std::string_view& remainder) {
  std::uint8_t bits = 0;
  std::string_view cursor = key_combo;
  while (!cursor.empty()) {
    bool matched = true;
    if (StartsWithNoCase(cursor, "LSHIFT")) {
      bits |= 0x01;
      cursor.remove_prefix(6);
    } else if (StartsWithNoCase(cursor, "RSHIFT")) {
      bits |= 0x02;
      cursor.remove_prefix(6);
    } else if (StartsWithNoCase(cursor, "SHIFT")) {
      bits |= 0x03;
      cursor.remove_prefix(5);
    } else if (StartsWithNoCase(cursor, "LCTRL")) {
      bits |= 0x04;
      cursor.remove_prefix(5);
    } else if (StartsWithNoCase(cursor, "RCTRL")) {
      bits |= 0x08;
      cursor.remove_prefix(5);
    } else if (StartsWithNoCase(cursor, "CTRL")) {
      bits |= 0x0C;
      cursor.remove_prefix(4);
    } else if (StartsWithNoCase(cursor, "LALT")) {
      bits |= 0x10;
      cursor.remove_prefix(4);
    } else if (StartsWithNoCase(cursor, "RALT")) {
      bits |= 0x20;
      cursor.remove_prefix(4);
    } else if (StartsWithNoCase(cursor, "ALT")) {
      bits |= 0x30;
      cursor.remove_prefix(3);
    } else {
      matched = false;
    }

    if (!matched) {
      break;
    }

    if (!cursor.empty() && cursor.front() == '-') {
      cursor.remove_prefix(1);
    }
  }
  remainder = cursor;
  return bits;
}

std::string BuildNormalizedModifierPrefix(unsigned char bits) {
  std::string prefix;
  if ((bits & 0x30u) == 0x30u) {
    prefix += "ALT-";
  } else if ((bits & 0x10u) != 0) {
    prefix += "LALT-";
  } else if ((bits & 0x20u) != 0) {
    prefix += "RALT-";
  }

  if ((bits & 0x0Cu) == 0x0Cu) {
    prefix += "CTRL-";
  } else if ((bits & 0x04u) != 0) {
    prefix += "LCTRL-";
  } else if ((bits & 0x08u) != 0) {
    prefix += "RCTRL-";
  }

  if ((bits & 0x03u) == 0x03u) {
    prefix += "SHIFT-";
  } else if ((bits & 0x01u) != 0) {
    prefix += "LSHIFT-";
  } else if ((bits & 0x02u) != 0) {
    prefix += "RSHIFT-";
  }

  return prefix;
}

std::size_t ToModeIndex(BindingProfileScope mode) {
  return static_cast<std::size_t>(mode);
}

struct ResolvedBindingCommand {
  std::string command;
  std::string matched_key_combo;
};

std::string NormalizeLookupKeyCombo(std::string_view key_combo);
std::vector<std::string> BuildLookupKeyCandidates(std::string_view key_combo);

template <typename LookupFn>
ResolvedBindingCommand ResolveBindingCommandByKeyCombo(
    std::string_view key_combo,
    LookupFn&& lookup_command) {
  if (key_combo.empty()) {
    return {};
  }

  for (const std::string& candidate : BuildLookupKeyCandidates(key_combo)) {
    if (const std::string exact_match = lookup_command(candidate);
        !exact_match.empty()) {
      return {std::move(exact_match), candidate};
    }

    const std::string normalized_candidate = NormalizeLookupKeyCombo(candidate);
    if (normalized_candidate != UppercaseAscii(candidate)) {
      if (const std::string normalized_match = lookup_command(normalized_candidate);
          !normalized_match.empty()) {
        return {std::move(normalized_match), candidate};
      }
    }
  }

  return {};
}

std::string NormalizeLookupKeyCombo(std::string_view key_combo) {
  const std::string upper_key = UppercaseAscii(std::string(key_combo));
  if (!HasModifierSeparator(upper_key)) {
    return upper_key;
  }

  std::string_view remainder = upper_key;
  std::uint8_t bits = ParseBindingModifierBitsImpl(upper_key, remainder);
  if ((bits & 0x03u) != 0) {
    bits |= 0x03u;
  }
  if ((bits & 0x0Cu) != 0) {
    bits |= 0x0Cu;
  }
  if ((bits & 0x30u) != 0) {
    bits |= 0x30u;
  }

  return BuildNormalizedModifierPrefix(bits) + std::string(remainder);
}

std::vector<std::string> BuildLookupKeyCandidates(std::string_view key_combo) {
  const ParsedModifierLookupKey parsed = ParseModifierLookupKey(key_combo);
  const std::size_t modifier_count = parsed.modifiers.size();
  const unsigned int max_mask = 1u << modifier_count;

  std::vector<unsigned int> masks;
  masks.reserve(max_mask);
  for (unsigned int mask = 0; mask < max_mask; ++mask) {
    masks.push_back(mask);
  }
  std::sort(masks.begin(), masks.end(),
            [](unsigned int lhs, unsigned int rhs) {
              const int lhs_bits = CountBits(lhs);
              const int rhs_bits = CountBits(rhs);
              if (lhs_bits != rhs_bits) {
                return lhs_bits > rhs_bits;
              }
              return lhs > rhs;
            });

  std::vector<std::string> candidates;
  candidates.reserve(masks.size());
  for (const unsigned int mask : masks) {
    std::string candidate;
    for (std::size_t i = 0; i < modifier_count; ++i) {
      if ((mask & (1u << i)) != 0) {
        candidate += parsed.modifiers[i];
      }
    }
    candidate += parsed.base_key;
    candidates.push_back(std::move(candidate));
  }
  return candidates;
}

bool HasSpecialBindingPrefix(std::string_view command) {
  return StartsWithNoCase(command, "SPELL ")
      || StartsWithNoCase(command, "ITEM ")
      || StartsWithNoCase(command, "MACRO ")
      || StartsWithNoCase(command, "CLICK ");
}

constexpr std::string_view kBindingModifierPrefixes[] = {
    "SHIFT-",
    "CTRL-",
    "ALT-",
};

constexpr std::string_view kNumericBindingPrefixes[] = {
    "F",
    "NUMPAD",
    "BUTTON",
    "JOYSTICK",
    "JOYAXIS",
    "JOYBUTTON",
};

constexpr std::string_view kNamedBindingKeys[] = {
    "SPACE",
    "NUMPADPLUS",
    "NUMPADMINUS",
    "NUMPADMULTIPLY",
    "NUMPADDIVIDE",
    "NUMPADDECIMAL",
    "ESCAPE",
    "ENTER",
    "BACKSPACE",
    "TAB",
    "LEFT",
    "UP",
    "RIGHT",
    "DOWN",
    "INSERT",
    "DELETE",
    "HOME",
    "END",
    "PAGEUP",
    "PAGEDOWN",
    "NUMLOCK",
    "CAPSLOCK",
    "PRINTSCREEN",
    "NUMPADEQUALS",
    "MOUSEWHEELDOWN",
    "MOUSEWHEELUP",
    "JOYHAT",
    "JOYHATUP",
    "JOYHATRIGHT",
    "JOYHATDOWN",
    "JOYHATLEFT",
};

std::string StripBindingModifiers(std::string_view key) {
  std::string remaining = UppercaseAscii(std::string(key));
  bool stripped = false;
  do {
    stripped = false;
    for (const std::string_view prefix : kBindingModifierPrefixes) {
      if (!StartsWithNoCase(remaining, prefix)) {
        continue;
      }
      remaining.erase(0, prefix.size());
      stripped = true;
    }
  } while (stripped);

  return remaining;
}

std::size_t ConsumedUtf8LeadSpan(std::string_view value) {
  if (value.empty()) {
    return 0;
  }

  const unsigned char first = static_cast<unsigned char>(value.front());
  int continuation_count = 0;
  if ((first & 0xFEu) == 0xFCu) {
    continuation_count = 5;
  } else if ((first & 0xFCu) == 0xF8u) {
    continuation_count = 4;
  } else if ((first & 0xF8u) == 0xF0u) {
    continuation_count = 3;
  } else if ((first & 0xF0u) == 0xE0u) {
    continuation_count = 2;
  } else if ((first & 0xE0u) == 0xC0u) {
    continuation_count = 1;
  } else {
    return 1;
  }

  std::size_t consumed = 1;
  for (int i = 0; i < continuation_count; ++i) {
    if (consumed >= value.size()) {
      return consumed;
    }

    const unsigned char next = static_cast<unsigned char>(value[consumed]);
    ++consumed;
    if ((next & 0xC0u) != 0x80u) {
      return consumed;
    }
  }

  return consumed;
}

bool MatchesBindingNumberedPrefix(std::string_view key) {
  for (const std::string_view prefix : kNumericBindingPrefixes) {
    if (!StartsWithNoCase(key, prefix) || key.size() <= prefix.size()) {
      continue;
    }

    std::size_t cursor = prefix.size();
    if (!std::isdigit(static_cast<unsigned char>(key[cursor]))) {
      continue;
    }

    while (cursor < key.size()
        && std::isdigit(static_cast<unsigned char>(key[cursor]))) {
      ++cursor;
    }

    if (cursor == key.size()) {
      return true;
    }

    if (EqualsNoCase(prefix, "JOYAXIS")) {
      const std::string_view suffix = key.substr(cursor);
      if (EqualsNoCase(suffix, "POS") || EqualsNoCase(suffix, "NEG")) {
        return true;
      }
    }

    return false;
  }

  return false;
}

}

BindingProfiles::OverrideOwner BindingProfiles::OverrideOwner::FromStableTag(
    std::string_view token) {
  return OverrideOwner(std::string(token));
}

BindingProfiles::OverrideOwner
BindingProfiles::OverrideOwner::FromLuaFrameReference(int lua_reference) {
  return OverrideOwner(std::string(kLuaFrameOverrideOwnerPrefix)
                       + std::to_string(lua_reference));
}

bool BindingProfiles::Initialize() {
  if (initialized_) return true;

  LoadDefaults();
  CopyBindingSet(BindingProfileScope::kDefault, BindingProfileScope::kAccount);
  Finalize();

  initialized_ = true;
  const auto active_count = static_cast<std::size_t>(std::count_if(
      bindings_.begin(), bindings_.end(), [](const BindingAssignment& binding) {
        return binding.scope == BindingProfileScope::kActive;
      }));
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "BindingProfiles: initialized with "
                         + std::to_string(active_count) + " active bindings");
  return true;
}

void BindingProfiles::Shutdown() {
  if (initialized_ || account_data_load_generation_ != 0) {
    AdvanceAccountDataLoadGeneration();
  }

  ResetBindingDefinitions();
  bindings_.clear();
  override_bindings_.clear();
  active_mode0_key_to_command_.clear();
  recognized_binding_commands_.clear();
  modified_click_bindings_.clear();
  modified_click_index_by_action_.clear();
  execute_fn_ = nullptr;
  release_fn_ = nullptr;
  protection_gate_ = {};
  initialized_ = false;
  current_binding_set_ = BindingProfileScope::kAccount;
  pending_account_data_mask_ = 0;
  copied_binding_set_mask_ = 0;
  vfs_ = nullptr;
  joystick_name_provider_ = {};
}

void BindingProfiles::BeginWorldUiSession() {
  ResetWorldUiRuntimeState();
  bindings_.clear();
  active_mode0_key_to_command_.clear();
  recognized_binding_commands_.clear();
  current_binding_set_ = BindingProfileScope::kAccount;
  pending_account_data_mask_ = 0;
  copied_binding_set_mask_ = 0;

  AdvanceAccountDataLoadGeneration();
}

void BindingProfiles::EndWorldUiSession() {
  ResetWorldUiRuntimeState();
  bindings_.clear();
  active_mode0_key_to_command_.clear();
  recognized_binding_commands_.clear();
  current_binding_set_ = BindingProfileScope::kAccount;
  pending_account_data_mask_ = 0;
  copied_binding_set_mask_ = 0;

  ++account_data_load_generation_;
}

void BindingProfiles::LoadDefaults() {
  bindings_.clear();
  current_binding_set_ = BindingProfileScope::kAccount;
  InitializeRecognizedBindingCommands();

  if (!parsed_default_bindings_.empty()) {
    std::unordered_set<std::string> claimed_keys;
    claimed_keys.reserve(parsed_default_bindings_.size() * 2);
    for (const auto& [key, command] : parsed_default_bindings_) {
      const std::string normalized_key =
          actions::bindings::adapters::persistence::
              NormalizeRetailBindingChord(key.value());
      if (claimed_keys.contains(normalized_key)) {
        continue;
      }
      if (AssignBinding(
              BindingProfileScope::kDefault, BindingSlot::Primary(),
              BindingChord(normalized_key), command)) {
        claimed_keys.insert(normalized_key);
      }
    }
  } else {

    std::unordered_map<std::string_view, int> next_index_by_command;
    next_index_by_command.reserve(64);
    auto add = [this, &next_index_by_command](const char* key,
                                              const char* command) {
      const int binding_index = next_index_by_command[command]++;
      bindings_.push_back(
          {BindingChord(key), BindingCommand(command),
           BindingProfileScope::kDefault, BindingSlot::Primary(),
           binding_index});
    };

    add("W", BindingAction::kMoveForward);
    add("UP", BindingAction::kMoveForward);
    add("S", BindingAction::kMoveBackward);
    add("DOWN", BindingAction::kMoveBackward);
    add("A", BindingAction::kTurnLeft);
    add("LEFT", BindingAction::kTurnLeft);
    add("D", BindingAction::kTurnRight);
    add("RIGHT", BindingAction::kTurnRight);
    add("Q", BindingAction::kStrafeLeft);
    add("E", BindingAction::kStrafeRight);
    add("SPACE", BindingAction::kJump);
    add("X", BindingAction::kSitStand);
    add("NUMLOCK", BindingAction::kToggleAutoRun);

    add("1", BindingAction::kActionButton1);
    add("2", BindingAction::kActionButton2);
    add("3", BindingAction::kActionButton3);
    add("4", BindingAction::kActionButton4);
    add("5", BindingAction::kActionButton5);
    add("6", BindingAction::kActionButton6);
    add("7", BindingAction::kActionButton7);
    add("8", BindingAction::kActionButton8);
    add("9", BindingAction::kActionButton9);
    add("0", BindingAction::kActionButton10);
    add("-", BindingAction::kActionButton11);
    add("=", BindingAction::kActionButton12);

    add("CTRL-1", BindingAction::kMultiBar1_1);
    add("CTRL-2", BindingAction::kMultiBar1_2);
    add("CTRL-3", BindingAction::kMultiBar1_3);
    add("CTRL-4", BindingAction::kMultiBar1_4);
    add("CTRL-5", BindingAction::kMultiBar1_5);
    add("CTRL-6", BindingAction::kMultiBar1_6);

    add("TAB", BindingAction::kTargetNearestEnemy);
    add("SHIFT-TAB", BindingAction::kTargetPreviousEnemy);
    add("F", BindingAction::kAssistTarget);

    add("V", BindingAction::kEnemyNameplates);
    add("SHIFT-V", BindingAction::kFriendlyNameplates);
    add("CTRL-V", BindingAction::kAllNameplates);
    add("C", BindingAction::kToggleCharacter);
    add("P", BindingAction::kToggleSpellBook);
    add("N", BindingAction::kToggleTalents);
    add("L", BindingAction::kToggleQuestLog);
    add("O", BindingAction::kToggleSocial);
    add("M", BindingAction::kToggleWorldMap);
    add("B", BindingAction::kToggleBag1);
    add("SHIFT-B", BindingAction::kToggleBag2);

    add("ENTER", BindingAction::kOpenChat);
    add("/", BindingAction::kOpenChatSlash);
    add("R", BindingAction::kReply);

    add("ESCAPE", BindingAction::kToggleGameMenu);

    add("PRINTSCREEN", BindingAction::kScreenshot);
  }

  if (vfs_ != nullptr) {
    bool loaded_default_bindings_file = false;
    if (const auto defaults_text = vfs_->ReadTextFile("WTF\\DefaultBindings.wtf");
        defaults_text.has_value()) {
      actions::bindings::adapters::persistence::BindingProfileStorage::
          ApplyText(*this, *defaults_text, BindingProfileScope::kDefault);
      loaded_default_bindings_file = true;
    }

    if (loaded_default_bindings_file) {
      const auto joystick_name = joystick_name_provider_
          ? joystick_name_provider_()
          : actions::bindings::adapters::platform::
                QueryPrimaryJoystickName();
      if (joystick_name.has_value()) {
        const auto joystick_config =
            vfs_->ReadTextFile("Interface\\Joystick\\JoystickConfig.xml");
        if (joystick_config.has_value()) {
          if (const auto profile =
                  actions::bindings::adapters::platform::
                      ParseJoystickConfigProfile(
                          *joystick_config, *joystick_name);
              profile.has_value() && !profile->default_bindings.empty()) {
            actions::bindings::adapters::persistence::BindingProfileStorage::
                ApplyText(*this, profile->default_bindings,
                          BindingProfileScope::kDefault);
          }
        }
      }
    }
  }

  RebuildLookup();
}

void BindingProfiles::InitializeRecognizedBindingCommands() {
  recognized_binding_commands_.clear();
  for (const std::string_view command : kRecognizedBindingCommands) {
    recognized_binding_commands_.emplace(
        BindingCommand(UppercaseAscii(std::string(command))));
  }
}

void BindingProfiles::RebuildLookup() {
  RebuildActiveLookup();
}

void BindingProfiles::RebuildActiveLookup() {
  active_mode0_key_to_command_.clear();

  for (const auto& b : bindings_) {
    if (b.scope == BindingProfileScope::kActive &&
        b.slot == BindingSlot::Primary()) {
      active_mode0_key_to_command_.insert_or_assign(b.chord, b.command);
    }
  }
}

void BindingProfiles::RemoveOverrideOwner(OverrideKeyEntry& entry,
                                            const OverrideOwner& owner) {
  const auto erase_owner = [&owner](std::vector<OverrideBindingNode>& bucket) {
    bucket.erase(
        std::remove_if(bucket.begin(), bucket.end(),
                       [&owner](const OverrideBindingNode& node) {
                         return node.owner == owner;
                       }),
        bucket.end());
  };

  erase_owner(entry.high_priority);
  erase_owner(entry.low_priority);
}

const BindingCommand* BindingProfiles::GetResolvedOverrideCommand(
    const BindingChord& chord) const {
  const auto found = override_bindings_.find(chord);
  if (found == override_bindings_.end()) {
    return nullptr;
  }

  const auto& entry = found->second;
  if (!entry.high_priority.empty()) {
    return &entry.high_priority.back().command;
  }
  if (!entry.low_priority.empty()) {
    return &entry.low_priority.back().command;
  }
  return nullptr;
}

void BindingProfiles::RemoveBindingsForMode(BindingProfileScope mode) {
  bindings_.erase(
      std::remove_if(bindings_.begin(), bindings_.end(),
                     [mode](const BindingAssignment& binding) {
                       return binding.scope == mode;
                     }),
      bindings_.end());
}

void BindingProfiles::RemoveBindingsForModeAndSlot(BindingProfileScope mode,
                                                     std::uint8_t binding_slot) {
  bindings_.erase(
      std::remove_if(bindings_.begin(), bindings_.end(),
                     [mode, binding_slot](const BindingAssignment& binding) {
                       return binding.scope == mode
                           && binding.slot.value() == binding_slot;
                     }),
      bindings_.end());
}

bool BindingProfiles::IsValidBindingKeyName(std::string_view key) {
  const std::string remaining = StripBindingModifiers(key);
  if (remaining.empty()) {
    return false;
  }

  if (ConsumedUtf8LeadSpan(remaining) == remaining.size()) {
    return true;
  }

  if (MatchesBindingNumberedPrefix(remaining)) {
    return true;
  }

  return std::any_of(std::begin(kNamedBindingKeys), std::end(kNamedBindingKeys),
                     [remaining](const std::string_view named_key) {
                       return EqualsNoCase(remaining, named_key);
                     });
}

int BindingProfiles::CountBindingsForCommandInModeAndSlot(
    BindingProfileScope mode,
    std::uint8_t binding_slot,
    const std::string& command) const {
  int count = 0;
  for (const auto& binding : bindings_) {
    if (binding.scope == mode
        && binding.slot.value() == binding_slot
        && EqualsNoCase(binding.command.value(), command)) {
      ++count;
    }
  }
  return count;
}

void BindingProfiles::DecrementBindingIndicesForCommand(
    BindingProfileScope mode,
    std::uint8_t binding_slot,
    const std::string& command,
    int removed_index) {
  for (auto& binding : bindings_) {
    if (binding.scope == mode
        && binding.slot.value() == binding_slot
        && EqualsNoCase(binding.command.value(), command)
        && binding.index > removed_index) {
      --binding.index;
    }
  }
}

std::vector<std::string> BindingProfiles::CollectKeysForCommandInModeAndSlot(
    const std::string& command,
    BindingProfileScope mode,
    std::uint8_t binding_slot) const {
  struct IndexedKey {
    int index;
    std::string key;
  };

  std::vector<IndexedKey> indexed_keys;
  for (const auto& binding : bindings_) {
    if (binding.scope == mode
        && binding.slot.value() == binding_slot
        && EqualsNoCase(binding.command.value(), command)) {
      indexed_keys.push_back({binding.index, binding.chord.value()});
    }
  }

  std::sort(indexed_keys.begin(), indexed_keys.end(),
            [](const IndexedKey& lhs, const IndexedKey& rhs) {
              if (lhs.index != rhs.index) {
                return lhs.index < rhs.index;
              }
              return lhs.key < rhs.key;
            });

  std::vector<std::string> keys;
  keys.reserve(indexed_keys.size());
  for (auto& indexed_key : indexed_keys) {
    keys.push_back(std::move(indexed_key.key));
  }
  return keys;
}

bool BindingProfiles::AssignBinding(BindingProfileScope mode,
                                    BindingSlot slot,
                                    BindingChord chord,
                                    BindingCommand command) {
  if (!IsValidBindingKeyName(chord.value()) || command.empty()) {
    return false;
  }

  auto existing = std::find_if(
      bindings_.begin(), bindings_.end(),
      [&](const BindingAssignment& binding) {
        return binding.scope == mode
            && binding.slot == slot
            && binding.chord == chord;
      });

  if (existing != bindings_.end()) {
    if (EqualsNoCase(existing->command.value(), command.value())) {
      return true;
    }
    DecrementBindingIndicesForCommand(
        mode, slot.value(), existing->command.value(), existing->index);
    existing->command = command;
    existing->index = CountBindingsForCommandInModeAndSlot(
        mode, slot.value(), command.value());
  } else {
    const int binding_index = CountBindingsForCommandInModeAndSlot(
        mode, slot.value(), command.value());
    bindings_.push_back(
        {std::move(chord), std::move(command), mode, slot, binding_index});
  }

  RebuildLookup();
  return true;
}

std::vector<BindingChord> BindingProfiles::ChordsForCommand(
    const BindingCommand& command,
    const BindingProfileScope scope,
    const BindingSlot slot) const {
  struct IndexedChord {
    int index;
    BindingChord chord;
  };

  std::vector<IndexedChord> indexed_chords;
  for (const auto& binding : bindings_) {
    if (binding.scope == scope && binding.slot == slot &&
        EqualsNoCase(binding.command.value(), command.value())) {
      indexed_chords.push_back({binding.index, binding.chord});
    }
  }
  std::sort(indexed_chords.begin(), indexed_chords.end(),
            [](const IndexedChord& lhs, const IndexedChord& rhs) {
              if (lhs.index != rhs.index) {
                return lhs.index < rhs.index;
              }
              return lhs.chord < rhs.chord;
            });

  std::vector<BindingChord> chords;
  chords.reserve(indexed_chords.size());
  for (auto& indexed_chord : indexed_chords) {
    chords.push_back(std::move(indexed_chord.chord));
  }
  return chords;
}

std::optional<BindingCommand> BindingProfiles::ResolveBinding(
    const BindingChord& chord,
    const bool check_override,
    const BindingProfileScope scope,
    const BindingSlot slot) const {
  const std::string command = LookupBindingActionExactForModeAndSlot(
      chord.value(), check_override, scope, slot.value());
  if (command.empty()) {
    return std::nullopt;
  }
  return BindingCommand(command);
}

int BindingProfiles::GetNumBindings() const {
  return static_cast<int>(visible_binding_definitions_.size());
}

int BindingProfiles::GetNumHiddenBindings() const {
  return next_hidden_binding_index_;
}

int BindingProfiles::GetNumModifiedClickActions() const {
  return static_cast<int>(modified_click_bindings_.size());
}

std::optional<BindingDisplayEntry> BindingProfiles::BindingAt(
    const int display_index,
    const BindingProfileScope scope,
    const BindingSlot slot) const {
  if (display_index < 1 ||
      display_index > static_cast<int>(visible_binding_definitions_.size())) {
    return std::nullopt;
  }
  const BindingCommand& command =
      binding_definitions_[visible_binding_definitions_[
          static_cast<std::size_t>(display_index - 1)]]
          .command;
  return BindingDisplayEntry{command, ChordsForCommand(command, scope, slot)};
}

std::vector<BindingChord> BindingProfiles::ChordsForCommandInActiveSlots(
    const BindingCommand& command,
    const BindingSlotSelector selector) const {
  if (selector != BindingSlotSelector::kCurrent) {
    return ChordsForCommand(
        command, BindingProfileScope::kActive,
        *BindingSlot::FromValue(static_cast<std::uint8_t>(selector)));
  }

  struct IndexedChord {
    int index;
    BindingChord chord;
  };

  std::vector<IndexedChord> indexed_chords;
  std::unordered_set<BindingChord> seen_chords;
  const std::uint8_t active_mask =
      static_cast<std::uint8_t>(binding_state_bits_ ^ binding_state_reference_bits_);

  for (const auto& binding : bindings_) {
    if (binding.scope != BindingProfileScope::kActive ||
        !seen_chords.insert(binding.chord).second) {
      continue;
    }

    const BindingAssignment* selected = nullptr;
    for (int slot = 3; slot >= 0; --slot) {
      if (slot != 0 && (active_mask & (1u << slot)) == 0u) {
        continue;
      }
      const auto it = std::find_if(
          bindings_.begin(), bindings_.end(),
          [&](const BindingAssignment& candidate) {
            return candidate.scope == BindingProfileScope::kActive
                && candidate.slot.value() == slot
                && candidate.chord == binding.chord;
          });
      if (it != bindings_.end()) {
        selected = &*it;
        break;
      }
    }

    if (selected != nullptr &&
        EqualsNoCase(selected->command.value(), command.value())) {
      indexed_chords.push_back({selected->index, selected->chord});
    }
  }

  std::sort(indexed_chords.begin(), indexed_chords.end(),
            [](const IndexedChord& lhs, const IndexedChord& rhs) {
              if (lhs.index != rhs.index) {
                return lhs.index < rhs.index;
              }
              return lhs.chord < rhs.chord;
            });

  std::vector<BindingChord> chords;
  chords.reserve(indexed_chords.size());
  for (auto& indexed_chord : indexed_chords) {
    chords.push_back(std::move(indexed_chord.chord));
  }
  return chords;
}

bool BindingProfiles::IsRecognizedBindingCommand(
    const std::string& command) const {
  const std::string normalized_command = UppercaseAscii(command);
  return HasSpecialBindingPrefix(command)
      || binding_definition_by_command_.find(
             BindingCommand(normalized_command))
          != binding_definition_by_command_.end()
      || recognized_binding_commands_.find(BindingCommand(normalized_command))
          != recognized_binding_commands_.end();
}

std::string BindingProfiles::LookupBindingActionExactForModeAndSlot(
    const std::string& key,
    bool check_override,
    BindingProfileScope mode,
    std::uint8_t binding_slot) const {
  const std::string normalized_key = NormalizeOverrideBindingKey(key);

  if (check_override) {
    if (const auto* command =
            GetResolvedOverrideCommand(BindingChord(normalized_key));
        command != nullptr &&
        IsRecognizedBindingCommand(command->value())) {
      return command->value();
    }
  }

  std::string command;
  if (mode == BindingProfileScope::kActive && binding_slot == 0) {
    const auto found =
        active_mode0_key_to_command_.find(BindingChord(normalized_key));
    if (found != active_mode0_key_to_command_.end()) {
      command = found->second.value();
    }
  }

  if (command.empty()) {
    for (const auto& binding : bindings_) {
      if (binding.scope == mode
          && binding.slot.value() == binding_slot
          && binding.chord.value() == normalized_key) {
        command = binding.command.value();
        break;
      }
    }
  }

  if (command.empty() || !IsRecognizedBindingCommand(command)) {
    return {};
  }
  return command;
}

std::string BindingProfiles::LookupBindingActionExact(
    const std::string& key,
    bool check_override,
    BindingProfileScope mode) const {
  return LookupBindingActionExactForModeAndSlot(key, check_override, mode, 0);
}

std::optional<BindingCommand> BindingProfiles::ResolveBindingInActiveSlots(
    const BindingChord& chord,
    const bool check_override,
    const BindingSlotSelector selector) const {
  if (selector != BindingSlotSelector::kCurrent) {
    return ResolveBinding(
        chord, check_override, BindingProfileScope::kActive,
        *BindingSlot::FromValue(static_cast<std::uint8_t>(selector)));
  }

  if (check_override) {
    if (const auto* command = GetResolvedOverrideCommand(chord);
        command != nullptr &&
        IsRecognizedBindingCommand(command->value())) {
      return *command;
    }
  }

  const std::uint8_t active_mask =
      static_cast<std::uint8_t>(binding_state_bits_ ^ binding_state_reference_bits_);
  for (int slot = 3; slot >= 0; --slot) {
    if (slot != 0 && (active_mask & (1u << slot)) == 0u) {
      continue;
    }
    const auto command = ResolveBinding(
        chord, false, BindingProfileScope::kActive,
        *BindingSlot::FromValue(static_cast<std::uint8_t>(slot)));
    if (command) {
      return command;
    }
  }
  return std::nullopt;
}

std::optional<BindingCommand> BindingProfiles::ResolveChordWithFallback(
    const BindingChord& chord,
    const BindingProfileScope scope) const {
  const auto resolution = ResolveChordWithFallbackDetailed(chord, scope);
  return resolution ? std::optional(resolution->command) : std::nullopt;
}

std::optional<BindingResolution>
BindingProfiles::ResolveChordWithFallbackDetailed(
    const BindingChord& chord,
    const BindingProfileScope scope) const {
  const auto resolved = ResolveBindingCommandByKeyCombo(
      chord.value(), [this, scope](const std::string& candidate) {
        return LookupBindingActionExact(candidate, true, scope);
      });
  if (resolved.command.empty()) {
    return std::nullopt;
  }
  return BindingResolution{
      BindingCommand(resolved.command),
      BindingChord(resolved.matched_key_combo),
  };
}

std::optional<BindingCommand>
BindingProfiles::ResolveChordWithFallbackInActiveSlots(
    const BindingChord& chord,
    const BindingSlotSelector selector) const {
  const auto resolved = ResolveBindingCommandByKeyCombo(
      chord.value(),
      [this, selector](const std::string& candidate) {
        const auto command = ResolveBindingInActiveSlots(
            BindingChord(candidate), true, selector);
        return command ? command->value() : std::string{};
      });
  return resolved.command.empty()
             ? std::nullopt
             : std::optional(BindingCommand(resolved.command));
}

std::vector<BindingAssignment> BindingProfiles::SnapshotBindings(
    const BindingProfileScope scope) const {
  std::vector<BindingAssignment> snapshot;
  for (const auto& binding : bindings_) {
    if (binding.scope == scope) {
      snapshot.push_back(binding);
    }
  }
  return snapshot;
}

std::vector<ModifiedClickAssignment> BindingProfiles::SnapshotModifiedClicks(
    const BindingProfileScope scope) const {
  std::vector<ModifiedClickAssignment> snapshot;
  const auto scope_index = static_cast<std::size_t>(scope);
  if (scope_index >= 4) {
    return snapshot;
  }
  snapshot.reserve(modified_click_bindings_.size());
  for (const auto& definition : modified_click_bindings_) {
    snapshot.push_back({definition.action, definition.states[scope_index]});
  }
  return snapshot;
}

void BindingProfiles::SetCurrentBindingStateBit(const std::uint8_t bit_index,
                                                  const bool enabled) {
  if (bit_index == 0) {
    return;
  }

  const std::uint8_t mask = static_cast<std::uint8_t>(1u << bit_index);
  const std::uint8_t desired_value = enabled ? 1u : 0u;
  if (desired_value == static_cast<std::uint8_t>(binding_state_bits_ & mask)) {
    return;
  }

  if (protection_gate_ &&
      !protection_gate_(BindingProtectedOperation::kChangeRuntimeState)) {
    return;
  }

  const std::uint8_t previous_effective_state =
      static_cast<std::uint8_t>(mask & (binding_state_bits_ ^ binding_state_reference_bits_));
  if (enabled) {
    binding_state_bits_ = static_cast<std::uint8_t>(binding_state_bits_ | mask);
  } else {
    binding_state_bits_ = static_cast<std::uint8_t>(binding_state_bits_ & ~mask);
  }
  binding_state_reference_bits_ =
      static_cast<std::uint8_t>(binding_state_reference_bits_ & ~mask);

  const std::uint8_t current_effective_state =
      static_cast<std::uint8_t>(mask & (binding_state_bits_ ^ binding_state_reference_bits_));
  if (previous_effective_state != current_effective_state) {
    openwow::ui::game::ScriptEventDispatch::Get().FireEvent(
        openwow::ui::game::events::UPDATE_BINDINGS);
  }
}

BindingProfileScope BindingProfiles::GetCurrentBindingSet() const {
  return current_binding_set_;
}

void BindingProfiles::SetOverrideBinding(const OverrideOwner& owner,
                                           bool is_priority,
                                           BindingChord chord,
                                           std::optional<BindingCommand> command) {
  auto& entry = override_bindings_[chord];
  RemoveOverrideOwner(entry, owner);

  if (command.has_value()) {
    auto& bucket = is_priority ? entry.high_priority : entry.low_priority;
    bucket.push_back({owner, std::move(*command)});
  } else if (entry.high_priority.empty() && entry.low_priority.empty()) {
    override_bindings_.erase(chord);
  }

  RebuildLookup();
}

void BindingProfiles::ClearOverrideBindings(const OverrideOwner& owner) {
  for (auto it = override_bindings_.begin(); it != override_bindings_.end();) {
    RemoveOverrideOwner(it->second, owner);
    if (it->second.high_priority.empty() && it->second.low_priority.empty()) {
      it = override_bindings_.erase(it);
    } else {
      ++it;
    }
  }
  RebuildLookup();
}

void BindingProfiles::SaveBindings(BindingProfileScope mode) {
  if (mode != BindingProfileScope::kAccount && mode != BindingProfileScope::kCharacter) {
    return;
  }

  current_binding_set_ = mode;
  CopyBindingSet(BindingProfileScope::kActive, mode);
  if (mode == BindingProfileScope::kAccount) {
    CopyBindingSet(BindingProfileScope::kAccount, BindingProfileScope::kCharacter);
  }
}

std::vector<BindingProfileScope>
BindingProfiles::PersistentProfilesNeedingSave() const {
  std::vector<BindingProfileScope> scopes;
  if (pending_account_data_mask_ != 0) {
    return scopes;
  }
  if ((copied_binding_set_mask_ &
       BindingSetMask(BindingProfileScope::kAccount)) != 0u) {
    scopes.push_back(BindingProfileScope::kAccount);
    if (current_binding_set_ == BindingProfileScope::kCharacter) {
      scopes.push_back(BindingProfileScope::kCharacter);
    }
    return scopes;
  }
  if ((copied_binding_set_mask_ &
       BindingSetMask(BindingProfileScope::kCharacter)) != 0u &&
      current_binding_set_ == BindingProfileScope::kCharacter) {
    scopes.push_back(BindingProfileScope::kCharacter);
  }
  return scopes;
}

void BindingProfiles::LoadBindings(BindingProfileScope mode) {
  if (mode != BindingProfileScope::kDefault
      && mode != BindingProfileScope::kAccount
      && mode != BindingProfileScope::kCharacter) {
    return;
  }
  CopyBindingSet(mode, BindingProfileScope::kActive);
}

std::optional<ModifiedClickAction> BindingProfiles::ModifiedClickActionAt(
    const std::size_t index) const {
  if (index >= modified_click_bindings_.size()) {
    return std::nullopt;
  }
  return modified_click_bindings_[index].action;
}

std::optional<ModifiedClickBindingState>
BindingProfiles::ModifiedClickBinding(
    const ModifiedClickAction& action,
    const BindingProfileScope scope) const {
  const auto* definition = FindModifiedClickDefinition(action);
  const auto scope_index = static_cast<std::size_t>(scope);
  if (definition == nullptr || scope_index >= definition->states.size()) {
    return std::nullopt;
  }
  return definition->states[scope_index];
}

bool BindingProfiles::AssignModifiedClick(
    const ModifiedClickAction& action,
    ModifiedClickBindingState state,
    const BindingProfileScope scope) {
  auto* definition = FindModifiedClickDefinition(action);
  const auto scope_index = static_cast<std::size_t>(scope);
  if (definition == nullptr || scope_index >= definition->states.size()) {
    return false;
  }
  definition->states[scope_index] = std::move(state);
  return true;
}

bool BindingProfiles::IsModifiedClickActive(
    const std::optional<ModifiedClickAction>& action,
    const ModifiedClickInputState& input) const {
  const auto state_matches = [&input](const ModifiedClickBindingState& state) {
    if (state.modifier_bits == 0u && !state.has_button_token) {
      return false;
    }
    if (state.modifier_bits != 0u &&
        (state.modifier_bits & input.modifier_bits) == 0u) {
      return false;
    }
    return !state.has_button_token || !input.button_index ||
           state.button_index == *input.button_index;
  };

  if (!action) {
    if (input.modifier_bits != 0u) {
      return true;
    }
    for (const auto& definition : modified_click_bindings_) {
      if (state_matches(
              definition.states[ToModeIndex(BindingProfileScope::kActive)])) {
        return true;
      }
    }
    return false;
  }
  if (action->value().empty()) {
    return false;
  }
  const auto* definition = FindModifiedClickDefinition(*action);
  if (definition == nullptr) {
    return false;
  }
  return state_matches(
      definition->states[ToModeIndex(BindingProfileScope::kActive)]);
}

void BindingProfiles::SetExecuteCallback(ExecuteFn fn) {
  execute_fn_ = std::move(fn);
}

void BindingProfiles::SetReleaseCallback(ReleaseExecuteFn fn) {
  release_fn_ = std::move(fn);
}

void BindingProfiles::SetProtectionGate(ProtectionGate gate) {
  protection_gate_ = std::move(gate);
}

void BindingProfiles::SetVfs(const openwow::vfs::VirtualFileSystem* vfs) {
  vfs_ = vfs;
}

void BindingProfiles::SetJoystickNameProvider(JoystickNameProvider provider) {
  joystick_name_provider_ = std::move(provider);
}

void BindingProfiles::ResetBindingDefinitions() {
  binding_script_executors_.clear();
  binding_definitions_.clear();
  binding_definition_by_command_.clear();
  visible_binding_definitions_.clear();
  parsed_default_bindings_.clear();
  modified_click_bindings_.clear();
  modified_click_index_by_action_.clear();
  next_visible_binding_index_ = 0;
  next_hidden_binding_index_ = 0;
}

void BindingProfiles::ResetWorldUiRuntimeState() {
  ResetBindingDefinitions();
  override_bindings_.clear();
  binding_state_bits_ = 0;
  binding_state_reference_bits_ = 0;
}

bool BindingProfiles::HasLoadedBindingDefinitions() const {
  return !binding_definitions_.empty();
}

bool BindingProfiles::HasBindingDefinition(
    const BindingCommand& command) const {
  return binding_definition_by_command_.contains(
      BindingCommand(UppercaseAscii(command.value())));
}

bool BindingProfiles::HasModifiedClickDefinition(
    const ModifiedClickAction& action) const {
  return modified_click_index_by_action_.contains(
      ModifiedClickAction(UppercaseAscii(action.value())));
}

bool BindingProfiles::HasBindingDefinitionJoystick() const {
  return joystick_name_provider_ ? joystick_name_provider_().has_value()
      : actions::bindings::adapters::platform::
            QueryPrimaryJoystickName().has_value();
}

void BindingProfiles::AddParsedDefaultBinding(
    BindingChord chord,
    BindingCommand command) {
  parsed_default_bindings_.emplace_back(std::move(chord),
                                        std::move(command));
}

void BindingProfiles::CopyBindingSet(BindingProfileScope src, BindingProfileScope dst) {
  if (protection_gate_ &&
      !protection_gate_(BindingProtectedOperation::kCopyProfile)) {
    return;
  }

  BindingProfileScope effective_src = BindingProfileScope::kDefault;
  if (src == BindingProfileScope::kDefault) {
    effective_src = BindingProfileScope::kDefault;
  } else {
    for (int mode_value = static_cast<int>(src); mode_value > 0; --mode_value) {
      const auto candidate = static_cast<BindingProfileScope>(mode_value);
      const bool has_bindings = std::any_of(
          bindings_.begin(), bindings_.end(),
          [candidate](const BindingAssignment& binding) {
            return binding.scope == candidate;
          });
      if (has_bindings) {
        effective_src = candidate;
        break;
      }
    }
  }

  RemoveBindingsForMode(dst);

  if (effective_src != dst) {
    std::vector<BindingAssignment> to_add;
    for (const auto& binding : bindings_) {
      if (binding.scope == effective_src) {
        BindingAssignment copy = binding;
        copy.scope = dst;
        to_add.push_back(std::move(copy));
      }
    }
    bindings_.insert(bindings_.end(), to_add.begin(), to_add.end());
  }

  const std::size_t src_index = ToModeIndex(effective_src);
  const std::size_t dst_index = ToModeIndex(dst);
  for (auto& definition : modified_click_bindings_) {
    definition.states[dst_index] = definition.states[src_index];
  }

  RebuildLookup();
  copied_binding_set_mask_ = static_cast<std::uint8_t>(
      copied_binding_set_mask_ | BindingSetMask(dst));
  if (dst == BindingProfileScope::kActive) {
    openwow::ui::game::ScriptEventDispatch::Get().FireEvent(
        openwow::ui::game::events::UPDATE_BINDINGS);
  }
}

void BindingProfiles::Finalize() {
  bool has_char_bindings = false;
  for (const auto& binding : bindings_) {
    if (binding.scope == BindingProfileScope::kCharacter) {
      has_char_bindings = true;
      break;
    }
  }

  current_binding_set_ = has_char_bindings ? BindingProfileScope::kCharacter
                                           : BindingProfileScope::kAccount;
  CopyBindingSet(current_binding_set_, BindingProfileScope::kActive);

  if (ChordsForCommand(BindingCommand(kToggleMouseBindingCommand),
                       BindingProfileScope::kActive)
          .empty()) {
    return;
  }

  if (auto* input = GetInputControlSingleton(); input != nullptr) {
    input->SetCursorVisibilityFlagsRaw(
        input->GetCursorVisibilityFlags() & ~kCursorVisibilityDefaultRequest);
    (void)input->RefreshCursorVisibility(false);
  }
}

void BindingProfiles::AdvanceAccountDataLoadGeneration() {
  do {
    ++account_data_load_generation_;
  } while (account_data_load_generation_ == 0);
}

BindingProfileLoadGeneration BindingProfiles::BeginProfileLoad() {
  if (account_data_load_generation_ == 0) {
    AdvanceAccountDataLoadGeneration();
  }
  pending_account_data_mask_ = kPendingAccountBindingMask;
  return BindingProfileLoadGeneration(account_data_load_generation_);
}

bool BindingProfiles::AcceptsProfileLoad(
    const BindingProfileLoadGeneration generation) const noexcept {
  return generation.value() == account_data_load_generation_;
}

void BindingProfiles::CompleteProfileLoad(
    const BindingProfileLoadGeneration generation,
    const BindingProfileScope scope) {
  if (generation.value() != account_data_load_generation_) {
    return;
  }
  if (scope != BindingProfileScope::kAccount &&
      scope != BindingProfileScope::kCharacter) {
    return;
  }
  const auto binding_set_id = static_cast<std::uint8_t>(scope);
  pending_account_data_mask_ = static_cast<std::uint8_t>(
      pending_account_data_mask_ & ~(1u << binding_set_id));
  if (pending_account_data_mask_ == 0) {
    Finalize();
  }
}

bool BindingProfiles::ExecuteUnhandledBindingCommand(
    const BindingCommand& command, const bool key_down) {
  if (key_down) {
    if (!execute_fn_) return false;
    execute_fn_(command);
    return true;
  }
  if (!release_fn_) return false;
  release_fn_(command);
  return true;
}

bool BindingProfiles::RunNamedBinding(const BindingCommand& command,
                                        bool key_down,
                                        float pressure,
                                        bool pressure_available,
                                        bool alternate_pressure_state,
                                        bool angle_state,
                                        float angle,
                                        float precision,
                                        std::optional<std::uint16_t> modifier_state_override) {
  const BindingDefinition* definition =
      FindBindingDefinition(command.value());
  if (definition == nullptr) {
    return false;
  }

  return RunBindingDefinition(*definition,
                              key_down,
                              pressure,
                              pressure_available,
                              alternate_pressure_state,
                              angle_state,
                              angle,
                              precision,
                              modifier_state_override);
}

BindingDefinition* BindingProfiles::FindBindingDefinition(const std::string& command) {
  const auto it = binding_definition_by_command_.find(
      BindingCommand(UppercaseAscii(command)));
  if (it == binding_definition_by_command_.end()) {
    return nullptr;
  }
  return &binding_definitions_[it->second];
}

const BindingDefinition* BindingProfiles::FindBindingDefinition(
    const std::string& command) const {
  const auto it = binding_definition_by_command_.find(
      BindingCommand(UppercaseAscii(command)));
  if (it == binding_definition_by_command_.end()) {
    return nullptr;
  }
  return &binding_definitions_[it->second];
}

BindingProfiles::ModifiedClickBindingDefinition*
BindingProfiles::FindModifiedClickDefinition(
    const ModifiedClickAction& action) {
  const auto it = modified_click_index_by_action_.find(
      ModifiedClickAction(UppercaseAscii(action.value())));
  if (it == modified_click_index_by_action_.end()) {
    return nullptr;
  }
  return &modified_click_bindings_[it->second];
}

const BindingProfiles::ModifiedClickBindingDefinition*
BindingProfiles::FindModifiedClickDefinition(
    const ModifiedClickAction& action) const {
  const auto it = modified_click_index_by_action_.find(
      ModifiedClickAction(UppercaseAscii(action.value())));
  if (it == modified_click_index_by_action_.end()) {
    return nullptr;
  }
  return &modified_click_bindings_[it->second];
}

void BindingProfiles::RegisterModifiedClickDefinition(
    ModifiedClickAction action,
    ModifiedClickBindingState default_state) {
  const std::string lookup_action = UppercaseAscii(action.value());
  if (lookup_action.empty()
      || modified_click_index_by_action_.find(
             ModifiedClickAction(lookup_action))
             != modified_click_index_by_action_.end()) {
    return;
  }

  ModifiedClickBindingDefinition definition{std::move(action)};
  definition.states.fill(default_state);
  modified_click_index_by_action_.insert_or_assign(
      ModifiedClickAction(lookup_action), modified_click_bindings_.size());
  modified_click_bindings_.push_back(std::move(definition));
}

void BindingProfiles::RegisterBindingScript(
    BindingCommand command,
    BindingScriptExecutor executor) {
  if (executor) {
    binding_script_executors_.insert_or_assign(std::move(command),
                                               std::move(executor));
  }
}

bool BindingProfiles::RunBindingDefinition(const BindingDefinition& definition,
                                             bool key_down,
                                             float pressure,
                                             bool pressure_available,
                                             bool alternate_pressure_state,
                                             bool angle_state,
                                             float angle,
                                             float precision,
                                             std::optional<std::uint16_t> modifier_state_override) {
  if (!key_down && !definition.run_on_up) {
    return false;
  }
  if (definition.requires_pressure) {
    if (!pressure_available) {
      return false;
    }
  } else if (alternate_pressure_state) {
    return false;
  }
  if (definition.requires_angle != angle_state) {
    return false;
  }

  const auto script = binding_script_executors_.find(definition.command);
  if (script == binding_script_executors_.end()) {
    return true;
  }
  script->second({
      .pressed = key_down,
      .pressure = pressure,
      .angle = angle,
      .precision = precision,
      .modifier_state = modifier_state_override,
  });

  return true;
}

void BindingProfiles::RegisterBindingDefinition(BindingDefinition definition) {
  const std::size_t index = binding_definitions_.size();
  if (definition.display_index >= 0) {
    visible_binding_definitions_.push_back(index);
    next_visible_binding_index_ =
        std::max(next_visible_binding_index_, definition.display_index + 1);
  } else {
    next_hidden_binding_index_ =
        std::max(next_hidden_binding_index_, -definition.display_index);
  }
  binding_definition_by_command_[
      BindingCommand(UppercaseAscii(definition.command.value()))] = index;
  binding_definitions_.push_back(std::move(definition));
}

}
