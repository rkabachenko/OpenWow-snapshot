#pragma once

#include "openwow/data/formats/dbc/dbc_entries_world.h"
#include "openwow/data/formats/dbc/dbc_store.h"
#include "openwow/data/formats/dbc/dbc_structures.h"

#include <array>
#include <bitset>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace openwow::audio { class SoundRuntime; }

namespace openwow::game {

using openwow::data::dbc::DbcStore;
using openwow::data::dbc::ObjectEffectEntry;
using openwow::data::dbc::ObjectEffectGroupEntry;
using openwow::data::dbc::ObjectEffectModifierEntry;
using openwow::data::dbc::ObjectEffectPackageElemEntry;
using openwow::data::dbc::ObjectEffectPackageEntry;
using openwow::data::dbc::SpellVisualEffectNameEntry;

class CObjectEffect;

inline constexpr std::uint32_t kEventSoundMapSentinel = 506;

inline constexpr std::uint32_t kEventSoundMapSize = 506;

inline constexpr std::uint32_t kObjectEffectEventCount = 82;

inline constexpr std::uint32_t kObjectEffectModifierInputTypeConstant = 0;
inline constexpr std::uint32_t kObjectEffectModifierInputTypeAnimTime = 1;

struct ObjectEffectEventSoundEntry {
  std::uint32_t anim_index{kEventSoundMapSentinel};
  std::uint32_t effect_state{0};
  std::uint32_t enchant_state{0};
};

struct ObjectEffectModifierData {
  std::uint32_t id{0};
  std::uint32_t input_type{0};
  std::uint32_t map_type{0};
  std::uint32_t output_type{0};
  std::uint32_t param_count{0};
  std::array<float, 4> map_params{};
};

struct ObjectEffectGroupRuntime {
  std::uint32_t id{0};
  std::string name;
  std::vector<const ObjectEffectEntry *> create_on_apply;
  std::vector<const ObjectEffectEntry *> attach_on_apply;
  std::vector<const ObjectEffectEntry *> create_on_clear;
  std::vector<const ObjectEffectEntry *> trigger_type4;
};

struct ObjectEffectPackageRuntime {
  std::uint32_t id{0};
  std::string name;

  std::unordered_map<std::uint32_t, std::vector<const ObjectEffectGroupRuntime *>> states;

  void Clear();
};

struct ObjectEffectInstance {
  ~ObjectEffectInstance();

  std::uint32_t sound_handle_id{0};
  const ObjectEffectEntry *effect_def{nullptr};
  CObjectEffect *owner{nullptr};
  openwow::audio::SoundRuntime* sound_runtime{nullptr};
  std::uint32_t play_count{0};
  std::uint32_t ref_count{0};
  float relative_volume_scale{1.0f};
  std::function<void()> destroy_callback;

  [[nodiscard]] bool UsesSoundKit() const;

  void DestroyResources();
};

struct ObjectEffectRef {
  ObjectEffectInstance *instance{nullptr};
};

struct ObjectEffectDef {
  std::uint32_t effect_id{0};

  std::unordered_map<std::uint32_t, ObjectEffectInstance *> attached_instances;
};

class CObjectEffect {
public:
  using InstancePositionResolver =
      std::function<std::optional<std::array<float, 3>>(const ObjectEffectInstance &)>;

  explicit CObjectEffect(openwow::audio::SoundRuntime& sound_runtime);

  ~CObjectEffect();

  CObjectEffect(const CObjectEffect &) = delete;
  CObjectEffect &operator=(const CObjectEffect &) = delete;
  CObjectEffect(CObjectEffect &&) noexcept = default;
  CObjectEffect &operator=(CObjectEffect &&) = delete;

  void AttachEffect(const ObjectEffectEntry *effect_def, std::uint32_t effect_group_id);

  [[nodiscard]] bool BindPackageAndApplyDefaultStates(std::uint32_t package_id);

  [[nodiscard]] bool BindPackage(std::uint32_t package_id);

  [[nodiscard]] bool ApplyState(std::uint32_t state_id, bool create_instances);

