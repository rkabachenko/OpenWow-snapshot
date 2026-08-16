#pragma once
#include "openwow/game/actions/bindings/model/binding_types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

struct lua_State;

namespace openwow::vfs {
class VirtualFileSystem;
}

namespace openwow::game {

class MacroCatalog;

enum class BindingProtectedOperation : std::uint8_t {
  kChangeRuntimeState,
  kCopyProfile,
};

struct BindingDefinition {
  explicit BindingDefinition(BindingCommand binding_command)
      : command(std::move(binding_command)) {}

  BindingCommand command;
  int display_index{-1};
  bool run_on_up{false};
  bool requires_pressure{false};
  bool requires_angle{false};
  bool is_header{false};
};

struct BindingDisplayEntry {
  BindingCommand command;
  std::vector<BindingChord> chords;
};

namespace BindingAction {
  inline constexpr const char* kMoveForward       = "MOVEFORWARD";
  inline constexpr const char* kMoveBackward      = "MOVEBACKWARD";
  inline constexpr const char* kTurnLeft          = "TURNLEFT";
  inline constexpr const char* kTurnRight         = "TURNRIGHT";
  inline constexpr const char* kStrafeLeft        = "STRAFELEFT";
  inline constexpr const char* kStrafeRight       = "STRAFERIGHT";
  inline constexpr const char* kJump              = "JUMP";

  inline constexpr const char* kSitStand          = "SITORSTAND";
  inline constexpr const char* kToggleAutoRun     = "TOGGLEAUTORUN";
  inline constexpr const char* kPitchUp           = "PITCHUP";
  inline constexpr const char* kPitchDown         = "PITCHDOWN";

  inline constexpr const char* kToggleRun         = "TOGGLERUN";
  inline constexpr const char* kActionButton1     = "ACTIONBUTTON1";
  inline constexpr const char* kActionButton2     = "ACTIONBUTTON2";
  inline constexpr const char* kActionButton3     = "ACTIONBUTTON3";
  inline constexpr const char* kActionButton4     = "ACTIONBUTTON4";
  inline constexpr const char* kActionButton5     = "ACTIONBUTTON5";
  inline constexpr const char* kActionButton6     = "ACTIONBUTTON6";
  inline constexpr const char* kActionButton7     = "ACTIONBUTTON7";
  inline constexpr const char* kActionButton8     = "ACTIONBUTTON8";
  inline constexpr const char* kActionButton9     = "ACTIONBUTTON9";
  inline constexpr const char* kActionButton10    = "ACTIONBUTTON10";
  inline constexpr const char* kActionButton11    = "ACTIONBUTTON11";
  inline constexpr const char* kActionButton12    = "ACTIONBUTTON12";
  inline constexpr const char* kMultiBar1_1       = "MULTIACTIONBAR1BUTTON1";
  inline constexpr const char* kMultiBar1_2       = "MULTIACTIONBAR1BUTTON2";
  inline constexpr const char* kMultiBar1_3       = "MULTIACTIONBAR1BUTTON3";
  inline constexpr const char* kMultiBar1_4       = "MULTIACTIONBAR1BUTTON4";
  inline constexpr const char* kMultiBar1_5       = "MULTIACTIONBAR1BUTTON5";
  inline constexpr const char* kMultiBar1_6       = "MULTIACTIONBAR1BUTTON6";
  inline constexpr const char* kTargetNearestEnemy    = "TARGETNEARESTENEMY";
  inline constexpr const char* kTargetPreviousEnemy   = "TARGETPREVIOUSENEMY";
  inline constexpr const char* kTargetNearestFriend   = "TARGETNEARESTFRIEND";
  inline constexpr const char* kAssistTarget          = "ASSISTTARGET";
  inline constexpr const char* kFocusTarget           = "FOCUSTARGET";
  inline constexpr const char* kToggleCharacter       = "TOGGLECHARACTER0";
  inline constexpr const char* kToggleSpellBook       = "TOGGLESPELLBOOK";
  inline constexpr const char* kToggleTalents         = "TOGGLETALENTS";
  inline constexpr const char* kToggleQuestLog        = "TOGGLEQUESTLOG";
  inline constexpr const char* kToggleSocial          = "TOGGLESOCIAL";
  inline constexpr const char* kToggleWorldMap        = "TOGGLEWORLDMAP";
  inline constexpr const char* kMinimapZoomIn         = "MINIMAPZOOMIN";
  inline constexpr const char* kMinimapZoomOut        = "MINIMAPZOOMOUT";
  inline constexpr const char* kOpenChat              = "OPENCHAT";
  inline constexpr const char* kOpenChatSlash         = "OPENCHATSLASH";
  inline constexpr const char* kReply                 = "REPLY";
  inline constexpr const char* kToggleBag1            = "TOGGLEBAG1";
  inline constexpr const char* kToggleBag2            = "TOGGLEBAG2";
  inline constexpr const char* kToggleBag3            = "TOGGLEBAG3";
  inline constexpr const char* kToggleBag4            = "TOGGLEBAG4";
  inline constexpr const char* kToggleBackpack        = "TOGGLEBACKPACK";
  inline constexpr const char* kEnemyNameplates      = "NAMEPLATES";
  inline constexpr const char* kFriendlyNameplates   = "FRIENDNAMEPLATES";
  inline constexpr const char* kAllNameplates        = "ALLNAMEPLATES";

