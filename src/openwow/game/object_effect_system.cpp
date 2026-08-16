
#include "openwow/game/object_effect_system.h"

#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/foundation/text/ascii.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <string>
#include <utility>

namespace openwow::game {

namespace {

constexpr std::uint32_t kDefaultPackageStateReady = 1;
constexpr std::uint32_t kDefaultPackageStateSpecialReady = 0x26;
constexpr std::uint32_t kPiecewiseLinearMapParamCount = 4;

float EvaluatePiecewiseLinearModifier(const ObjectEffectModifierData &modifier_data,
                                      const float input_value) {
  const float lower_input = modifier_data.map_params[0];
  const float upper_input = modifier_data.map_params[1];
  const float lower_output = modifier_data.map_params[2];
  const float upper_output = modifier_data.map_params[3];

  if (input_value <= lower_input) {
    return lower_output;
  }

  if (input_value >= upper_input) {
    return upper_output;
  }

  return ((input_value - lower_input) / (upper_input - lower_input)) *
             (upper_output - lower_output) +
         lower_output;
}

}

ObjectEffectInstance::~ObjectEffectInstance() {
  DestroyResources();
}

bool ObjectEffectInstance::UsesSoundKit() const {
  return effect_def != nullptr && effect_def->effect_rec_type == 1;
}

void ObjectEffectInstance::DestroyResources() {
  if (sound_handle_id != 0u) {
    if (sound_runtime != nullptr) {
      (void)sound_runtime->StopActiveSoundHandle(sound_handle_id, true, -1.0f, true);
    }
  }

  if (destroy_callback) {
    auto callback = std::move(destroy_callback);
    callback();
  }

  sound_handle_id = 0;
  effect_def = nullptr;
  owner = nullptr;
  play_count = 0;
  ref_count = 0;
  relative_volume_scale = 1.0f;
}

void ObjectEffectPackageRuntime::Clear() {
  states.clear();
  id = 0;
  name = {};
}

CObjectEffect::CObjectEffect(openwow::audio::SoundRuntime& sound_runtime)
    : sound_runtime_(sound_runtime) {}

CObjectEffect::~CObjectEffect() {
  DestroyAll();
}

std::unique_ptr<ObjectEffectInstance>
CObjectEffect::BuildInstance(const ObjectEffectEntry *effect_def) {
  auto instance = std::make_unique<ObjectEffectInstance>();
  instance->effect_def = effect_def;
  instance->owner = this;
  instance->sound_runtime = &sound_runtime_;
  instance->play_count = 0;
  instance->ref_count = 0;
  instance->relative_volume_scale = 1.0f;
  return instance;
}

void CObjectEffect::RemoveInstanceRefs(const ObjectEffectInstance *instance) {
  const auto prune_refs = [instance](auto &refs) {
    refs.erase(
        std::remove_if(refs.begin(), refs.end(),
                       [instance](const ObjectEffectRef &ref) { return ref.instance == instance; }),
        refs.end());
  };

  for (auto it = modifier_refs_.begin(); it != modifier_refs_.end();) {
    prune_refs(it->second);
    if (it->second.empty()) {
      it = modifier_refs_.erase(it);
      continue;
    }
    ++it;
  }

  for (auto it = effect_defs_.begin(); it != effect_defs_.end();) {
    auto &attached_instances = it->second.attached_instances;
    for (auto instance_it = attached_instances.begin(); instance_it != attached_instances.end();) {
      if (instance_it->second == instance) {
        instance_it = attached_instances.erase(instance_it);
        continue;
      }
      ++instance_it;
    }

    if (attached_instances.empty()) {
      it = effect_defs_.erase(it);
      continue;
    }
    ++it;
  }
}

void CObjectEffect::RetireInstance(ObjectEffectInstance *instance) {
  if (instance == nullptr || instance->ref_count != 0u) {
    return;
  }

  const auto active_it = std::find_if(
      active_instances_.begin(), active_instances_.end(),
      [instance](const auto &candidate) { return candidate.get() == instance; });
  if (active_it == active_instances_.end()) {
    return;
  }

  pending_instances_.splice(pending_instances_.end(), active_instances_, active_it);
  if (instance->UsesSoundKit() && instance->sound_handle_id != 0u) {
    (void)sound_runtime_.RequestStopSoundHandle(
        instance->sound_handle_id, 0.7f);
  }
}

void CObjectEffect::ClearAttachedEventStates() {
  attached_events_.reset();
}

void CObjectEffect::ClearEffectDefs() {
  effect_defs_.clear();
}

void CObjectEffect::ClearModifierRefs() {
  modifier_refs_.clear();
}

std::uint32_t CObjectEffect::EffectInstanceKey(const ObjectEffectEntry *effect_def) {
  return effect_def ? effect_def->id : 0;
}

std::uint32_t CObjectEffect::ModifierInputTypeFor(const ObjectEffectEntry *effect_def) {
  if (effect_def == nullptr || effect_def->object_effect_modifier_id == 0u) {
    return 0u;
  }

  const auto *modifier_data =
      ObjectEffectDataStore::Instance().FindModifierData(effect_def->object_effect_modifier_id);
  return modifier_data != nullptr ? modifier_data->input_type : 0u;
}

bool CObjectEffect::BindPackage(const std::uint32_t package_id) {
  package_id_ = package_id;
  bound_package_ = ObjectEffectDataStore::Instance().FindPackageRuntime(package_id);
  return bound_package_ != nullptr;
}

bool CObjectEffect::BindPackageAndApplyDefaultStates(const std::uint32_t package_id) {
  if (!BindPackage(package_id)) {
    return false;
  }

  (void)ApplyState(kDefaultPackageStateReady, true);
  (void)ApplyState(kDefaultPackageStateSpecialReady, true);
  return true;
}

bool CObjectEffect::ApplyState(const std::uint32_t state_id, const bool create_instances) {
  if (!bound_package_ || state_id >= kObjectEffectEventCount) {
    return false;
  }

  if (attached_events_.test(state_id)) {
    return true;
  }

  attached_events_.set(state_id);

  const auto state_it = bound_package_->states.find(state_id);
  if (state_it == bound_package_->states.end()) {
    return false;
  }

  for (const auto *group_runtime : state_it->second) {
    if (!group_runtime) {
      continue;
    }

    if (create_instances) {
      for (const auto *effect_def : group_runtime->create_on_apply) {
        CreateInstance(effect_def);
      }
    }

    for (const auto *effect_def : group_runtime->attach_on_apply) {
      AttachEffect(effect_def, group_runtime->id);
    }
  }

  return true;
}

bool CObjectEffect::ClearState(const std::uint32_t state_id, const bool create_instances) {
  if (!bound_package_ || state_id >= kObjectEffectEventCount || !attached_events_.test(state_id)) {
    return false;
  }

  attached_events_.reset(state_id);

  const auto state_it = bound_package_->states.find(state_id);
  if (state_it == bound_package_->states.end()) {
    return false;
  }

  for (const auto *group_runtime : state_it->second) {
    if (!group_runtime) {
      continue;
    }

    if (create_instances) {
      for (const auto *effect_def : group_runtime->create_on_clear) {
        CreateInstance(effect_def);
      }
    }

    const auto active_group_it = effect_defs_.find(group_runtime->id);
    if (active_group_it == effect_defs_.end()) {
      continue;
    }

    bool all_instances_released = true;
    for (const auto &[effect_id, instance] : active_group_it->second.attached_instances) {
      (void)effect_id;
      if (instance == nullptr) {
        continue;
      }

      if (instance->ref_count != 0u) {
        --instance->ref_count;
      }
      if (instance->ref_count == 0u) {
        RetireInstance(instance);
      } else {
        all_instances_released = false;
      }
    }

    if (all_instances_released) {
      effect_defs_.erase(active_group_it);
    }
  }

  return true;
}

bool CObjectEffect::IsStateAttached(const std::uint32_t state_id) const {
  return state_id < kObjectEffectEventCount && attached_events_.test(state_id);
}

void CObjectEffect::SetModifierInputResolver(std::function<float()> resolver) {
  modifier_input_resolver_ = std::move(resolver);
  modifier_input_type1_cache_.reset();
}

void CObjectEffect::SetInstancePositionResolver(InstancePositionResolver resolver) {
  instance_position_resolver_ = std::move(resolver);
}

const ObjectEffectModifierData *
CObjectEffect::FindModifierData(const ObjectEffectInstance &instance) const {
  if (instance.effect_def == nullptr || instance.effect_def->object_effect_modifier_id == 0u) {
    return nullptr;
  }

  return ObjectEffectDataStore::Instance().FindModifierData(
      instance.effect_def->object_effect_modifier_id);
}

std::optional<float>
CObjectEffect::ResolveModifierInputValue(const std::uint32_t input_type) const {
  switch (input_type) {
  case 0:
    return -1.0f;
  case 1:
    if (!modifier_input_resolver_) {
      return std::nullopt;
    }
    return modifier_input_resolver_();
  default:
    return std::nullopt;
  }
}

void CObjectEffect::ApplyModifierValue(ObjectEffectInstance &instance,
                                       const ObjectEffectModifierData &modifier_data,
                                       const float input_value) const {
  if (modifier_data.map_type == 0u) {
    return;
  }

  float mapped_value = input_value;
  switch (modifier_data.map_type) {
  case 1:
    if (modifier_data.param_count != kPiecewiseLinearMapParamCount) {
      return;
    }
    mapped_value = EvaluatePiecewiseLinearModifier(modifier_data, input_value);
    break;
  default:
    return;
  }

  switch (modifier_data.output_type) {
  case 0:
    return;
  case 1:
    instance.relative_volume_scale = mapped_value;
    if (instance.sound_handle_id != 0u) {
      (void)sound_runtime_.ApplySoundHandleRelativeVolumeScale(
          instance.sound_handle_id, mapped_value);
    }
    return;
  default:
    return;
  }
}

void CObjectEffect::RefreshModifierInputType(const std::uint32_t input_type) {
  const auto input_value = ResolveModifierInputValue(input_type);
  if (!input_value.has_value()) {
    return;
  }

  if (input_type == 1u) {
    modifier_input_type1_cache_ = *input_value;
  }

  RefreshModifierInputTypeRefs(input_type, *input_value);
}

void CObjectEffect::RefreshModifierInputTypeRefs(const std::uint32_t input_type,
                                                 const float input_value) const {
  const auto refs_it = modifier_refs_.find(input_type);
  if (refs_it == modifier_refs_.end()) {
    return;
  }

  for (const auto &ref : refs_it->second) {
    if (ref.instance == nullptr) {
      continue;
    }

    const auto *modifier_data = FindModifierData(*ref.instance);
    if (modifier_data == nullptr || modifier_data->input_type != input_type) {
      continue;
    }

    ApplyModifierValue(*ref.instance, *modifier_data, input_value);
  }
}

void CObjectEffect::ApplyResolvedModifier(ObjectEffectInstance &instance) {
  const auto *modifier_data = FindModifierData(instance);
  if (modifier_data == nullptr) {
    return;
  }

  const auto input_value = ResolveModifierInputValue(modifier_data->input_type);
  if (!input_value.has_value()) {
    return;
  }

  if (modifier_data->input_type == 1u) {
    modifier_input_type1_cache_ = *input_value;
  }

  ApplyModifierValue(instance, *modifier_data, *input_value);
}

std::array<float, 3>
CObjectEffect::ResolveInstancePosition(const ObjectEffectInstance &instance) const {
  if (instance_position_resolver_) {
    if (const auto resolved_position = instance_position_resolver_(instance);
        resolved_position.has_value()) {
      return *resolved_position;
    }
  }

  if (instance.effect_def != nullptr) {
    return {instance.effect_def->offset_x, instance.effect_def->offset_y,
            instance.effect_def->offset_z};
  }

  return {};
}

void CObjectEffect::StartPendingSound(ObjectEffectInstance &instance,
                                      const std::array<float, 3> &position) {
  if (!instance.UsesSoundKit() || instance.effect_def == nullptr) {
    return;
  }

  auto &sound = sound_runtime_;
  if (!sound.IsSoundHandlePlaying(instance.sound_handle_id)) {

    openwow::audio::SoundKitPlaybackOptions options;
    options.loop_mode = openwow::audio::SoundLoopMode::kForceOneShot;

    std::uint32_t handle_id = instance.sound_handle_id;
    if (sound.PlaySoundKit(instance.effect_def->effect_rec_id, position.data(), &handle_id,
                           options) == 0) {
      instance.sound_handle_id = handle_id;
      if (instance.relative_volume_scale != 1.0f) {
        (void)sound.ApplySoundHandleRelativeVolumeScale(handle_id, instance.relative_volume_scale);
      }
    }
  }

  ++instance.play_count;
}

void CObjectEffect::StartActiveSoundIfNeeded(ObjectEffectInstance &instance,
                                             const std::array<float, 3> &position) {
  if (!instance.UsesSoundKit() || instance.effect_def == nullptr || instance.play_count != 0u) {
    return;
  }

  auto &sound = sound_runtime_;
  if (!sound.IsSoundHandlePlaying(instance.sound_handle_id)) {
    openwow::audio::SoundKitPlaybackOptions options;
    options.volume_scale = 0.7f;

    const std::uint32_t usage =
        sound.GetSoundKitAdvancedUsage(instance.effect_def->effect_rec_id);
    options.loop_mode = (usage != 1) ? openwow::audio::SoundLoopMode::kForceLoop
                                     : openwow::audio::SoundLoopMode::kUseSoundKit;

    std::uint32_t handle_id = instance.sound_handle_id;
    if (sound.PlaySoundKit(instance.effect_def->effect_rec_id, position.data(), &handle_id,
                           options) == 0) {
      instance.sound_handle_id = handle_id;
      if (instance.relative_volume_scale != 1.0f) {
        (void)sound.ApplySoundHandleRelativeVolumeScale(handle_id, instance.relative_volume_scale);
      }
    }
  }

  ++instance.play_count;
}

void CObjectEffect::UpdateActiveInstance(ObjectEffectInstance &instance) {
  if (!instance.UsesSoundKit()) {
    return;
  }

  const auto position = ResolveInstancePosition(instance);
  auto &sound = sound_runtime_;
  if (sound.IsSoundHandlePlaying(instance.sound_handle_id)) {

    sound.StartSoundHandleFadeIn(instance.sound_handle_id);
  } else {
    StartActiveSoundIfNeeded(instance, position);
  }

  ApplyResolvedModifier(instance);
  (void)sound.SetSoundHandlePosition(instance.sound_handle_id, position.data());
}

void CObjectEffect::AttachEffect(const ObjectEffectEntry *effect_def,
                                 std::uint32_t effect_group_id) {
  if (!effect_def)
    return;

  auto &def = effect_defs_[effect_group_id];
  def.effect_id = effect_group_id;
  const auto effect_key = EffectInstanceKey(effect_def);

  for (auto it = pending_instances_.begin(); it != pending_instances_.end(); ++it) {
    if ((*it)->effect_def != effect_def) {
      continue;
    }

    auto *instance = it->get();
    def.attached_instances[effect_key] = instance;
    active_instances_.splice(active_instances_.end(), pending_instances_, it);
    ++instance->ref_count;
    if (instance->UsesSoundKit() && instance->sound_handle_id != 0u) {
      sound_runtime_.StartSoundHandleFadeIn(
          instance->sound_handle_id);
    }
    return;
  }

  if (const auto existing = def.attached_instances.find(effect_key);
      existing != def.attached_instances.end() && existing->second != nullptr) {
    ++existing->second->ref_count;
    return;
  }

  auto instance = BuildInstance(effect_def);
  instance->ref_count = 1;
  auto *raw_instance = instance.get();

  def.attached_instances[effect_key] = raw_instance;
  modifier_refs_[ModifierInputTypeFor(effect_def)].push_back({raw_instance});

  active_instances_.push_back(std::move(instance));
  StartActiveSoundIfNeeded(*raw_instance, ResolveInstancePosition(*raw_instance));
  ApplyResolvedModifier(*raw_instance);
}

void CObjectEffect::CreateInstance(const ObjectEffectEntry *effect_def) {
  if (!effect_def)
    return;

  auto instance = BuildInstance(effect_def);
  auto *raw_instance = instance.get();

  modifier_refs_[ModifierInputTypeFor(effect_def)].push_back({raw_instance});
  pending_instances_.push_back(std::move(instance));
  StartPendingSound(*raw_instance, ResolveInstancePosition(*raw_instance));
  ApplyResolvedModifier(*raw_instance);
}

void CObjectEffect::UpdateEffects() {
  for (auto &instance : active_instances_) {
    UpdateActiveInstance(*instance);
  }

  auto &sound = sound_runtime_;
  for (auto it = pending_instances_.begin(); it != pending_instances_.end();) {
    auto &instance = **it;
    const auto effect_type =
        instance.effect_def != nullptr ? instance.effect_def->effect_rec_type : 0u;

    if (effect_type == 1u) {
      const auto position = ResolveInstancePosition(instance);
      if (sound.IsSoundHandlePlaying(instance.sound_handle_id)) {
        (void)sound.SetSoundHandlePosition(instance.sound_handle_id, position.data());
        ++it;
        continue;
      }
    } else if (effect_type == 2u) {
      ++it;
      continue;
    }

    if (instance.play_count != 0u) {
      --instance.play_count;
    }

    RemoveInstanceRefs(it->get());
    it = pending_instances_.erase(it);
  }
}

void CObjectEffect::DestroyAll() {

  ClearAttachedEventStates();
  ClearModifierRefs();
  ClearEffectDefs();
  active_instances_.clear();
  pending_instances_.clear();
}

ObjectEffectDataStore &ObjectEffectDataStore::Instance() {
  static ObjectEffectDataStore instance;
  return instance;
}

void ObjectEffectDataStore::InitEventSoundMap() {

  for (auto &entry : event_sound_map_) {
    entry = {kEventSoundMapSentinel, 0, 0};
  }

  event_sound_map_[  0] = {  0, 19, 41};
  event_sound_map_[  1] = {  1, 15,  0};
  event_sound_map_[  4] = {  4, 13,  0};
  event_sound_map_[  5] = {  5, 12,  0};
  event_sound_map_[  6] = {  6, 81,  0};
  event_sound_map_[  8] = {  8, 17,  0};
  event_sound_map_[  9] = {  9, 14,  0};
  event_sound_map_[ 10] = { 10, 39,  0};
  event_sound_map_[ 11] = { 11, 20,  0};
  event_sound_map_[ 12] = { 12, 21,  0};
  event_sound_map_[ 13] = { 13, 22,  0};
  event_sound_map_[ 16] = { 16, 40,  0};
  event_sound_map_[ 25] = { 25, 18,  0};
  event_sound_map_[ 37] = { 37, 23,  0};
  event_sound_map_[ 38] = { 38, 36,  0};
  event_sound_map_[ 39] = { 39, 24,  0};
  event_sound_map_[ 40] = { 40, 25,  0};
  event_sound_map_[ 41] = { 41, 26, 42};
  event_sound_map_[ 42] = { 42, 27,  0};
  event_sound_map_[ 43] = { 43, 28,  0};
  event_sound_map_[ 44] = { 44, 29,  0};
  event_sound_map_[ 45] = { 45, 30,  0};
  event_sound_map_[ 53] = { 53, 16,  0};
  event_sound_map_[ 92] = { 92, 32,  0};
  event_sound_map_[ 93] = { 93, 31,  0};
  event_sound_map_[ 96] = { 96, 45,  0};
  event_sound_map_[ 97] = { 97, 43,  0};
  event_sound_map_[ 98] = { 98, 44,  0};
  event_sound_map_[107] = {107,  8,  0};
  event_sound_map_[108] = {108, 11,  0};
  event_sound_map_[111] = {111,  9,  0};
  event_sound_map_[112] = {112, 10,  0};
  event_sound_map_[135] = {135, 33,  0};
  event_sound_map_[143] = {143, 34,  0};
  event_sound_map_[146] = {146, 73,  0};
  event_sound_map_[147] = {147, 74,  0};
  event_sound_map_[148] = {148, 71,  0};
  event_sound_map_[149] = {149, 72,  0};
  event_sound_map_[150] = {150, 75,  0};
  event_sound_map_[151] = {151, 76,  0};
  event_sound_map_[153] = {153, 77,  0};
  event_sound_map_[154] = {154, 78,  0};
  event_sound_map_[155] = {155, 79,  0};
  event_sound_map_[156] = {156, 80,  0};
  event_sound_map_[162] = {162, 68,  0};
  event_sound_map_[163] = {163, 69,  0};
  event_sound_map_[164] = {164, 70,  0};
  event_sound_map_[187] = {187, 35,  0};
  event_sound_map_[193] = {193, 46,  0};
  event_sound_map_[226] = {226, 67,  0};

}

std::uint32_t ObjectEffectDataStore::GetEventSoundState(
    std::uint32_t anim_index, bool is_enchant) const {

  if (anim_index >= kEventSoundMapSize) {
    return 0;
  }
  const auto &entry = event_sound_map_[anim_index];
  return is_enchant ? entry.enchant_state : entry.effect_state;
}

void ObjectEffectDataStore::Shutdown() {
  group_runtime_.clear();

  for (auto &[id, runtime] : package_runtime_) {
    runtime.Clear();
  }
  package_runtime_.clear();
  modifier_data_.clear();
  effects_by_group_.clear();
  package_elements_.clear();
  package_table_.clear();
  modifier_table_.clear();
  group_table_.clear();
  sven_table_.clear();
  package_names_.clear();
  effect_names_.clear();
  group_names_.clear();
  sven_file_paths_.clear();
  sven_names_.clear();
  event_sound_map_.fill({});
  HardcodedEffectIdTable::Reset();
  loaded_ = false;
}

void ObjectEffectDataStore::LoadEffectData(
    const DbcStore<SpellVisualEffectNameEntry> &sven_store,
    const DbcStore<ObjectEffectEntry> &effect_store,
    const DbcStore<ObjectEffectGroupEntry> &group_store,
    const DbcStore<ObjectEffectModifierEntry> &modifier_store,
    const DbcStore<ObjectEffectPackageEntry> &package_store,
    const DbcStore<ObjectEffectPackageElemEntry> &package_elem_store) {

  if (loaded_)
    return;

  InitEventSoundMap();
  HardcodedEffectIdTable::Build(sven_store);

  for (const auto &entry : sven_store.entries()) {
    auto &owned_name = sven_names_[entry.id];
    auto &owned_file_path = sven_file_paths_[entry.id];
    owned_name.assign(entry.name);
    owned_file_path.assign(entry.file_path);

    auto owned_entry = entry;
    owned_entry.name = owned_name;
    owned_entry.file_path = owned_file_path;
    sven_table_[entry.id] = owned_entry;
  }
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo, "ObjectEffect: Loaded " +
                                                         std::to_string(sven_table_.size()) +
                                                         " SpellVisualEffectName entries");

