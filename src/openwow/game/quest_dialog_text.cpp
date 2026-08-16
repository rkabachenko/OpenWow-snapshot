
#include "openwow/game/quest_dialog_text.h"
#include "openwow/data/formats/dbc/dbc_loader.h"

#include "openwow/game/conditional_text_tag.h"
#include "openwow/game/localization.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/power_lua_bridge.h"
#include "openwow/game/world_session.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <string_view>

namespace openwow::game {
namespace {

constexpr std::size_t kQuestDialogTextBufferChars = 2999;

struct QuestDialogTextSubject {
  std::string name;
  std::string class_name;
  std::string race_name;
  std::uint8_t class_id = 0;
  std::uint8_t race_id = 0;
  std::uint8_t gender = 0;
  int gender_selector = 0;
  int class_selector = 0;
  int race_selector = 0;
  bool has_context = false;
};

void AppendQuestDialogText(std::string& output, std::string_view text) {
  if (output.size() >= kQuestDialogTextBufferChars || text.empty()) {
    return;
  }

  const auto remaining = kQuestDialogTextBufferChars - output.size();
  output.append(text.substr(0, remaining));
}

void AppendQuestDialogChar(std::string& output, char ch) {
  if (output.size() < kQuestDialogTextBufferChars) {
    output.push_back(ch);
  }
}

std::string LowercaseAscii(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char ch) {
                   return static_cast<char>(std::tolower(ch));
                 });
  return text;
}

std::string FormatLocalizedInt(const std::string& key,
                               const char* fallback_format,
                               int value) {
  const auto format = Localization::Get().GetString(key, fallback_format);
  std::array<char, 64> buffer{};
  FormatRuntimeStringTemplateInto(buffer.data(), buffer.size(), format.c_str(), value);
  return buffer.data();
}

std::string FormatQuestDurationMinutes(int minutes) {
  if (minutes < 1440) {
    if (minutes < 60) {
      return FormatLocalizedInt("INT_GENERAL_DURATION_MIN", "%d min", minutes);
    }

    const auto hours = minutes / 60 + ((minutes % 60) >= 30 ? 1 : 0);
    return FormatLocalizedInt("INT_GENERAL_DURATION_HOURS", "%d hours", hours);
  }

  const auto days = ((minutes - 1) / 1440) + 1;
  return FormatLocalizedInt("INT_GENERAL_DURATION_DAYS", "%d days", days);
}

QuestDialogTextSubject ResolveQuestDialogTextSubject(
    const WorldSession& session) {
  QuestDialogTextSubject subject;
  const auto guid = session.objects().GetActivePlayerGuid();
  if (guid.IsEmpty()) {
    return subject;
  }

  const auto* object = session.objects().Get(guid);
  if (object != nullptr) {
    subject.has_context = true;
    if (object->IsUnit()) {
      const auto* unit = static_cast<const CGUnit_C*>(object);

      subject.name = unit->ResolveRetailName(session);
      subject.class_id = unit->State().GetClass();
      subject.race_id = unit->State().GetRace();
      subject.gender = unit->State().GetGender();

      if (object->IsPlayer() && subject.gender == 2) {
        subject.gender =
            static_cast<const CGPlayer_C*>(object)->GetGenderFromBytes();
      }
    }
  }

  if (subject.name.empty()) {
    subject.name = session.objects().GetPlayerName(guid);
  }

  if (const auto* name_info =
          session.query_cache().GetPlayerName(guid.GetRawValue())) {
    subject.has_context = true;
    if (subject.name.empty()) {
      subject.name = name_info->name;
    }
    if (subject.class_id == 0) {
      subject.class_id = name_info->class_id;
    }
    if (subject.race_id == 0) {
      subject.race_id = name_info->race;
    }
    if (subject.gender == 0) {
      subject.gender = name_info->sex;
    }
  }

  if (const auto* name_entry = session.objects().GetNameEntry(guid)) {
    subject.has_context = true;
    if (subject.name.empty()) {
      subject.name = name_entry->name;
    }
    if (subject.class_id == 0) {
      subject.class_id = name_entry->class_id;
    }
    if (subject.race_id == 0) {
      subject.race_id = name_entry->race;
    }
    if (subject.gender == 0) {
      subject.gender = name_entry->gender;
    }
  }

  subject.gender_selector = subject.gender;
  subject.class_selector = subject.gender_selector;
  subject.race_selector = subject.gender_selector;

  if (const auto* dbc = session.GetDbcLoader()) {
    if (subject.class_id != 0) {
      if (const auto* class_entry =
              dbc->chr_classes().LookupEntry(subject.class_id)) {
        subject.class_name = std::string(
            class_entry->DisplayNameForSex(
                static_cast<std::uint32_t>(subject.gender_selector)));
        subject.class_selector = static_cast<int>(
            class_entry->ResolveDisplaySex(
                static_cast<std::uint32_t>(subject.gender_selector)));
      }
    }
    if (subject.race_id != 0) {
      if (const auto* race_entry = dbc->chr_races().LookupEntry(subject.race_id)) {
        subject.race_name = std::string(
            race_entry->DisplayNameForSex(
                static_cast<std::uint32_t>(subject.gender_selector)));
        subject.race_selector = static_cast<int>(
            race_entry->ResolveDisplaySex(
                static_cast<std::uint32_t>(subject.gender_selector)));
      }
    }
  }

  if (subject.class_id != 0) {
    if (subject.class_name.empty()) {
      subject.class_name = PowerLuaBridge::ClassNameFromId(subject.class_id);
    }
  }
  if (subject.race_id != 0) {
    if (subject.race_name.empty()) {
      subject.race_name = PowerLuaBridge::RaceNameFromId(subject.race_id);
    }
  }
  return subject;
}