  inline constexpr const char* kToggleGameMenu        = "TOGGLEGAMEMENU";
  inline constexpr const char* kScreenshot            = "SCREENSHOT";
}

class BindingProfiles {
 public:
  enum class BindingSlotSelector : std::uint8_t {
    kMode0   = 0,
    kMode1   = 1,
    kMode2   = 2,
    kMode3   = 3,
    kCurrent = 4,
  };

  struct OverrideOwner {
    [[nodiscard]] static OverrideOwner FromStableTag(std::string_view token);
    [[nodiscard]] static OverrideOwner FromLuaFrameReference(int lua_reference);

    [[nodiscard]] const std::string& token() const { return token_; }
    [[nodiscard]] bool operator==(const OverrideOwner& rhs) const {
      return token_ == rhs.token_;
    }

   private:
    explicit OverrideOwner(std::string token) : token_(std::move(token)) {}

    std::string token_;
  };

  BindingProfiles() = default;
  ~BindingProfiles() { Shutdown(); }

  bool Initialize();

  void Shutdown();

  void BeginWorldUiSession();

  void EndWorldUiSession();

  [[nodiscard]] int GetNumBindings() const;

  [[nodiscard]] int GetNumHiddenBindings() const;

  [[nodiscard]] int GetNumModifiedClickActions() const;

  [[nodiscard]] BindingProfileScope GetCurrentBindingSet() const;

  [[nodiscard]] static bool IsValidBindingKeyName(std::string_view key);

  [[nodiscard]] bool AssignBinding(BindingProfileScope scope,
                                   BindingSlot slot,
                                   BindingChord chord,
                                   BindingCommand command);
  [[nodiscard]] std::vector<BindingChord> ChordsForCommand(
      const BindingCommand& command,
      BindingProfileScope scope = BindingProfileScope::kActive,
      BindingSlot slot = BindingSlot::Primary()) const;
  [[nodiscard]] std::optional<BindingCommand> ResolveBinding(
      const BindingChord& chord,
      bool check_override = true,
      BindingProfileScope scope = BindingProfileScope::kActive,
      BindingSlot slot = BindingSlot::Primary()) const;

  [[nodiscard]] std::optional<BindingDisplayEntry> BindingAt(
      int display_index,
      BindingProfileScope scope,
      BindingSlot slot = BindingSlot::Primary()) const;
  [[nodiscard]] std::vector<BindingChord> ChordsForCommandInActiveSlots(
      const BindingCommand& command,
      BindingSlotSelector selector) const;
  [[nodiscard]] std::optional<BindingCommand> ResolveBindingInActiveSlots(
      const BindingChord& chord,
      bool check_override,
      BindingSlotSelector selector) const;
  [[nodiscard]] std::optional<BindingCommand> ResolveChordWithFallback(
      const BindingChord& chord,
      BindingProfileScope scope = BindingProfileScope::kActive) const;
  [[nodiscard]] std::optional<BindingResolution>
  ResolveChordWithFallbackDetailed(
      const BindingChord& chord,
      BindingProfileScope scope = BindingProfileScope::kActive) const;
  [[nodiscard]] std::optional<BindingCommand>
  ResolveChordWithFallbackInActiveSlots(
      const BindingChord& chord,
      BindingSlotSelector selector) const;
  [[nodiscard]] std::vector<BindingAssignment> SnapshotBindings(
      BindingProfileScope scope) const;
  [[nodiscard]] std::vector<ModifiedClickAssignment> SnapshotModifiedClicks(
      BindingProfileScope scope) const;
  [[nodiscard]] std::optional<ModifiedClickAction> ModifiedClickActionAt(
      std::size_t index) const;
  [[nodiscard]] std::optional<ModifiedClickBindingState> ModifiedClickBinding(
      const ModifiedClickAction& action,
      BindingProfileScope scope = BindingProfileScope::kActive) const;
  [[nodiscard]] bool AssignModifiedClick(
      const ModifiedClickAction& action,
      ModifiedClickBindingState state,
      BindingProfileScope scope = BindingProfileScope::kActive);
  [[nodiscard]] bool IsModifiedClickActive(
      const std::optional<ModifiedClickAction>& action,
      const ModifiedClickInputState& input) const;