  for (const auto &entry : group_store.entries()) {
    auto &owned_name = group_names_[entry.id];
    owned_name.assign(entry.name);
    auto owned_entry = entry;
    owned_entry.name = owned_name;
    group_table_[entry.id] = owned_entry;
    group_runtime_[entry.id] = ObjectEffectGroupRuntime{.id = entry.id, .name = owned_name};
  }
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo, "ObjectEffect: Loaded " +
                                                         std::to_string(group_table_.size()) +
                                                         " ObjectEffectGroup entries");

  for (const auto &entry : modifier_store.entries()) {
    modifier_table_[entry.id] = entry;

    ObjectEffectModifierData data;
    data.id = entry.id;
    data.input_type = entry.input_type;
    data.map_type = entry.map_type;
    data.output_type = entry.output_type;
    if (entry.map_type == 1u) {
      data.param_count = kPiecewiseLinearMapParamCount;
      data.map_params = entry.map_params;
    }
    modifier_data_[entry.id] = data;
  }
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo, "ObjectEffect: Loaded " +
                                                         std::to_string(modifier_table_.size()) +
                                                         " ObjectEffectModifier entries");

  for (const auto &entry : effect_store.entries()) {
    auto &owned_name = effect_names_[entry.id];
    owned_name.assign(entry.name);
    auto owned_entry = entry;
    owned_entry.name = owned_name;
    effects_by_group_[entry.object_effect_group_id].push_back(owned_entry);
  }
  for (auto &[group_id, effects] : effects_by_group_) {
    auto &runtime = group_runtime_[group_id];
    runtime.id = group_id;
    if (const auto group_it = group_table_.find(group_id); group_it != group_table_.end()) {
      runtime.name = group_it->second.name;
    }

    for (const auto &effect : effects) {
      switch (effect.trigger_type) {
      case 1:
        runtime.create_on_apply.push_back(&effect);
        break;
      case 2:
        runtime.attach_on_apply.push_back(&effect);
        break;
      case 3:
        runtime.create_on_clear.push_back(&effect);
        break;
      case 4:
        runtime.trigger_type4.push_back(&effect);
        break;
      default:
        break;
      }
    }
  }
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "ObjectEffect: Loaded " + std::to_string(effect_store.size()) +
                         " ObjectEffect entries into " + std::to_string(effects_by_group_.size()) +
                         " groups");

  for (const auto &entry : package_store.entries()) {
    auto &owned_name = package_names_[entry.id];
    owned_name.assign(entry.name);
    auto owned_entry = entry;
    owned_entry.name = owned_name;
    package_table_[entry.id] = owned_entry;
    package_runtime_[entry.id] = ObjectEffectPackageRuntime{.id = entry.id, .name = owned_name};
  }

  for (const auto &entry : package_elem_store.entries()) {
    package_elements_[entry.object_effect_package_id].push_back(entry);

    auto &runtime = package_runtime_[entry.object_effect_package_id];
    runtime.id = entry.object_effect_package_id;
    if (const auto package_it = package_table_.find(entry.object_effect_package_id);
        package_it != package_table_.end()) {
      runtime.name = package_it->second.name;
    }

    const auto group_it = group_runtime_.find(entry.object_effect_group_id);
    if (group_it == group_runtime_.end()) {
      continue;
    }

    runtime.states[entry.state_type].push_back(&group_it->second);
  }
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "ObjectEffect: Loaded " + std::to_string(package_elem_store.size()) +
                         " ObjectEffectPackageElem entries into " +
                         std::to_string(package_elements_.size()) + " packages");

  loaded_ = true;
}

