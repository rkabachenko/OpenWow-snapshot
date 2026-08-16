#pragma once
#include "openwow/game/actions/macros/application/macro_condition_runtime.h"
#include "openwow/game/actions/macros/application/macro_execution_runtime.h"
#include "openwow/game/actions/macros/application/macro_icon_library.h"
#include "openwow/game/actions/macros/application/macro_presentation_runtime.h"
#include "openwow/game/actions/macros/model/macro_document.h"
#include "openwow/game/actions/macros/model/macro_id.h"
#include "openwow/game/actions/macros/model/macro_store.h"
#include "openwow/game/actions/macros/rules/macro_body_rules.h"
#include "openwow/game/actions/macros/rules/secure_command_option_parser.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::game {

using actions::macros::MacroId;

class MacroCatalog {
 public:
    MacroCatalog() = default;

    static constexpr uint32_t kMaxAccountMacros     = 36;
    static constexpr uint32_t kMaxCharacterMacros   = 18;
    static constexpr uint32_t kCharacterMacroOffset  = 36;
    static constexpr uint32_t kTotalSlots            = 72;
    static constexpr uint32_t kMaxBodyLength =
        actions::macros::rules::MacroBodyRules::kMaxBodyLength;
    static constexpr uint32_t kMaxBodyCodepoints =
        actions::macros::rules::MacroBodyRules::kMaxBodyCodepoints;

    static constexpr uint32_t kMaxNameLength         = 63;

    void SetMacro(MacroId id, const MacroDocument& macro);
    void DeleteMacro(MacroId id);
    [[nodiscard]] std::optional<MacroDocument> FindMacro(MacroId id) const;
    bool UpdateMacro(
        MacroId id,
        const std::function<void(MacroDocument&)>& update);

    [[nodiscard]] size_t GetNumAccountMacros() const;
    [[nodiscard]] size_t GetNumCharacterMacros() const;

    [[nodiscard]] std::vector<MacroDocument> GetAccountMacros() const;
    [[nodiscard]] std::vector<MacroDocument> GetCharacterMacros() const;

    [[nodiscard]] std::optional<MacroDocument> FindMacroByName(
        std::string_view name) const;

    [[nodiscard]] int32_t FindSlotIndex(MacroId id) const;

    [[nodiscard]] int32_t FindSlotIndexByName(const std::string& name) const;

    [[nodiscard]] uint32_t GetMacroIndexByName(const std::string& name) const;

    [[nodiscard]] std::optional<MacroDocument> FindMacroAtSlot(
        uint32_t slot_index) const;
    bool UpdateMacroAtSlot(
        uint32_t slot_index,
        const std::function<void(MacroDocument&)>& update);

    [[nodiscard]] MacroId CreateMacro(const std::string& name,
                                      uint32_t icon_id,
                                      const std::string& body,
                                      MacroScope scope);

    MacroId EditMacro(MacroId id, const std::string& name,
                      uint32_t icon_id, const std::string& body);

    void SetBodyText(uint32_t slot_index, const std::string& body);

    void ExecuteBody(
        uint32_t slot_index,
        std::optional<actions::macros::MacroInputButton> button =
            std::nullopt);
    void ExecuteBodyText(
        const std::string& body,
        std::optional<actions::macros::MacroInputButton> button =
            std::nullopt);

    void ExecuteByUniqueId(
        MacroId id,
        std::optional<actions::macros::MacroInputButton> button =
            std::nullopt);

    void StopMacro();
    [[nodiscard]] bool IsRunningMacro() const;

    [[nodiscard]] std::optional<MacroId> GetRunningMacroId() const;
    [[nodiscard]] int32_t GetRunningMacroSlot() const;
    [[nodiscard]] std::optional<actions::macros::MacroInputButton>
    RunningMacroInputButton() const;
    bool WithTransientButtonContext(std::string_view button,
                                    const std::function<bool()>& fn);
    [[nodiscard]] bool HasRetailProtectedActionFlag(std::uint32_t flag_mask) const;
    void ConsumeRetailProtectedActionFlag(std::uint32_t flag_mask);

    [[nodiscard]] std::string ResolveMacroBody(const std::string& body) const;
    std::string EvaluateConditional(const std::string& conditional) const;
    using SecureCommandOptionResult =
        actions::macros::rules::SecureCommandOptionResult;
    [[nodiscard]] SecureCommandOptionResult ParseSecureCommandOptions(
        std::string_view options) const;
    using ModifiedClickConditionQuery =
        actions::macros::MacroConditionRuntime::ModifiedClickQuery;
    void SetModifiedClickConditionQuery(ModifiedClickConditionQuery query);
    [[nodiscard]] std::optional<bool> QueryModifiedClickCondition(
        std::optional<std::string_view> action,
        std::uint16_t modifier_state,
        std::string_view mouse_button) const;
    using UnknownConditionHandler =
        actions::macros::MacroConditionRuntime::UnknownConditionHandler;
    void SetUnknownConditionHandler(UnknownConditionHandler handler);
    using ConditionSnapshotProvider =
        actions::macros::MacroConditionRuntime::SnapshotProvider;
    void SetConditionSnapshotProvider(ConditionSnapshotProvider provider);

