
#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace openwow::ui::game {

enum class CVarSerializationScope : std::uint8_t {
  kConfigFile = 0x00,
  kAccountDataSlot0 = 0x10,
  kAccountDataSlot1 = 0x20,
};

enum class CVarFlags : std::uint32_t {
  None           = 0x000,
  Registered     = 0x001,
  Archive        = Registered,
  ReadOnly       = 0x002,
  Immutable      = 0x004,
  ServerSent     = 0x004,
  Protected      = 0x008,
  Account        = 0x010,
  Character      = 0x020,
  Hidden         = 0x040,
  NoSave         = 0x080,
  ConsoleReadOnly = 0x100,
};

inline CVarFlags operator|(CVarFlags a, CVarFlags b) {
  return static_cast<CVarFlags>(static_cast<std::uint32_t>(a) |
                                static_cast<std::uint32_t>(b));
}
inline CVarFlags operator&(CVarFlags a, CVarFlags b) {
  return static_cast<CVarFlags>(static_cast<std::uint32_t>(a) &
                                static_cast<std::uint32_t>(b));
}
inline bool HasFlag(CVarFlags set, CVarFlags flag) {
  return (static_cast<std::uint32_t>(set) & static_cast<std::uint32_t>(flag)) != 0;
}

using CVarCallback = std::function<void(const std::string&, const std::string&)>;

using CVarValidationCallback = std::function<bool(const std::string&, const std::string&, const std::string&)>;

class CVarSystem;

class CVarValidationCallbackRegistration {
 public:
  CVarValidationCallbackRegistration() = default;
  ~CVarValidationCallbackRegistration();
  CVarValidationCallbackRegistration(
      const CVarValidationCallbackRegistration&) = delete;
  CVarValidationCallbackRegistration& operator=(
      const CVarValidationCallbackRegistration&) = delete;
  CVarValidationCallbackRegistration(
      CVarValidationCallbackRegistration&& other) noexcept;
  CVarValidationCallbackRegistration& operator=(
      CVarValidationCallbackRegistration&& other) noexcept;

  void Reset();

 private:
  friend class CVarSystem;
  CVarValidationCallbackRegistration(CVarSystem& owner, std::string name,
                                     std::uint64_t handle);

  CVarSystem* owner_ = nullptr;
  std::string name_;
  std::uint64_t handle_ = 0;
};

class CVarSystem {
 public:
  friend class CVarValidationCallbackRegistration;

  static constexpr int kFallbackConsoleCategory = 4;

  struct CVarSnapshot {
    std::string registered_name;
    std::string value;
    float current_float_value = 0.0f;
    std::int32_t current_int_value = 0;
    std::string default_value;
    bool has_default_value = false;
    std::string startup_value;
    bool has_startup_value = false;
    std::string pending_value;
    bool has_pending_value = false;
    CVarFlags flags = CVarFlags::None;
    std::string description;
    float min_value = 0.0f;
    float max_value = 0.0f;
    bool has_limits = false;
    std::uint32_t change_counter = 0;
    std::uint8_t info_bits = 0;
    bool has_validation_callback = false;
    int console_category = kFallbackConsoleCategory;
  };

  static CVarSystem& Instance();

  void RegisterCVar(const std::string& name, const std::string& default_value,
                    CVarFlags flags = CVarFlags::None,
                    const std::string& description = "",
                    float min_value = 0.0f, float max_value = 0.0f,
                    int console_category = kFallbackConsoleCategory);

  void RegisterNativeCVar(const std::string& name,
                          const std::string& default_value,
                          CVarFlags flags,
                          const std::string& description,
                          CVarValidationCallback validation_callback = {},
                          float min_value = 0.0f,
                          float max_value = 0.0f,
                          int console_category = kFallbackConsoleCategory);

  void RegisterScriptCVar(const std::string& name, const std::string& initial_value);

  [[nodiscard]] std::string GetCVar(const std::string& name) const;
  [[nodiscard]] bool        GetCVarBool(const std::string& name) const;
  [[nodiscard]] float       GetCVarFloat(const std::string& name) const;
  [[nodiscard]] int         GetCVarInt(const std::string& name) const;

  bool SetCVar(const std::string& name, const std::string& value,
               bool force = false);

  struct SetRegisteredValueOptions {
    bool force;
    bool bypass_read_only;
    bool bypass_validation;
    bool validate_before_immutable;
    bool increment_change_counter;
    bool update_current_value;
    bool populate_startup_if_missing;
    bool populate_default_if_missing;
    bool mark_dirty;
    constexpr SetRegisteredValueOptions()
        : force(false), bypass_read_only(false), bypass_validation(false),
          validate_before_immutable(false),
          increment_change_counter(true), update_current_value(true),
          populate_startup_if_missing(false), populate_default_if_missing(false),
          mark_dirty(true) {}
  };

  bool SetRegisteredCVarValue(const std::string& name, const std::string& value,
                              SetRegisteredValueOptions options = {});
  bool SetRegisteredCVarIntValue(const std::string& name, int value,
                                 SetRegisteredValueOptions options = {});

  bool SetRegisteredCVarValueDirect(const std::string& name,
                                    const std::string& value);