const SpellVisualEffectNameEntry *
ObjectEffectDataStore::FindSpellVisualEffectName(std::uint32_t id) const {
  auto it = sven_table_.find(id);
  return (it != sven_table_.end()) ? &it->second : nullptr;
}

const ObjectEffectGroupEntry *ObjectEffectDataStore::FindEffectGroup(std::uint32_t id) const {
  auto it = group_table_.find(id);
  return (it != group_table_.end()) ? &it->second : nullptr;
}

const ObjectEffectModifierEntry *ObjectEffectDataStore::FindModifier(std::uint32_t id) const {
  auto it = modifier_table_.find(id);
  return (it != modifier_table_.end()) ? &it->second : nullptr;
}

const ObjectEffectModifierData *ObjectEffectDataStore::FindModifierData(std::uint32_t id) const {
  const auto it = modifier_data_.find(id);
  return (it != modifier_data_.end()) ? &it->second : nullptr;
}

const ObjectEffectPackageRuntime *
ObjectEffectDataStore::FindPackageRuntime(std::uint32_t id) const {
  const auto it = package_runtime_.find(id);
  return (it != package_runtime_.end()) ? &it->second : nullptr;
}

std::array<std::uint32_t, kHardcodedEffectIdCount> HardcodedEffectIdTable::s_effect_ids{};