  [[nodiscard]] bool ClearState(std::uint32_t state_id, bool create_instances);

  void UpdateEffects();

  void DestroyAll();

  void CreateInstance(const ObjectEffectEntry *effect_def);

  void RefreshModifierInputType(std::uint32_t input_type);

  [[nodiscard]] std::size_t GetActiveCount() const {
    return active_instances_.size();
  }

  [[nodiscard]] std::size_t GetPendingCount() const {
    return pending_instances_.size();
  }

  [[nodiscard]] bool IsStateAttached(const std::uint32_t state_id) const;
  [[nodiscard]] std::uint32_t GetBoundPackageId() const noexcept {
    return bound_package_ != nullptr ? package_id_ : 0u;
  }
  void SetModifierInputResolver(std::function<float()> resolver);
  void SetInstancePositionResolver(InstancePositionResolver resolver);

private:
  friend struct CObjectEffectTestAccess;

  [[nodiscard]] std::unique_ptr<ObjectEffectInstance>
  BuildInstance(const ObjectEffectEntry *effect_def);
  [[nodiscard]] const ObjectEffectModifierData *
  FindModifierData(const ObjectEffectInstance &instance) const;
  [[nodiscard]] std::optional<float> ResolveModifierInputValue(std::uint32_t input_type) const;
  void ApplyModifierValue(ObjectEffectInstance &instance,
                          const ObjectEffectModifierData &modifier_data, float input_value) const;
  void RemoveInstanceRefs(const ObjectEffectInstance *instance);
  void RetireInstance(ObjectEffectInstance *instance);
  void ClearAttachedEventStates();
  void ClearEffectDefs();
  void ClearModifierRefs();
  void ApplyResolvedModifier(ObjectEffectInstance &instance);
  void StartPendingSound(ObjectEffectInstance &instance, const std::array<float, 3> &position);
  void StartActiveSoundIfNeeded(ObjectEffectInstance &instance,
                                const std::array<float, 3> &position);
  void UpdateActiveInstance(ObjectEffectInstance &instance);
  void RefreshModifierInputTypeRefs(std::uint32_t input_type, float input_value) const;
  [[nodiscard]] std::array<float, 3>
  ResolveInstancePosition(const ObjectEffectInstance &instance) const;
  [[nodiscard]] static std::uint32_t EffectInstanceKey(const ObjectEffectEntry *effect_def);
  [[nodiscard]] static std::uint32_t ModifierInputTypeFor(const ObjectEffectEntry *effect_def);

  std::bitset<kObjectEffectEventCount> attached_events_{};

  std::unordered_map<std::uint32_t, ObjectEffectDef> effect_defs_;

  std::unordered_map<std::uint32_t, std::vector<ObjectEffectRef>> modifier_refs_;

  std::list<std::unique_ptr<ObjectEffectInstance>> active_instances_;

  std::list<std::unique_ptr<ObjectEffectInstance>> pending_instances_;

  std::uint32_t package_id_{0};
  const ObjectEffectPackageRuntime *bound_package_{nullptr};
  std::function<float()> modifier_input_resolver_{};
  InstancePositionResolver instance_position_resolver_{};
  openwow::audio::SoundRuntime& sound_runtime_;
  std::optional<float> modifier_input_type1_cache_{};
};

class ObjectEffectDataStore {
public:
  static ObjectEffectDataStore &Instance();

  void LoadEffectData(const DbcStore<SpellVisualEffectNameEntry> &sven_store,
                      const DbcStore<ObjectEffectEntry> &effect_store,
                      const DbcStore<ObjectEffectGroupEntry> &group_store,
                      const DbcStore<ObjectEffectModifierEntry> &modifier_store,
                      const DbcStore<ObjectEffectPackageEntry> &package_store,
                      const DbcStore<ObjectEffectPackageElemEntry> &package_elem_store);

  void InitEventSoundMap();

  void Shutdown();

  [[nodiscard]] const SpellVisualEffectNameEntry *FindSpellVisualEffectName(std::uint32_t id) const;