  [[nodiscard]] std::string GetCVarDefault(const std::string& name) const;
  [[nodiscard]] bool        HasCVarDefault(const std::string& name) const;
  [[nodiscard]] CVarFlags   GetCVarFlags(const std::string& name) const;
  [[nodiscard]] std::string GetCVarDescription(const std::string& name) const;
  [[nodiscard]] std::uint8_t GetCVarInfoBits(const std::string& name) const;

  void ResetToStartup(const std::string& name);

  void ResetCVar(const std::string& name);

  bool ApplyPendingValue(const std::string& name);

  [[nodiscard]] bool Exists(const std::string& name) const;

  bool UnregisterCVar(const std::string& name);

  [[nodiscard]] bool IsHidden(const std::string& name) const;

  [[nodiscard]] std::string GetStartupValue(const std::string& name) const;
  [[nodiscard]] bool        HasStartupValue(const std::string& name) const;

  [[nodiscard]] std::string GetPendingValue(const std::string& name) const;

  [[nodiscard]] std::uint32_t GetChangeCounter(const std::string& name) const;

  [[nodiscard]] std::optional<CVarSnapshot> GetCVarSnapshot(
      std::string_view name) const;

  [[nodiscard]] std::optional<CVarSnapshot> LookupCVarByName(
      std::string_view name) const;

  void SetValidationCallback(const std::string& name, CVarValidationCallback cb);

  [[nodiscard]] CVarValidationCallbackRegistration RegisterValidationCallback(
      const std::string& name, CVarValidationCallback cb);

  void SetCVarInfoBits(const std::string& name, std::uint8_t bits);

  void AddFlags(const std::string& name, CVarFlags flags);

  bool ReconcileValueAgainstValidationCallback(const std::string& name);

  void ApplyClientRegisterCVarsValueFixups();

  std::uint32_t AddCallback(const std::string& name, CVarCallback cb);

  void RemoveCallback(const std::string& name, std::uint32_t handle);

  [[nodiscard]] std::vector<std::string> GetAllNames() const;

  [[nodiscard]] std::size_t Count() const;

  void Clear();

  void LoadFromFile(const std::string& path);

  bool SaveToFile(const std::string& path, bool save_all = false) const;
  [[nodiscard]] std::string SerializeConfig(bool save_all = false) const;
  [[nodiscard]] std::string SerializeConfig(
      CVarSerializationScope scope, bool save_all = false) const;
  [[nodiscard]] std::string SerializeConfig(
      CVarSerializationScope scope, CVarFlags excluded_flags,
      bool save_all = false) const;

  void RegisterDefaults();

 private:
  CVarSystem() = default;

  struct CVarEntry {

    std::string registered_name;
    std::string value;

    std::string default_value;

    bool has_default_value = false;
    std::string startup_value;

    bool has_startup_value = false;
    std::string pending_value;

    bool has_pending_value = false;
    CVarFlags flags = CVarFlags::None;

    std::string description;

    float min_value = 0.0f;
    float max_value = 0.0f;
    bool has_limits = false;
    std::uint32_t change_counter = 0;

    CVarValidationCallback validation_callback;
    std::uint64_t validation_callback_handle = 0;
    bool validation_reconciled = false;

    struct CallbackEntry {
      std::uint32_t handle;
      CVarCallback fn;
    };
    std::vector<CallbackEntry> callbacks;
    bool is_native_registered = false;
    int console_category = kFallbackConsoleCategory;
    std::uint64_t console_command_registration_id = 0;

  };

  struct RegisteredSetResult {
    bool accepted = false;
    bool current_value_changed = false;
    bool metadata_changed = false;
    bool should_mark_dirty = false;
    std::string old_value;
    std::string new_value;
    std::vector<CVarCallback> callbacks_to_fire;
  };

  mutable std::recursive_mutex mutex_;
  std::unordered_map<std::string, CVarEntry> cvars_;

  std::vector<std::string> stable_keys_;
  std::uint32_t next_callback_handle_ = 1;
  std::uint64_t next_validation_callback_handle_ = 1;

  using EntryMap = std::unordered_map<std::string, CVarEntry>;
  using EntryIterator = EntryMap::iterator;
  using ConstEntryIterator = EntryMap::const_iterator;

  [[nodiscard]] EntryIterator FindEntryLocked(std::string_view name);
  [[nodiscard]] ConstEntryIterator FindEntryLocked(std::string_view name) const;
  [[nodiscard]] static std::string FoldLookupKey(std::string_view name);
  [[nodiscard]] RegisteredSetResult ApplyRegisteredSetValueLocked(
      const std::string& name, CVarEntry& entry, const std::string& value,
      const SetRegisteredValueOptions& options);
  void RegisterConsoleCommandForEntry(const std::string& name,
                                      const std::string& description,
                                      int console_category);
  void ResetCVarValue(const std::string& name, bool prefer_startup_value);
  void RemoveValidationCallback(const std::string& name, std::uint64_t handle);
  [[nodiscard]] static CVarSnapshot MakeSnapshot(std::string_view registered_name,
                                                 const CVarEntry& entry);
  [[nodiscard]] std::string SerializeConfigLocked(CVarSerializationScope scope,
                                                  CVarFlags excluded_flags,
                                                  bool save_all,
                                                  int* count) const;

};

}