static constexpr const char *kHardcodedEffectNames[kHardcodedEffectIdCount] = {
    "HARDCODED Footstep Water Run Spray",
    "HARDCODED Footstep Water Walk Spray",
    "HARDCODED Breath Underwater",
    "HARDCODED Breath Cold",
    "HARDCODED Loot Art",
    "HARDCODED Unit Level Up",
    "HARDCODED Mount Poof",
    "HARDCODED Inebriated Bubbles",
    "HARDCODED Meeting Stone Join",
    "HARDCODED Reputation",
    "HARDCODED Resist Spell",
    "HARDCODED Achievement Base",
};

constexpr std::uint32_t kHardcodedEffectAttachmentSentinel = 0x32u;
constexpr std::array<std::uint32_t, kHardcodedEffectIdCount>
    kHardcodedEffectAttachmentPoints = {
        kHardcodedEffectAttachmentSentinel,
        kHardcodedEffectAttachmentSentinel,
        17u, 17u, 19u, 19u, 19u, 17u, 19u, 19u, 34u, 19u,
    };

void HardcodedEffectIdTable::Initialize(const DbcStore<SpellVisualEffectNameEntry> &sven_store) {
  Build(sven_store);
}

void HardcodedEffectIdTable::Build(const DbcStore<SpellVisualEffectNameEntry> &sven_store) {
  s_effect_ids.fill(0);

  for (const auto &entry : sven_store.entries()) {
    for (std::uint32_t i = 0; i < kHardcodedEffectIdCount; ++i) {
      if (openwow::text::EqualsIgnoreCaseAscii(entry.name, kHardcodedEffectNames[i])) {
        s_effect_ids[i] = entry.id;
        break;
      }
    }
  }
}