  [[nodiscard]] const ObjectEffectGroupEntry *FindEffectGroup(std::uint32_t id) const;

  [[nodiscard]] const ObjectEffectModifierEntry *FindModifier(std::uint32_t id) const;

  [[nodiscard]] const ObjectEffectModifierData *FindModifierData(std::uint32_t id) const;

  [[nodiscard]] const ObjectEffectPackageRuntime *FindPackageRuntime(std::uint32_t id) const;

  [[nodiscard]] const std::array<ObjectEffectEventSoundEntry, kEventSoundMapSize> &
  GetEventSoundMap() const {
    return event_sound_map_;
  }

  [[nodiscard]] std::uint32_t GetEventSoundState(std::uint32_t anim_index,
                                                  bool is_enchant) const;

  [[nodiscard]] bool IsLoaded() const {
    return loaded_;
  }

private:
  friend struct CObjectEffectTestAccess;

  ObjectEffectDataStore() = default;

  std::unordered_map<std::uint32_t, SpellVisualEffectNameEntry> sven_table_;
  std::unordered_map<std::uint32_t, std::string> sven_names_;
  std::unordered_map<std::uint32_t, std::string> sven_file_paths_;

  std::unordered_map<std::uint32_t, ObjectEffectGroupEntry> group_table_;
  std::unordered_map<std::uint32_t, std::string> group_names_;

  std::unordered_map<std::uint32_t, ObjectEffectModifierEntry> modifier_table_;

  std::unordered_map<std::uint32_t, std::vector<ObjectEffectEntry>> effects_by_group_;
  std::unordered_map<std::uint32_t, std::string> effect_names_;

  std::unordered_map<std::uint32_t, std::vector<ObjectEffectPackageElemEntry>> package_elements_;

  std::unordered_map<std::uint32_t, ObjectEffectPackageEntry> package_table_;
  std::unordered_map<std::uint32_t, std::string> package_names_;

  std::unordered_map<std::uint32_t, ObjectEffectGroupRuntime> group_runtime_;

  std::unordered_map<std::uint32_t, ObjectEffectPackageRuntime> package_runtime_;

  std::unordered_map<std::uint32_t, ObjectEffectModifierData> modifier_data_;

  std::array<ObjectEffectEventSoundEntry, kEventSoundMapSize> event_sound_map_{};

  bool loaded_{false};
};

inline constexpr std::uint32_t kHardcodedEffectIdCount = 12;

enum class HardcodedEffectId : std::uint32_t {
  kFootstepWaterRunSpray = 0,
  kFootstepWaterWalkSpray = 1,
  kBreathUnderwater = 2,
  kBreathCold = 3,
  kLootArt = 4,
  kUnitLevelUp = 5,
  kMountPoof = 6,
  kInebriatedBubbles = 7,
  kMeetingStoneJoin = 8,
  kReputation = 9,
  kResistSpell = 10,
  kAchievementBase = 11,
};

class HardcodedEffectIdTable {
public:

  static void Initialize(const DbcStore<SpellVisualEffectNameEntry> &sven_store);

  static void Build(const DbcStore<SpellVisualEffectNameEntry> &sven_store);

  static void Reset();

  [[nodiscard]] static std::uint32_t GetEffectId(HardcodedEffectId index);

  [[nodiscard]] static std::uint32_t GetEffectId(std::uint32_t index);

  [[nodiscard]] static std::optional<std::uint32_t>
  GetAttachmentPoint(HardcodedEffectId index);

private:

  static std::array<std::uint32_t, kHardcodedEffectIdCount> s_effect_ids;
};

struct GameObjectTypeHandlerInfo {
  std::uint32_t event_id{234};
  float interact_dist{5.0f};
  std::int32_t anim_index_0{-1};
  std::int32_t anim_index_1{-1};
  std::int32_t anim_index_2{-1};
};

[[nodiscard]] GameObjectTypeHandlerInfo GetTypeHandlerInfo(std::uint8_t go_type);

}
