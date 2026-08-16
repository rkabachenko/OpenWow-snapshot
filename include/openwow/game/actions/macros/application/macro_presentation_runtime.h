#pragma once

#include "openwow/game/actions/macros/model/macro_id.h"
#include "openwow/game/actions/macros/model/macro_document.h"
#include "openwow/game/actions/macros/rules/macro_icon_resolution_rules.h"

#include <cstdint>
#include <functional>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace openwow::game::actions::macros {

class MacroPresentationRuntime {
 public:
  using RefreshCallback = std::function<void(MacroId)>;
  using ActionBarSlotProvider = std::function<std::vector<MacroId>()>;
  using ShapeshiftSlotProvider =
      std::function<std::vector<std::uint32_t>()>;
  using CastSequenceTokenResolver =
      std::function<std::optional<std::string>(std::string_view)>;
  using MacrosChangedCallback = std::function<void()>;
  using IconResolutionQueries =
      rules::MacroIconResolutionQueries;
  using IconPathResolver =
      std::function<std::string(const MacroDocument&)>;
  using ClearActionBarMacro =
      std::function<void(MacroId)>;
  using ActiveShapeshiftFormProvider =
      std::function<std::uint32_t()>;

  void QueueIconUpdate(MacroId id);
  void RemoveIconUpdate(MacroId id);
  void ClearIconUpdates();
  [[nodiscard]] std::vector<MacroId> PendingIconUpdates() const;

  void SetRefreshCallback(RefreshCallback callback);
  [[nodiscard]] RefreshCallback Refresh() const;
  void SetActionBarSlotProvider(ActionBarSlotProvider provider);
  [[nodiscard]] ActionBarSlotProvider ActionBarSlots() const;
  void SetShapeshiftSlotProvider(ShapeshiftSlotProvider provider);
  [[nodiscard]] ShapeshiftSlotProvider ShapeshiftSlots() const;
  void SetCastSequenceTokenResolver(CastSequenceTokenResolver resolver);
  [[nodiscard]] CastSequenceTokenResolver CastSequenceToken() const;
  void SetMacrosChangedCallback(MacrosChangedCallback callback);
  [[nodiscard]] MacrosChangedCallback MacrosChanged() const;
  void SetIconResolutionQueries(IconResolutionQueries queries);
  [[nodiscard]] IconResolutionQueries IconResolution() const;
  void SetIconPathResolver(IconPathResolver resolver);
  [[nodiscard]] IconPathResolver IconPath() const;
  void SetClearActionBarMacro(ClearActionBarMacro callback);
  [[nodiscard]] ClearActionBarMacro ClearActionBar() const;
  void SetActiveShapeshiftFormProvider(
      ActiveShapeshiftFormProvider provider);
  [[nodiscard]] ActiveShapeshiftFormProvider ActiveShapeshiftForm() const;

 private:
  mutable std::mutex mutex_;
  std::list<MacroId> pending_icon_updates_;
  std::unordered_map<MacroId, std::list<MacroId>::iterator>
      pending_icon_update_positions_;
  RefreshCallback refresh_callback_;
  ActionBarSlotProvider action_bar_slot_provider_;
  ShapeshiftSlotProvider shapeshift_slot_provider_;
  CastSequenceTokenResolver cast_sequence_token_resolver_;
  MacrosChangedCallback macros_changed_callback_;
  IconResolutionQueries icon_resolution_queries_;
  IconPathResolver icon_path_resolver_;
  ClearActionBarMacro clear_action_bar_macro_;
  ActiveShapeshiftFormProvider active_shapeshift_form_provider_;
};

}