void HardcodedEffectIdTable::Reset() {
  s_effect_ids.fill(0);
}

std::uint32_t HardcodedEffectIdTable::GetEffectId(HardcodedEffectId index) {
  return s_effect_ids[static_cast<std::uint32_t>(index)];
}

std::uint32_t HardcodedEffectIdTable::GetEffectId(std::uint32_t index) {
  if (index >= kHardcodedEffectIdCount) {
    return 0;
  }
  return s_effect_ids[index];
}

std::optional<std::uint32_t> HardcodedEffectIdTable::GetAttachmentPoint(
    const HardcodedEffectId index) {
  const auto table_index = static_cast<std::uint32_t>(index);
  if (table_index >= kHardcodedEffectAttachmentPoints.size()) {
    return std::nullopt;
  }

  const auto attachment = kHardcodedEffectAttachmentPoints[table_index];
  if (attachment == kHardcodedEffectAttachmentSentinel) {
    return std::nullopt;
  }
  return attachment;
}

GameObjectTypeHandlerInfo GetTypeHandlerInfo(std::uint8_t go_type) {

  GameObjectTypeHandlerInfo info;

  switch (go_type) {
  case 0:
    info.event_id = 235;
    break;
  case 1:
    info.event_id = 236;
    break;
  case 4:
    info.interact_dist = 10.0f;
    break;
  case 2:
  case 9:
  case 24:
  case 26:
  case 27:
    info.interact_dist = 50.0f / 9.0f;
    break;
  case 7:
  case 32:
    info.interact_dist = 3.0f;
    break;
  case 12:
    info.interact_dist = 0.0f;
    break;
  case 17:
    info.interact_dist = 100.0f;
    break;
  default:
    break;
  }

  return info;
}

}
