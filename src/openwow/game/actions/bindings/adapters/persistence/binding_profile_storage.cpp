#include "openwow/game/actions/bindings/adapters/persistence/binding_profile_storage.h"

#include "openwow/game/actions/bindings/adapters/persistence/retail_binding_text_codec.h"
#include "openwow/game/actions/bindings/adapters/retail/modified_click_adapter.h"
#include "openwow/game/actions/bindings/application/binding_profiles.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <vector>

namespace openwow::game::actions::bindings::adapters::persistence {

void BindingProfileStorage::LoadFile(BindingProfiles& profiles,
                                     const std::string& path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                              "BindingProfiles: could not open " + path);
    return;
  }

  std::ostringstream buffer;
  buffer << file.rdbuf();
  ApplyText(profiles, buffer.str(), profiles.GetCurrentBindingSet());
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                            "BindingProfiles: loaded bindings from " + path);
}

void BindingProfileStorage::SaveFile(const BindingProfiles& profiles,
                                     const std::string& path) {
  std::size_t saved_count = 0;
  const std::string serialized =
      Serialize(profiles, profiles.GetCurrentBindingSet(), &saved_count);

  std::ofstream file(path);
  if (!file.is_open()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                              "BindingProfiles: could not write " + path);
    return;
  }
  file << serialized;
  openwow::diagnostics::Log(
      openwow::diagnostics::LogLevel::kInfo,
      "BindingProfiles: saved " + std::to_string(saved_count) +
          " bindings to " + path);
}

std::string BindingProfileStorage::Serialize(
    const BindingProfiles& profiles,
    const BindingProfileScope scope,
    std::size_t* const saved_count) {
  std::ostringstream out;
  std::size_t local_saved_count = 0;
  const auto snapshot = profiles.SnapshotBindings(scope);
  for (std::uint8_t slot_value = 0; slot_value < 4; ++slot_value) {
    std::vector<const BindingAssignment*> assignments;
    for (const auto& binding : snapshot) {
      if (binding.slot.value() == slot_value) {
        assignments.push_back(&binding);
      }
    }
    if (assignments.empty()) {
      continue;
    }

    std::sort(assignments.begin(), assignments.end(),
              [](const BindingAssignment* lhs,
                 const BindingAssignment* rhs) {
                if (lhs->index != rhs->index) {
                  return lhs->index < rhs->index;
                }
                return lhs->chord < rhs->chord;
              });

    out << "BINDINGMODE " << static_cast<int>(slot_value) << "\r\n";
    for (const BindingAssignment* binding : assignments) {
      out << "bind " << binding->chord.value() << " "
          << binding->command.value() << "\r\n";
      ++local_saved_count;
    }
  }

  for (const auto& modified_click : profiles.SnapshotModifiedClicks(scope)) {
    const auto& state = modified_click.state;
    if (!state.skip_serialization) {
      out << "modifiedclick "
          << SerializeRetailModifiedClick(
                 state.modifier_bits, state.button_index,
                 state.has_button_token)
          << " " << modified_click.action.value() << "\r\n";
    }
  }

  if (saved_count != nullptr) {
    *saved_count = local_saved_count;
  }
  return out.str();
}

void BindingProfileStorage::ApplyText(BindingProfiles& profiles,
                                      const std::string_view text,
                                      const BindingProfileScope target_scope) {
  if (target_scope != BindingProfileScope::kDefault &&
      target_scope != BindingProfileScope::kAccount &&
      target_scope != BindingProfileScope::kCharacter) {
    return;
  }
  if (target_scope != BindingProfileScope::kDefault) {
    profiles.CopyBindingSet(BindingProfileScope::kDefault, target_scope);
  }

  const auto document = ParseRetailBindingDocument(text);
  for (const auto& binding : document.bindings) {
    (void)profiles.AssignBinding(target_scope, binding.slot, binding.chord,
                                 binding.command);
  }
  for (const auto& modified_click : document.modified_clicks) {
    (void)::openwow::game::actions::bindings::adapters::retail::
        SetModifiedClickBinding(
        profiles, modified_click.action.value(), modified_click.binding,
        target_scope);
  }
}

}