    void LoadIconList();
    [[nodiscard]] uint32_t GetNumMacroIcons() const;
    [[nodiscard]] uint32_t GetNumMacroItemIcons() const;
    [[nodiscard]] std::optional<std::string> MacroIconName(
        uint32_t index) const;
    [[nodiscard]] std::optional<std::string> MacroItemIconName(
        uint32_t index) const;

    void InitializeUiSession();

    void ClearAll();

    void Reset();

    void RebuildSlotArray();

    void MarkDirty();
    [[nodiscard]] bool IsDirty() const;
    void ClearDirty();

    void RebuildActionBarReferences();
    void IncrementActionBarLinks(MacroId id);
    void DecrementActionBarLinks(MacroId id);
    void ClearActionBarReferencesForMacro(MacroId id);

    void ReplaceMacros(MacroScope scope, std::vector<MacroDocument> macros);
    [[nodiscard]] std::vector<MacroDocument> SnapshotMacros(
        MacroScope scope) const;
    [[nodiscard]] bool ConsumeDirty();

    std::string GetIconPath(MacroId id);

    void UpdateDirtyIcons();

    void UpdateAllPendingIcons();

    void UpdateActionBarLinks(MacroId id);
    using ActionBarRefreshCallback = std::function<void(MacroId id)>;
    void SetActionBarRefreshCallback(ActionBarRefreshCallback callback);
    void NotifyActionBarRefresh(MacroId id) const;
    using ActionBarMacroSlotProvider = std::function<std::vector<MacroId>()>;
    void SetActionBarMacroSlotProvider(ActionBarMacroSlotProvider provider);
    using ShapeshiftSlotProvider = std::function<std::vector<uint32_t>()>;
    void SetShapeshiftSlotProvider(ShapeshiftSlotProvider provider);
    using ActiveShapeshiftFormProvider =
        actions::macros::MacroPresentationRuntime::
            ActiveShapeshiftFormProvider;
    void SetActiveShapeshiftFormProvider(
        ActiveShapeshiftFormProvider provider);
    using ClearActionBarMacro =
        actions::macros::MacroPresentationRuntime::ClearActionBarMacro;
    void SetClearActionBarMacro(ClearActionBarMacro callback);
    [[nodiscard]] std::uint32_t GetRetailShapeshiftFormIndex() const;

    [[nodiscard]] std::vector<std::string> GetMacroIconList();

    using ChatCommandHandler =
        actions::macros::MacroExecutionRuntime::CommandHandler;
    void SetChatCommandHandler(ChatCommandHandler handler);
    using ProtectionGate =
        actions::macros::MacroExecutionRuntime::ProtectionGate;
    void SetProtectionGate(ProtectionGate gate);
    [[nodiscard]] bool CanPerform(
        actions::macros::MacroProtectedOperation operation) const;
    using CastSequenceTokenResolver =
        actions::macros::MacroPresentationRuntime::CastSequenceTokenResolver;
    void SetCastSequenceTokenResolver(CastSequenceTokenResolver resolver);
    [[nodiscard]] std::optional<std::string> ResolveCastSequenceToken(
        std::string_view body) const;
    using IconResolutionQueries =
        actions::macros::MacroPresentationRuntime::IconResolutionQueries;
    void SetIconResolutionQueries(IconResolutionQueries queries);
    using IconPathResolver =
        actions::macros::MacroPresentationRuntime::IconPathResolver;
    void SetIconPathResolver(IconPathResolver resolver);
    using MacrosChangedCallback =
        actions::macros::MacroPresentationRuntime::MacrosChangedCallback;
    void SetMacrosChangedCallback(MacrosChangedCallback callback);

 private:
    void DispatchExecutableLinesLocked(const std::string& body);
    [[nodiscard]] bool CanPerformLocked(
        actions::macros::MacroProtectedOperation operation) const;
    [[nodiscard]] const MacroDocument* FindMacroByNameLocked(std::string_view name) const;
    [[nodiscard]] int32_t FindSlotIndexLocked(MacroId id) const;
    void RebuildSlotArrayLocked();
    void NotifyMacrosChanged() const;
    void PromoteMacroLookupOrderLocked(MacroId id);
    void RemoveMacroLookupOrderLocked(MacroId id);
    void QueuePendingActionBarIconUpdateLocked(MacroId id);
    void UnqueuePendingActionBarIconUpdateLocked(MacroId id);
    void IncrementActionBarLinksLocked(MacroDocument& macro);
    void DecrementActionBarLinksLocked(MacroDocument& macro);
    void SyncPendingActionBarIconUpdateLocked(const MacroDocument& macro);
    bool ApplyBodyTextAndResolveLocked(MacroDocument& macro, std::string_view body);
    [[nodiscard]] std::string ResolveMacroIconName(uint32_t icon_index);
    [[nodiscard]] actions::macros::rules::MacroIconResolution
    ResolveIconStateFromBodyLocked(const MacroDocument& macro) const;

    mutable actions::macros::rules::SecureCommandOptionParser
        secure_command_option_parser_;

    actions::macros::MacroExecutionRuntime execution_runtime_;
    actions::macros::MacroConditionRuntime condition_runtime_;

    actions::macros::MacroIconLibrary icon_library_;
    std::uint16_t account_data_generation_ = 0xFFFF;

    actions::macros::MacroStore store_;

    actions::macros::MacroPresentationRuntime
        presentation_runtime_;
    mutable std::recursive_mutex mutex_;
};

}