  void SetCurrentBindingStateBit(std::uint8_t bit_index, bool enabled);

  void SetOverrideBinding(const OverrideOwner& owner, bool is_priority,
                          BindingChord chord,
                          std::optional<BindingCommand> command);

  void ClearOverrideBindings(const OverrideOwner& owner);

  void SaveBindings(BindingProfileScope mode);

  [[nodiscard]] BindingProfileLoadGeneration BeginProfileLoad();
  [[nodiscard]] bool AcceptsProfileLoad(
      BindingProfileLoadGeneration generation) const noexcept;
  void CompleteProfileLoad(BindingProfileLoadGeneration generation,
                           BindingProfileScope scope);
  [[nodiscard]] std::vector<BindingProfileScope>
  PersistentProfilesNeedingSave() const;

  void LoadBindings(BindingProfileScope mode);

  using ExecuteFn = std::function<void(const BindingCommand& command)>;
  void SetExecuteCallback(ExecuteFn fn);

  using ReleaseExecuteFn = std::function<void(const BindingCommand& command)>;
  void SetReleaseCallback(ReleaseExecuteFn fn);

  using ProtectionGate = std::function<bool(BindingProtectedOperation)>;
  void SetProtectionGate(ProtectionGate gate);

  void SetVfs(const openwow::vfs::VirtualFileSystem* vfs);

  using JoystickNameProvider = std::function<std::optional<std::string>()>;

  void SetJoystickNameProvider(JoystickNameProvider provider);

  void LoadDefaults();

  void ResetBindingDefinitions();

  void ResetWorldUiRuntimeState();
  [[nodiscard]] bool HasLoadedBindingDefinitions() const;
  [[nodiscard]] bool HasBindingDefinition(
      const BindingCommand& command) const;
  [[nodiscard]] bool HasModifiedClickDefinition(
      const ModifiedClickAction& action) const;
  [[nodiscard]] bool HasBindingDefinitionJoystick() const;
  void RegisterBindingDefinition(BindingDefinition definition);
  using BindingScriptExecutor =
      std::function<void(const BindingInvocation&)>;
  void RegisterBindingScript(BindingCommand command,
                             BindingScriptExecutor executor);
  void RegisterModifiedClickDefinition(
      ModifiedClickAction action,
      ModifiedClickBindingState default_state);
  void AddParsedDefaultBinding(BindingChord chord, BindingCommand command);

  void CopyBindingSet(BindingProfileScope src, BindingProfileScope dst);

  void Finalize();

  [[nodiscard]] bool ExecuteUnhandledBindingCommand(
      const BindingCommand& command,
      bool key_down);

  bool RunNamedBinding(const BindingCommand& command,
                       bool key_down,
                       float pressure = 1.0f,
                       bool pressure_available = true,
                       bool alternate_pressure_state = false,
                       bool angle_state = false,
                       float angle = -1.0f,
                       float precision = 0.0f,
                       std::optional<std::uint16_t> modifier_state_override = std::nullopt);

 private:
  friend struct BindingProfilesTestAccess;

  std::vector<BindingAssignment> bindings_;

  struct OverrideBindingNode {
    OverrideOwner owner;
    BindingCommand command;
  };