bool TryExpandQuestDialogToken(const QuestDialogTextSubject& subject,
                               std::string_view raw_text,
                               std::size_t* cursor,
                               std::string& output) {
  int numeric_prefix = 0;
  bool has_numeric_prefix = false;
  while (*cursor < raw_text.size() &&
         std::isdigit(static_cast<unsigned char>(raw_text[*cursor])) != 0) {
    has_numeric_prefix = true;
    numeric_prefix = numeric_prefix * 10 + (raw_text[*cursor] - '0');
    ++*cursor;
  }

  if (*cursor >= raw_text.size()) {
    return false;
  }

  const auto token = raw_text[*cursor];
  switch (token) {
    case 'A':
    case 'a':
      ++*cursor;
      return true;

    case 'B':
    case 'b':
      AppendQuestDialogChar(output, '\n');
      ++*cursor;
      return true;

    case 'C':
    case 'c':
      if (subject.class_name.empty()) {
        return false;
      }
      AppendQuestDialogText(
          output, token == 'c' ? LowercaseAscii(subject.class_name)
                               : subject.class_name);
      ++*cursor;
      return true;

    case 'D':
    case 'd':
      if (!has_numeric_prefix) {
        return false;
      }
      AppendQuestDialogText(output, FormatQuestDurationMinutes(numeric_prefix));
      ++*cursor;
      return true;

    case 'E':
    case 'e':
      if (!has_numeric_prefix) {
        return false;
      }
      AppendQuestDialogText(output, std::to_string(-numeric_prefix));
      ++*cursor;
      return true;

    case 'G':
    case 'g': {
      if (!subject.has_context) {
        return false;
      }

      ConditionalTextTagSelection selection;
      ConditionalTextTagContext context;
      context.selector = subject.gender_selector;
      context.class_selector = subject.class_selector;
      context.race_selector = subject.race_selector;
      if (!TrySelectConditionalTextTag(
              raw_text.substr(*cursor + 1),
              context,
              &selection)) {
        return false;
      }
      AppendQuestDialogText(output, selection.text);
      *cursor += 1 + selection.consumed;
      return true;
    }

    case 'N':
    case 'n':
      if (subject.name.empty()) {
        return false;
      }
      AppendQuestDialogText(output, subject.name);
      ++*cursor;
      return true;

    case 'R':
    case 'r':
      if (subject.race_name.empty()) {
        return false;
      }
      AppendQuestDialogText(
          output, token == 'r' ? LowercaseAscii(subject.race_name)
                               : subject.race_name);
      ++*cursor;
      return true;

    case 'W':
    case 'w':
      if (!has_numeric_prefix) {
        return false;
      }
      AppendQuestDialogText(output, std::to_string(numeric_prefix));
      ++*cursor;
      return true;

    default:
      return false;
  }
}

std::string ExpandQuestDialogText(const QuestDialogTextSubject& subject,
                                  std::string_view raw_text,
                                  bool empty_as_space) {
  std::string expanded;
  expanded.reserve(std::min(raw_text.size(), kQuestDialogTextBufferChars));

  std::size_t cursor = 0;
  while (cursor < raw_text.size()) {
    const auto dollar = raw_text.find('$', cursor);
    if (dollar == std::string_view::npos) {
      AppendQuestDialogText(expanded, raw_text.substr(cursor));
      break;
    }

    AppendQuestDialogText(expanded, raw_text.substr(cursor, dollar - cursor));
    cursor = dollar + 1;
    if (cursor >= raw_text.size()) {
      AppendQuestDialogChar(expanded, '$');
      break;
    }

    if (!TryExpandQuestDialogToken(subject, raw_text, &cursor, expanded)) {
      AppendQuestDialogChar(expanded, '$');
    }
  }

  if (expanded.empty() && empty_as_space) {
    return " ";
  }
  return expanded;
}

}

std::string ExpandQuestDialogText(std::string_view raw_text,
                                  bool empty_as_space) {
  return ExpandQuestDialogText(QuestDialogTextSubject{}, raw_text,
                               empty_as_space);
}

std::string ExpandQuestDialogText(const WorldSession& session,
                                  std::string_view raw_text,
                                  bool empty_as_space) {
  return ExpandQuestDialogText(ResolveQuestDialogTextSubject(session), raw_text,
                               empty_as_space);
}

}
