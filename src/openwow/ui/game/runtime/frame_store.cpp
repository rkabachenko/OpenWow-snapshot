#include "openwow/ui/game/runtime/frame_store.h"

#include "openwow/foundation/text/ascii.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/framescript/core/frame_alpha.h"
#include "openwow/ui/game/framescript/core/frame_color_runtime.h"
#include "openwow/ui/game/framescript/core/frame_font_runtime.h"
#include "openwow/ui/game/framescript/core/frame_runtime_identity.h"
#include "openwow/ui/game/framescript/core/lua_script_object_access.h"
#include "openwow/ui/game/framescript/core/script_region_ownership.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/widgets/script_object.h"
#include "openwow/ui/widgets/simple_frame.h"
#include "openwow/ui/widgets/simple_game_tooltip.h"
#include "openwow/ui/widgets/simple_minimap.h"
#include "openwow/ui/widgets/simple_world_frame.h"
#include "openwow/ui/widgets/status_bar.h"

extern "C" {
#include <lua.hpp>
}

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace openwow::ui::game::runtime {
namespace {

void EraseValue(std::vector<std::string>& values, const std::string_view value) {
  values.erase(std::remove(values.begin(), values.end(), value), values.end());
}

struct TransparentStringHash {
  using is_transparent = void;

  std::size_t operator()(const std::string_view value) const noexcept {
    return std::hash<std::string_view>{}(value);
  }
};

struct TransparentStringEqual {
  using is_transparent = void;

  bool operator()(const std::string_view left,
                  const std::string_view right) const noexcept {
    return left == right;
  }
};

constexpr std::uint64_t kFrameHandleIndexBits = 20u;
constexpr std::uint64_t kFrameHandleIndexMask =
    (std::uint64_t{1} << kFrameHandleIndexBits) - 1u;
constexpr std::uint64_t kFrameHandleGenerationMask =
    ~kFrameHandleIndexMask;

FrameStore::FrameHandle MakeFrameHandle(const std::uint64_t index,
                                        const std::uint64_t generation) noexcept {
  return (index & kFrameHandleIndexMask) |
         ((generation << kFrameHandleIndexBits) & kFrameHandleGenerationMask);
}
std::uint64_t FrameHandleIndex(const FrameStore::FrameHandle handle) noexcept {
  return handle & kFrameHandleIndexMask;
}
std::uint64_t FrameHandleGeneration(const FrameStore::FrameHandle handle) noexcept {
  return (handle & kFrameHandleGenerationMask) >> kFrameHandleIndexBits;
}

}

struct FrameStore::Impl {
  struct Record {
    openwow::ui::framexml::UiFrame frame;
    std::unique_ptr<openwow::ui::widgets::CScriptObject> native_object;
    int lua_ref{LUA_NOREF};
    const void* lua_identity{nullptr};
  };

  struct EnumerableFrameRef {
    int lua_ref{LUA_NOREF};
    const void* native_identity{nullptr};
  };

  struct Slot {

    std::string_view key;
    std::unique_ptr<Record> record;
    std::uint64_t generation{0};

    FrameHandle parent_handle{kInvalidFrameHandle};
  };

  explicit Impl(Ports value_ports) : ports(std::move(value_ports)) {}

  [[nodiscard]] Record* RecordAt(const FrameHandle handle) noexcept {
    if (handle == kInvalidFrameHandle) return nullptr;
    const std::uint32_t index = FrameHandleIndex(handle);
    if (index >= slots.size()) return nullptr;
    Slot& slot = slots[index];
    if (slot.record == nullptr || slot.generation != FrameHandleGeneration(handle)) {
      return nullptr;
    }
    return slot.record.get();
  }
  [[nodiscard]] const Record* RecordAt(const FrameHandle handle) const noexcept {
    if (handle == kInvalidFrameHandle) return nullptr;
    const std::uint32_t index = FrameHandleIndex(handle);
    if (index >= slots.size()) return nullptr;
    const Slot& slot = slots[index];
    if (slot.record == nullptr || slot.generation != FrameHandleGeneration(handle)) {
      return nullptr;
    }
    return slot.record.get();
  }
  [[nodiscard]] FrameHandle HandleForKey(const std::string_view key) const noexcept {
    const auto found = handle_by_key.find(key);
    return found != handle_by_key.end() ? found->second : kInvalidFrameHandle;
  }
  [[nodiscard]] Record* FindRecord(const std::string_view key) noexcept {
    return RecordAt(HandleForKey(key));
  }
  [[nodiscard]] const Record* FindRecord(const std::string_view key) const noexcept {
    return RecordAt(HandleForKey(key));
  }

  FrameHandle AllocateSlot(const std::string& key) {
    std::uint32_t index;
    if (!free_indices.empty()) {
      index = free_indices.back();
      free_indices.pop_back();
    } else {
      index = static_cast<std::uint32_t>(slots.size());
      slots.emplace_back();
    }
    Slot& slot = slots[index];
    slot.generation =
        (slot.generation + 1u) & (kFrameHandleGenerationMask >> kFrameHandleIndexBits);
    slot.record = std::make_unique<Record>();
    slot.parent_handle = kInvalidFrameHandle;
    const FrameHandle handle = MakeFrameHandle(index, slot.generation);

    const auto [iterator, inserted] = handle_by_key.insert_or_assign(key, handle);
    (void)inserted;
    slot.key = iterator->first;
    return handle;
  }

  void FreeSlot(const FrameHandle handle) {
    if (handle == kInvalidFrameHandle) return;
    const std::uint32_t index = FrameHandleIndex(handle);
    if (index >= slots.size()) return;
    Slot& slot = slots[index];
    if (slot.record == nullptr) return;

    handle_by_key.erase(std::string(slot.key));
    slot.key = {};
    slot.record.reset();
    slot.parent_handle = kInvalidFrameHandle;
    free_indices.push_back(index);
  }

  FrameHandle ResolveParentHandle(const FrameHandle handle) {
    const Record* const record = RecordAt(handle);
    if (record == nullptr) return kInvalidFrameHandle;
    Slot& slot = slots[FrameHandleIndex(handle)];
    if (slot.parent_handle != kInvalidFrameHandle &&
        RecordAt(slot.parent_handle) != nullptr) {
      return slot.parent_handle;
    }
    const std::string_view parent = record->frame.parent;
    if (parent.empty()) {
      slot.parent_handle = kInvalidFrameHandle;
      return kInvalidFrameHandle;
    }
    slot.parent_handle = HandleForKey(parent);
    return slot.parent_handle;
  }

  void ResetParentHandleCache(const FrameHandle handle) noexcept {
    if (handle == kInvalidFrameHandle) return;
    const std::uint32_t index = FrameHandleIndex(handle);
    if (index < slots.size()) slots[index].parent_handle = kInvalidFrameHandle;
  }

  void TrackRegistration(const std::string& key, const FrameHandle handle) {
    if (key.empty()) return;
    registration_order.push_back(key);
    registration_handles.push_back(handle);
  }

  void RemoveRegistration(const std::string_view key) {
    const auto it = std::find(registration_order.begin(), registration_order.end(), key);
    if (it == registration_order.end()) return;
    const auto index = static_cast<std::size_t>(it - registration_order.begin());
    registration_order.erase(it);
    registration_handles.erase(registration_handles.begin() +
                               static_cast<std::ptrdiff_t>(index));
  }

  void TrackLuaRef(const int lua_ref,
                   const void* const enumerable_native_identity) {
    if (lua_ref == LUA_NOREF || !tracked_lua_ref_members.insert(lua_ref).second) {
      return;
    }
    tracked_lua_refs.push_back(lua_ref);
    if (lua == nullptr) return;

    const int top = lua_gettop(lua);
    lua_rawgeti(lua, LUA_REGISTRYINDEX, lua_ref);
    if (lua_istable(lua, -1) != 0 && enumerable_native_identity != nullptr) {
      lua_rawgeti(lua, -1, 0);
      const void* const attached_native_identity = lua_touserdata(lua, -1);
      lua_pop(lua, 1);
      if (attached_native_identity == enumerable_native_identity &&
          !enumerable_indices_by_native_identity.contains(
              enumerable_native_identity)) {
        const std::size_t index = enumerable_refs.size();
        enumerable_refs.push_back({lua_ref, enumerable_native_identity});
        enumerable_indices_by_ref.emplace(lua_ref, index);
        enumerable_indices_by_native_identity.emplace(enumerable_native_identity,
                                                      index);
      }
    }
    lua_settop(lua, top);
  }

  void UntrackLuaRef(const int lua_ref) {
    tracked_lua_ref_members.erase(lua_ref);
    tracked_lua_refs.erase(
        std::remove(tracked_lua_refs.begin(), tracked_lua_refs.end(), lua_ref),
        tracked_lua_refs.end());
    const auto found = enumerable_indices_by_ref.find(lua_ref);
    if (found == enumerable_indices_by_ref.end()) return;
    const std::size_t removed = found->second;
    enumerable_indices_by_ref.erase(found);
    enumerable_indices_by_native_identity.erase(
        enumerable_refs[removed].native_identity);
    enumerable_refs.erase(enumerable_refs.begin() +
                          static_cast<std::ptrdiff_t>(removed));
    for (std::size_t index = removed; index < enumerable_refs.size(); ++index) {
      enumerable_indices_by_ref[enumerable_refs[index].lua_ref] = index;
      enumerable_indices_by_native_identity[
          enumerable_refs[index].native_identity] = index;
    }
  }

  void RemoveChildLink(const std::string_view parent, const std::string_view child) {
    const auto found = children_by_parent.find(std::string(parent));
    if (found == children_by_parent.end()) return;
    EraseValue(found->second, child);
    if (found->second.empty()) children_by_parent.erase(found);
  }

  void AddChildLink(const std::string_view parent, const std::string_view child) {
    if (parent.empty()) return;
    auto& children = children_by_parent[std::string(parent)];
    if (std::find(children.begin(), children.end(), child) == children.end()) {
      children.emplace_back(child);
    }
  }

  void ReleaseSingle(const std::string& key) {
    const FrameHandle handle = HandleForKey(key);
    Record* const found_record = RecordAt(handle);
    if (found_record == nullptr) return;
    Record& record = *found_record;
    const std::string lua_name(record.frame.LuaName());
    const int ref = record.lua_ref;
    bool owns_global = false;

    if (lua != nullptr && ref != LUA_NOREF) {
      const int top = lua_gettop(lua);
      lua_rawgeti(lua, LUA_REGISTRYINDEX, ref);
      if (lua_istable(lua, -1) != 0 && record.native_object != nullptr) {
        lua_adapter::InvalidateNativeScriptObjectIdentity(lua, -1);
      }
      lua_settop(lua, top);
    }
    if (ports.before_binding_release) ports.before_binding_release(key, ref);
    if (lua != nullptr && ref != LUA_NOREF && !lua_name.empty()) {
      const int top = lua_gettop(lua);
      lua_getglobal(lua, lua_name.c_str());
      lua_rawgeti(lua, LUA_REGISTRYINDEX, ref);
      owns_global = lua_rawequal(lua, -1, -2) != 0;
      lua_settop(lua, top);
      if (owns_global) {
        openwow::ui::UnregisterLuaGlobal(lua, lua_name.c_str());
      }
    }
    if (auto* tooltip = dynamic_cast<openwow::ui::widgets::CSimpleGameTooltip*>(
            record.native_object.get());
        tooltip != nullptr) {
      tooltip->SetOwnsDefaultTooltipState(owns_global &&
                                          lua_name == "GameTooltip");
    }
    if (ref != LUA_NOREF) {
      if (record.lua_identity != nullptr) {
        binding_keys_by_identity.erase(record.lua_identity);
      }
      UntrackLuaRef(ref);
      if (lua != nullptr) {
        frame_api::ClearBoundFontObjectByRef(lua, ref);
        luaL_unref(lua, LUA_REGISTRYINDEX, ref);
      }
    }

    RemoveChildLink(record.frame.parent, key);
    children_by_parent.erase(key);
    RemoveRegistration(key);
    classification_index_dirty = true;
    auto native_object = std::move(record.native_object);
    FreeSlot(handle);
    native_object.reset();
    if (ports.after_identity_release) ports.after_identity_release(key);
  }

  void RebuildClassificationIndex() const {
    keys_by_kind.clear();
    scroll_child_keys.clear();
    for (const auto& key : registration_order) {
      const Record* const found = FindRecord(key);
      if (found == nullptr) continue;
      const auto& frame = found->frame;
      keys_by_kind[openwow::text::ToLowerAscii(frame.kind)].push_back(key);
      if (frame.scroll_child_content) scroll_child_keys.push_back(key);
    }
    classification_index_dirty = false;
  }

  Ports ports;
  lua_State* lua{nullptr};
  std::vector<Slot> slots;
  std::vector<std::uint32_t> free_indices;
  std::unordered_map<std::string, FrameHandle, TransparentStringHash,
                     TransparentStringEqual>
      handle_by_key;
  mutable std::unordered_map<std::string, std::vector<std::string>,
                             TransparentStringHash, TransparentStringEqual>
      keys_by_kind;
  mutable std::vector<std::string> scroll_child_keys;
  mutable bool classification_index_dirty{true};
  std::vector<std::string> registration_order;
  std::vector<FrameHandle> registration_handles;
  std::unordered_map<std::string, std::vector<std::string>> children_by_parent;
  std::vector<int> tracked_lua_refs;
  std::unordered_set<int> tracked_lua_ref_members;
  std::vector<EnumerableFrameRef> enumerable_refs;
  std::unordered_map<int, std::size_t> enumerable_indices_by_ref;
  std::unordered_map<const void*, std::size_t>
      enumerable_indices_by_native_identity;
  std::unordered_map<const void*, std::string> binding_keys_by_identity;
  std::uint64_t next_unique_id{1};
};

FrameStore::FrameStore(Ports ports)
    : impl_(std::make_unique<Impl>(std::move(ports))) {}
FrameStore::~FrameStore() = default;

void FrameStore::BindLuaState(lua_State* state) noexcept { impl_->lua = state; }

void FrameStore::Reserve(const std::size_t capacity) {
  impl_->slots.reserve(capacity);
  impl_->handle_by_key.reserve(capacity);
  impl_->registration_order.reserve(capacity);
  impl_->registration_handles.reserve(capacity);
  impl_->children_by_parent.reserve(capacity);
  impl_->tracked_lua_refs.reserve(capacity);
  impl_->tracked_lua_ref_members.reserve(capacity);
  impl_->enumerable_refs.reserve(capacity);
  impl_->enumerable_indices_by_ref.reserve(capacity);
  impl_->enumerable_indices_by_native_identity.reserve(capacity);
  impl_->binding_keys_by_identity.reserve(capacity);
}

std::size_t FrameStore::size() const noexcept {
  return impl_->slots.size() - impl_->free_indices.size();
}
std::size_t FrameStore::binding_count() const noexcept {
  return impl_->tracked_lua_refs.size();
}
std::size_t FrameStore::enumerable_frame_count() const noexcept {
  return impl_->enumerable_refs.size();
}
std::size_t FrameStore::registration_checkpoint() const noexcept {
  return impl_->registration_order.size();
}
bool FrameStore::contains(const std::string_view key) const {
  return impl_->FindRecord(key) != nullptr;
}

openwow::ui::framexml::UiFrame* FrameStore::FindFrame(
    const std::string_view key) {
  auto* const record = impl_->FindRecord(key);
  return record != nullptr ? &record->frame : nullptr;
}
const openwow::ui::framexml::UiFrame* FrameStore::FindFrame(
    const std::string_view key) const {
  const auto* const record = impl_->FindRecord(key);
  return record != nullptr ? &record->frame : nullptr;
}
std::optional<int> FrameStore::FindLuaRef(const std::string_view key) const {
  const auto* const record = impl_->FindRecord(key);
  if (record == nullptr || record->lua_ref == LUA_NOREF) return std::nullopt;
  return record->lua_ref;
}
std::optional<int> FrameStore::FindLuaRef(const FrameHandle handle) const {
  const auto* const record = impl_->RecordAt(handle);
  if (record == nullptr || record->lua_ref == LUA_NOREF) return std::nullopt;
  return record->lua_ref;
}

FrameStore::FrameHandle FrameStore::HandleOf(const std::string_view key) const {
  return impl_->HandleForKey(key);
}
FrameStore::FrameHandle FrameStore::ParentHandleOf(const FrameHandle handle) const {

  return impl_->ResolveParentHandle(handle);
}
std::string_view FrameStore::KeyOf(const FrameHandle handle) const {
  if (impl_->RecordAt(handle) == nullptr) return {};
  return impl_->slots[FrameHandleIndex(handle)].key;
}
openwow::ui::framexml::UiFrame* FrameStore::FindFrame(const FrameHandle handle) {
  auto* const record = impl_->RecordAt(handle);
  return record != nullptr ? &record->frame : nullptr;
}
const openwow::ui::framexml::UiFrame* FrameStore::FindFrame(
    const FrameHandle handle) const {
  const auto* const record = impl_->RecordAt(handle);
  return record != nullptr ? &record->frame : nullptr;
}
std::span<const FrameStore::FrameHandle> FrameStore::registration_handles()
    const noexcept {
  return impl_->registration_handles;
}

std::optional<std::string> FrameStore::FindKeyForLuaTable(
    lua_State* state, int table_index) const {
  if (state == nullptr || lua_istable(state, table_index) == 0) {
    return std::nullopt;
  }
  const void* identity = lua_topointer(state, table_index);
  const auto found = impl_->binding_keys_by_identity.find(identity);
  return found != impl_->binding_keys_by_identity.end()
             ? std::optional<std::string>(found->second)
             : std::nullopt;
}

openwow::ui::widgets::CSimpleFrame* FrameStore::FindSimpleFrame(
    const std::string_view key) {
  auto* const record = impl_->FindRecord(key);
  return record != nullptr
             ? dynamic_cast<openwow::ui::widgets::CSimpleFrame*>(
                   record->native_object.get())
             : nullptr;
}
openwow::ui::widgets::StatusBar* FrameStore::FindStatusBar(
    const std::string_view key) {
  auto* const record = impl_->FindRecord(key);
  return record != nullptr
             ? dynamic_cast<openwow::ui::widgets::StatusBar*>(
                   record->native_object.get())
             : nullptr;
}
const openwow::ui::widgets::StatusBar* FrameStore::FindStatusBar(
    const std::string_view key) const {
  const auto* const record = impl_->FindRecord(key);
  return record != nullptr
             ? dynamic_cast<const openwow::ui::widgets::StatusBar*>(
                   record->native_object.get())
             : nullptr;
}
openwow::ui::widgets::CSimpleMinimap* FrameStore::FindMinimap(
    const std::string_view key) {
  auto* const record = impl_->FindRecord(key);
  return record != nullptr
             ? dynamic_cast<openwow::ui::widgets::CSimpleMinimap*>(
                   record->native_object.get())
             : nullptr;
}
openwow::ui::widgets::CSimpleWorldFrame* FrameStore::FindWorldFrame(
    const std::string_view key) {
  auto* const record = impl_->FindRecord(key);
  return record != nullptr
             ? dynamic_cast<openwow::ui::widgets::CSimpleWorldFrame*>(
                   record->native_object.get())
             : nullptr;
}

std::vector<FrameStore::FrameView> FrameStore::CollectStableFrames() {
  std::vector<FrameView> result;
  result.reserve(size());
  for (auto& slot : impl_->slots) {
    if (slot.record == nullptr) continue;
    result.push_back({slot.key, &slot.record->frame, slot.record->lua_ref});
  }
  return result;
}
std::vector<FrameStore::ConstFrameView> FrameStore::CollectStableFrames() const {
  std::vector<ConstFrameView> result;
  result.reserve(size());
  for (const auto& slot : impl_->slots) {
    if (slot.record == nullptr) continue;
    result.push_back({slot.key, &slot.record->frame, slot.record->lua_ref});
  }
  return result;
}

void FrameStore::ForEachStableFrame(
    const std::function<void(const ConstFrameView&)>& visitor) const {
  if (!visitor) return;
  for (const auto& slot : impl_->slots) {
    if (slot.record == nullptr) continue;
    visitor(ConstFrameView{slot.key, &slot.record->frame, slot.record->lua_ref});
  }
}

std::vector<openwow::ui::framexml::UiFrame>
FrameStore::CollectFramesInRegistrationOrder() const {
  std::vector<openwow::ui::framexml::UiFrame> result;
  result.reserve(size());
  for (const FrameHandle handle : impl_->registration_handles) {
    if (const auto* frame = FindFrame(handle); frame != nullptr) {
      result.push_back(*frame);
    }
  }
  return result;
}

std::vector<const openwow::ui::framexml::UiFrame*>
FrameStore::CollectFramePointersInRegistrationOrder() const {
  std::vector<const openwow::ui::framexml::UiFrame*> result;
  result.reserve(size());
  for (const FrameHandle handle : impl_->registration_handles) {
    if (const auto* frame = FindFrame(handle); frame != nullptr) result.push_back(frame);
  }
  return result;
}

std::vector<std::string> FrameStore::CollectBindingKeys() const {
  return impl_->registration_order;
}
std::span<const std::string> FrameStore::registration_order() const noexcept {
  return impl_->registration_order;
}
std::span<const int> FrameStore::tracked_lua_refs() const noexcept {
  return impl_->tracked_lua_refs;
}

std::span<const std::string> FrameStore::FramesOfKind(
    const std::string_view kind) const {
  if (impl_->classification_index_dirty) impl_->RebuildClassificationIndex();
  const auto found =
      impl_->keys_by_kind.find(openwow::text::ToLowerAscii(std::string(kind)));
  if (found == impl_->keys_by_kind.end()) return {};
  return found->second;
}

std::span<const std::string> FrameStore::ScrollChildContentFrames() const {
  if (impl_->classification_index_dirty) impl_->RebuildClassificationIndex();
  return impl_->scroll_child_keys;
}

void FrameStore::InvalidateClassificationIndex() noexcept {
  impl_->classification_index_dirty = true;
}

std::string FrameStore::AllocateUniqueKey(const std::string_view preferred_key) {
  const auto available = [this](const std::string& key) {
    return impl_->FindRecord(key) == nullptr;
  };
  if (preferred_key.empty()) {
    for (;;) {
      std::string key = "$anonymous_frame_" +
                        std::to_string(impl_->next_unique_id++);
      if (available(key)) return key;
    }
  }
  std::string preferred(preferred_key);
  if (available(preferred)) return preferred;
  for (;;) {
    std::string key = preferred + ".__ow_instance_" +
                      std::to_string(impl_->next_unique_id++);
    if (available(key)) return key;
  }
}

openwow::ui::framexml::UiFrame& FrameStore::InsertFrame(
    std::string key, openwow::ui::framexml::UiFrame frame) {
  frame.name = key;
  const std::string parent = frame.parent;
  if (auto* const existing = impl_->FindRecord(key); existing != nullptr) {
    const std::string previous_parent = existing->frame.parent;
    existing->frame = std::move(frame);
    impl_->classification_index_dirty = true;
    impl_->RemoveChildLink(previous_parent, key);
    impl_->AddChildLink(parent, key);
    impl_->ResetParentHandleCache(impl_->HandleForKey(key));
    if (impl_->ports.hierarchy_invalidated)
      impl_->ports.hierarchy_invalidated();
    return existing->frame;
  }
  const FrameStore::FrameHandle handle = impl_->AllocateSlot(key);
  auto* const record = impl_->RecordAt(handle);
  record->frame = std::move(frame);
  impl_->TrackRegistration(key, handle);
  impl_->classification_index_dirty = true;
  impl_->AddChildLink(parent, key);
  if (impl_->ports.hierarchy_invalidated) impl_->ports.hierarchy_invalidated();
  return record->frame;
}

void FrameStore::ReplaceFrame(const std::string_view key,
                              openwow::ui::framexml::UiFrame frame) {
  auto* const existing = impl_->FindRecord(key);
  if (existing == nullptr) {
    InsertFrame(std::string(key), std::move(frame));
    return;
  }
  const std::string previous_parent = existing->frame.parent;
  frame.name.assign(key);
  existing->frame = std::move(frame);
  impl_->classification_index_dirty = true;
  NotifyHierarchyMutation(key, previous_parent);
}

void FrameStore::AttachLuaBinding(const std::string_view key, const int lua_ref) {
  auto* const record = impl_->FindRecord(key);
  if (record == nullptr) return;
  if (record->lua_identity != nullptr) {
    impl_->binding_keys_by_identity.erase(record->lua_identity);
    record->lua_identity = nullptr;
  }
  if (record->lua_ref != LUA_NOREF) {
    impl_->UntrackLuaRef(record->lua_ref);
  }
  record->lua_ref = lua_ref;
  if (impl_->lua != nullptr && lua_ref != LUA_NOREF) {
    const int top = lua_gettop(impl_->lua);
    lua_rawgeti(impl_->lua, LUA_REGISTRYINDEX, lua_ref);
    if (lua_istable(impl_->lua, -1) != 0) {
      record->lua_identity = lua_topointer(impl_->lua, -1);
      if (record->lua_identity != nullptr) {
        impl_->binding_keys_by_identity.insert_or_assign(
            record->lua_identity, std::string(key));
      }
    }
    lua_settop(impl_->lua, top);
  }
  const auto* const native_object = record->native_object.get();
  const void* const enumerable_native_identity =
      native_object != nullptr &&
              native_object->IsKindOf(openwow::ui::widgets::ScriptObjectType::Frame)
          ? native_object
          : nullptr;
  impl_->TrackLuaRef(lua_ref, enumerable_native_identity);
}
void FrameStore::AttachNativeObject(
    const std::string_view key,
    std::unique_ptr<openwow::ui::widgets::CScriptObject> object) {
  if (auto* const record = impl_->FindRecord(key); record != nullptr) {
    record->native_object = std::move(object);
  }
}
void FrameStore::NotifyHierarchyMutation(const std::string_view key,
                                         const std::string_view previous_parent) {
  auto* frame = FindFrame(key);
  if (frame == nullptr) return;

  const auto inherited_membership = [this](const std::string_view parent) {
    const auto* parent_frame = FindFrame(parent);
    return parent_frame != nullptr
               ? parent_frame->scroll_child_membership_count
               : std::size_t{0u};
  };
  const std::size_t previous_membership =
      inherited_membership(previous_parent);
  const std::size_t next_membership = inherited_membership(frame->parent);
  if (previous_membership != next_membership) {
    for (const auto& descendant : CollectSubtreePostorder(key)) {
      auto* member = FindFrame(descendant);
      if (member == nullptr) continue;
      if (next_membership > previous_membership) {
        member->scroll_child_membership_count +=
            next_membership - previous_membership;
      } else {
        member->scroll_child_membership_count -= std::min(
            member->scroll_child_membership_count,
            previous_membership - next_membership);
      }
      member->scroll_child_content =
          member->scroll_child_membership_count != 0u;
    }
    impl_->classification_index_dirty = true;
  }
  impl_->RemoveChildLink(previous_parent, key);
  impl_->AddChildLink(frame->parent, key);

  impl_->ResetParentHandleCache(impl_->HandleForKey(key));
  if (impl_->ports.hierarchy_invalidated) impl_->ports.hierarchy_invalidated();
}
void FrameStore::InvalidatePaintOrder() {
  if (impl_->ports.paint_order_invalidated) impl_->ports.paint_order_invalidated();
}
void FrameStore::InvalidateHitTest() {
  if (impl_->ports.hit_test_invalidated) impl_->ports.hit_test_invalidated();
}

bool FrameStore::PushNewestEnumerableFrame(lua_State* state) const {
  if (state == nullptr || impl_->enumerable_refs.empty()) return false;
  lua_rawgeti(state, LUA_REGISTRYINDEX, impl_->enumerable_refs.back().lua_ref);
  if (lua_istable(state, -1) != 0) return true;
  lua_pop(state, 1);
  return false;
}
bool FrameStore::ContainsEnumerableFrameNativeIdentity(
    const void* const native_identity) const noexcept {
  return native_identity != nullptr &&
         impl_->enumerable_indices_by_native_identity.contains(native_identity);
}
bool FrameStore::PushNextEnumerableFrame(
    lua_State* state, const void* const native_identity) const {
  if (state == nullptr || native_identity == nullptr) return false;
  const auto found =
      impl_->enumerable_indices_by_native_identity.find(native_identity);
  if (found == impl_->enumerable_indices_by_native_identity.end() ||
      found->second == 0u) {
    return false;
  }
  lua_rawgeti(state, LUA_REGISTRYINDEX,
              impl_->enumerable_refs[found->second - 1u].lua_ref);
  if (lua_istable(state, -1) != 0) return true;
  lua_pop(state, 1);
  return false;
}

void FrameStore::SetFrameUserPlaced(const std::string_view key,
                                    const bool user_placed) {
  if (auto* frame = FindFrame(key); frame != nullptr) frame->user_placed = user_placed;
}
void FrameStore::SetFrameDontSavePosition(const std::string_view key,
                                          const bool value) {
  if (auto* frame = FindFrame(key); frame != nullptr) frame->dont_save_position = value;
}
void FrameStore::SetFrameAlphaByte(const std::string_view key,
                                   const std::uint8_t alpha_byte) {
  if (auto* frame = FindFrame(key); frame != nullptr) {
    frame->color_a = static_cast<float>(NormalizeFrameAlphaByte(alpha_byte));
  }
}
void FrameStore::SetRegionDrawLayer(const std::string_view key,
                                    const std::string_view draw_layer) {
  auto* frame = FindFrame(key);
  if (frame == nullptr ||
      (frame->runtime_kind != openwow::ui::framexml::UiFrame::RuntimeKind::Texture &&
       frame->runtime_kind != openwow::ui::framexml::UiFrame::RuntimeKind::FontString) ||
      frame->draw_layer == draw_layer) {
    return;
  }
  frame->draw_layer.assign(draw_layer);

  if (impl_->ports.order_key_invalidated) impl_->ports.order_key_invalidated(key);
  InvalidateHitTest();
}

void FrameStore::SetTextureRole(
    const std::string_view key, const std::string_view owner,
    const openwow::ui::framexml::UiFrame::TextureRole role,
    const void* texture_identity) {
  auto* frame = FindFrame(key);
  if (frame == nullptr || frame->runtime_kind !=
                              openwow::ui::framexml::UiFrame::RuntimeKind::Texture)
    return;
  const std::string previous_owner = frame->parent;
  const auto previous_role = frame->texture_role;
  if (previous_role ==
          openwow::ui::framexml::UiFrame::TextureRole::StatusBarFill &&
      (role != previous_role || previous_owner != owner)) {
    if (auto* status_bar = FindStatusBar(previous_owner); status_bar != nullptr) {
      (void)status_bar->DetachTexture(texture_identity);
    }
  }
  frame->parent.assign(owner);
  frame->texture_role = role;

  if (previous_owner != owner) {
    NotifyHierarchyMutation(key, previous_owner);
    if (impl_->ports.layout_graph_invalidated)
      impl_->ports.layout_graph_invalidated();
    InvalidatePaintOrder();
    InvalidateHitTest();
  }
  if (role != openwow::ui::framexml::UiFrame::TextureRole::StatusBarFill)
    return;
  auto* status_bar = FindStatusBar(owner);
  if (status_bar == nullptr) return;
  (void)status_bar->AttachTextureDeferred(texture_identity);
  const auto ref = FindLuaRef(key);
  if (impl_->lua == nullptr || !ref.has_value()) return;
  const int top = lua_gettop(impl_->lua);
  lua_rawgeti(impl_->lua, LUA_REGISTRYINDEX, *ref);
  if (lua_istable(impl_->lua, -1) != 0) {
    (void)frame_api::ApplyStatusBarTextureRotation(
        impl_->lua, -1, status_bar->Snapshot().rotates_texture);
  }
  lua_settop(impl_->lua, top);
}

void FrameStore::SetTextureColor(const std::string_view key, const float red,
                                 const float green, const float blue,
                                 const float alpha) {
  auto* frame = FindFrame(key);
  if (frame == nullptr || frame->runtime_kind !=
                              openwow::ui::framexml::UiFrame::RuntimeKind::Texture)
    return;
  const auto normalize = [](const float component) {
    return static_cast<float>(frame_api::NormalizeScriptColorByte(
        frame_api::QuantizeScriptColorByte(component)));
  };
  frame->color_r = normalize(red);
  frame->color_g = normalize(green);
  frame->color_b = normalize(blue);
  frame->color_a = normalize(alpha);
  frame->has_vertex_color = true;
}

std::optional<std::array<float, 4>> FrameStore::FindStatusBarColor(
    const std::string_view owner) const {
  for (const auto& view : CollectStableFrames()) {
    const auto& frame = *view.frame;
    if (frame.runtime_kind ==
            openwow::ui::framexml::UiFrame::RuntimeKind::Texture &&
        frame.texture_role ==
            openwow::ui::framexml::UiFrame::TextureRole::StatusBarFill &&
        frame.parent == owner) {
      return std::array{frame.color_r, frame.color_g, frame.color_b, frame.color_a};
    }
  }
  return std::nullopt;
}
void FrameStore::SetScrollOffset(const std::string_view key, const bool horizontal,
                                 const float offset) {
  auto* frame = FindFrame(key);
  if (frame == nullptr || !openwow::text::EqualsIgnoreCaseAscii(frame->kind, "ScrollFrame")) return;
  const float finite = std::isfinite(offset) ? offset : 0.0F;
  if (horizontal) frame->runtime_horizontal_scroll = finite;
  else frame->runtime_vertical_scroll = finite;
  InvalidateHitTest();
}
void FrameStore::SetScrollChildSubtree(const std::string_view root_key,
                                       const bool enabled) {
  if (root_key.empty()) return;
  std::vector<std::string> pending{std::string(root_key)};
  std::unordered_set<std::string> visited{pending.front()};
  for (std::size_t cursor = 0; cursor < pending.size(); ++cursor) {
    const auto children = impl_->children_by_parent.find(pending[cursor]);
    if (children != impl_->children_by_parent.end()) {
      for (const auto& child : children->second) {
        if (visited.insert(child).second) pending.push_back(child);
      }
    }
  }
  for (const auto& key : pending) {
    auto* frame = FindFrame(key);
    if (frame == nullptr) continue;
    if (enabled) {
      ++frame->scroll_child_membership_count;
    } else if (frame->scroll_child_membership_count != 0u) {
      --frame->scroll_child_membership_count;
    }
    frame->scroll_child_content = frame->scroll_child_membership_count != 0u;
  }
  impl_->classification_index_dirty = true;

  if (impl_->ports.inherited_order_invalidated)
    impl_->ports.inherited_order_invalidated(root_key);
  InvalidateHitTest();
}

std::vector<std::string> FrameStore::CollectSubtreePostorder(
    const std::string_view root_key) const {
  struct Cursor { std::string key; std::size_t next_child{0}; };
  std::vector<Cursor> stack{{std::string(root_key), 0}};
  std::unordered_set<std::string> visited{std::string(root_key)};
  std::vector<std::string> result;
  while (!stack.empty()) {
    auto& cursor = stack.back();
    const auto children = impl_->children_by_parent.find(cursor.key);
    if (children != impl_->children_by_parent.end() &&
        cursor.next_child < children->second.size()) {
      const std::string& child = children->second[cursor.next_child++];
      if (visited.insert(child).second) stack.push_back({child, 0});
      continue;
    }
    result.push_back(std::move(cursor.key));
    stack.pop_back();
  }
  return result;
}

void FrameStore::DestroySubtree(const std::string_view root_key) {
  if (!contains(root_key)) return;
  if (impl_->lua != nullptr) detail::BumpLuaLayoutProtectionGeneration(impl_->lua);
  for (const auto& key : CollectSubtreePostorder(root_key)) impl_->ReleaseSingle(key);
  if (impl_->ports.layout_graph_invalidated)
    impl_->ports.layout_graph_invalidated();
}

void FrameStore::RollbackRegistrationsAfter(const std::size_t checkpoint) {
  if (checkpoint >= impl_->registration_order.size()) return;
  if (impl_->lua != nullptr) detail::BumpLuaLayoutProtectionGeneration(impl_->lua);
  const std::vector<std::string> added(
      impl_->registration_order.begin() + static_cast<std::ptrdiff_t>(checkpoint),
      impl_->registration_order.end());
  for (auto it = added.rbegin(); it != added.rend(); ++it) {
    impl_->ReleaseSingle(*it);
  }
  if (impl_->ports.layout_graph_invalidated)
    impl_->ports.layout_graph_invalidated();
}

void FrameStore::ReleaseAllLuaBindingsWhileValid() {
  if (impl_->lua == nullptr) return;
  for (const int ref : impl_->tracked_lua_refs) {
    frame_api::ClearBoundFontObjectByRef(impl_->lua, ref);
    luaL_unref(impl_->lua, LUA_REGISTRYINDEX, ref);
  }
  impl_->tracked_lua_refs.clear();
  impl_->tracked_lua_ref_members.clear();
  impl_->enumerable_refs.clear();
  impl_->enumerable_indices_by_ref.clear();
  impl_->enumerable_indices_by_native_identity.clear();
  impl_->binding_keys_by_identity.clear();
  for (auto& slot : impl_->slots) {
    if (slot.record == nullptr) continue;
    slot.record->lua_ref = LUA_NOREF;
    slot.record->lua_identity = nullptr;
  }
}

void FrameStore::DestroyNativeObjectsWhileLuaValid() {
  for (auto& slot : impl_->slots) {
    if (slot.record == nullptr) continue;
    slot.record->native_object.reset();
  }
}

void FrameStore::ClearAfterLuaDestroyed() {
  impl_->slots.clear();
  impl_->free_indices.clear();
  impl_->handle_by_key.clear();
  impl_->registration_order.clear();
  impl_->registration_handles.clear();
  impl_->keys_by_kind.clear();
  impl_->scroll_child_keys.clear();
  impl_->classification_index_dirty = true;
  impl_->children_by_parent.clear();
  impl_->tracked_lua_refs.clear();
  impl_->tracked_lua_ref_members.clear();
  impl_->enumerable_refs.clear();
  impl_->enumerable_indices_by_ref.clear();
  impl_->enumerable_indices_by_native_identity.clear();
  impl_->binding_keys_by_identity.clear();
  impl_->lua = nullptr;
}

}
