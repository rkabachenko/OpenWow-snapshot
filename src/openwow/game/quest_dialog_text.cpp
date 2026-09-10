#include "openwow/game/quest_dialog_text.h"

#include "openwow/game/object_manager.h"
#include "openwow/game/spell_text_formatter.h"
#include "openwow/game/world_session.h"

#include <array>
#include <ctime>

namespace openwow::game {
namespace {

constexpr std::size_t kQuestDialogTextBufferSize = 3000;

std::string FinishQuestDialogText(const char* expanded, bool empty_as_space) {
  return expanded[0] == '\0' && empty_as_space ? " " : expanded;
}

}

std::string ExpandQuestDialogText(std::string_view raw_text,
                                  bool empty_as_space) {
  std::array<char, kQuestDialogTextBufferSize> expanded{};
  SpellTextFormatter::ExpandObjectTextVariables(
      std::string(raw_text).c_str(), expanded.data(), static_cast<std::uint32_t>(expanded.size()),
      0, nullptr, 0);
  return FinishQuestDialogText(expanded.data(), empty_as_space);
}

std::string ExpandQuestDialogText(const WorldSession& session,
                                  std::string_view raw_text,
                                  bool empty_as_space) {
  std::array<char, kQuestDialogTextBufferSize> expanded{};
  const auto guid = session.objects().GetActivePlayerGuid();
  auto name = session.objects().GetPlayerName(guid);
  SpellTextFormatter::ExpandObjectTextVariables(
      std::string(raw_text).c_str(), expanded.data(), static_cast<std::uint32_t>(expanded.size()),
      guid.GetRawValue(), name.empty() ? nullptr : name.data(), static_cast<std::int32_t>(name.size()),
      [&session](std::int32_t variable_id) {
        return session.world_states().GetWorldState(variable_id);
      },
      session.world_states().world_state_ui_current_time_seconds(std::time(nullptr)),
      0, &session.objects(), session.GetDbcLoader());
  return FinishQuestDialogText(expanded.data(), empty_as_space);
}

bool ExpandServerTextTokens(const ObjectManager& objects,
                            const data::dbc::DbcLoader* dbc_loader,
                            ObjectGuid subject_guid,
                            std::string_view raw_text,
                            std::string& expanded) {
  if (subject_guid.IsEmpty()) {
    subject_guid = objects.GetActivePlayerGuid();
  }
  std::array<char, kQuestDialogTextBufferSize> output{};
  const bool resolved = SpellTextFormatter::ExpandObjectTextVariables(
      std::string(raw_text).c_str(), output.data(), static_cast<std::uint32_t>(output.size()),
      subject_guid.GetRawValue(), nullptr, 0, {}, 0, 0, &objects, dbc_loader);
  expanded = output.data();
  return resolved;
}

}