  struct OverrideKeyEntry {
    std::vector<OverrideBindingNode> low_priority;
    std::vector<OverrideBindingNode> high_priority;
  };
  std::unordered_map<BindingChord, OverrideKeyEntry> override_bindings_;

  ExecuteFn execute_fn_;
  ReleaseExecuteFn release_fn_;
  ProtectionGate protection_gate_;

  std::unordered_map<BindingChord, BindingCommand>
      active_mode0_key_to_command_;

  std::unordered_set<BindingCommand> recognized_binding_commands_;

  std::vector<BindingDefinition> binding_definitions_;
  std::unordered_map<BindingCommand, std::size_t>
      binding_definition_by_command_;
  std::vector<std::size_t> visible_binding_definitions_;
  std::vector<std::pair<BindingChord, BindingCommand>>
      parsed_default_bindings_;
  std::unordered_map<BindingCommand, BindingScriptExecutor>
      binding_script_executors_;

 public:
  struct ModifiedClickBindingDefinition {
    explicit ModifiedClickBindingDefinition(ModifiedClickAction action_name)
        : action(std::move(action_name)) {}

    ModifiedClickAction action;
    std::array<ModifiedClickBindingState, 4> states{};
  };

 private:
  std::vector<ModifiedClickBindingDefinition> modified_click_bindings_;
  std::unordered_map<ModifiedClickAction, std::size_t>
      modified_click_index_by_action_;
  int next_visible_binding_index_{0};
  int next_hidden_binding_index_{0};

  void RebuildLookup();
  void RebuildActiveLookup();
  void RemoveBindingsForMode(BindingProfileScope mode);
  void RemoveBindingsForModeAndSlot(BindingProfileScope mode, std::uint8_t binding_slot);
  void RemoveOverrideOwner(OverrideKeyEntry& entry, const OverrideOwner& owner);
  void InitializeRecognizedBindingCommands();
  [[nodiscard]] int CountBindingsForCommandInModeAndSlot(
      BindingProfileScope mode,
      std::uint8_t binding_slot,
      const std::string& command) const;
  void DecrementBindingIndicesForCommand(
      BindingProfileScope mode,
      std::uint8_t binding_slot,
      const std::string& command,
      int removed_index);
  [[nodiscard]] std::vector<std::string> CollectKeysForCommandInModeAndSlot(
      const std::string& command,
      BindingProfileScope mode,
      std::uint8_t binding_slot) const;
  [[nodiscard]] const BindingCommand* GetResolvedOverrideCommand(
      const BindingChord& chord) const;
  [[nodiscard]] std::string LookupBindingActionExact(
      const std::string& key,
      bool check_override,
      BindingProfileScope mode) const;
  [[nodiscard]] std::string LookupBindingActionExactForModeAndSlot(
      const std::string& key,
      bool check_override,
      BindingProfileScope mode,
      std::uint8_t binding_slot) const;
  [[nodiscard]] bool IsRecognizedBindingCommand(
      const std::string& command) const;
  [[nodiscard]] BindingDefinition* FindBindingDefinition(
      const std::string& command);
  [[nodiscard]] const BindingDefinition* FindBindingDefinition(
      const std::string& command) const;
  [[nodiscard]] bool RunBindingDefinition(const BindingDefinition& definition,
                                          bool key_down,
                                          float pressure,
                                          bool pressure_available,
                                          bool alternate_pressure_state,
                                          bool angle_state,
                                          float angle,
                                          float precision,
                                          std::optional<std::uint16_t> modifier_state_override = std::nullopt);
  [[nodiscard]] ModifiedClickBindingDefinition* FindModifiedClickDefinition(
      const ModifiedClickAction& action);
  [[nodiscard]] const ModifiedClickBindingDefinition* FindModifiedClickDefinition(
      const ModifiedClickAction& action) const;
  void AdvanceAccountDataLoadGeneration();

  bool initialized_{false};
  BindingProfileScope current_binding_set_{BindingProfileScope::kAccount};
  std::uint8_t pending_account_data_mask_{0};

  std::uint16_t account_data_load_generation_{0};

  std::uint8_t copied_binding_set_mask_{0};
  std::uint8_t binding_state_bits_{0};
  std::uint8_t binding_state_reference_bits_{0};
  const openwow::vfs::VirtualFileSystem* vfs_{nullptr};
  JoystickNameProvider joystick_name_provider_;
};

}
