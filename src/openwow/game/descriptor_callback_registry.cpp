#include "openwow/game/descriptor_callback_registry.h"

#include "openwow/game/objects/cgobject.h"
#include "openwow/game/update_fields.h"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <unordered_map>
#include <utility>

namespace openwow::game {

namespace {

std::uint16_t ByteOffsetToField(std::uint16_t offset_bytes) {
  return static_cast<std::uint16_t>(offset_bytes / sizeof(std::uint32_t));
}

bool IsSupportedSectionRegistrationStart(const TypeID section_type,
                                         const std::uint16_t relative_offset_bytes) {
  const auto relative_field = ByteOffsetToField(relative_offset_bytes);

  switch (section_type) {
    case TypeID::kItem: {

      constexpr std::array<std::uint16_t, 47> kSupportedStartFields = {
          0,  1,  8,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
          23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38,
          39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 54, 55,
      };
      return std::binary_search(kSupportedStartFields.begin(),
                                kSupportedStartFields.end(),
                                relative_field);
    }
    case TypeID::kContainer:

      return relative_field >= 2 && relative_field <= 73;
    default:
      return true;
  }
}

std::optional<std::uint16_t> GetSectionBaseField(const TypeID section_type) {
  switch (section_type) {
    case TypeID::kObject:
      return 0;
    case TypeID::kItem:
      return OBJECT_END;
    case TypeID::kContainer:
      return ITEM_END;
    case TypeID::kUnit:
      return OBJECT_END;
    case TypeID::kPlayer:
      return UNIT_END;
    case TypeID::kGameObject:
      return OBJECT_END;
    case TypeID::kDynamicObject:
      return OBJECT_END;
    case TypeID::kCorpse:
      return OBJECT_END;
    default:
      return std::nullopt;
  }
}

std::optional<std::uint16_t> ResolveSectionOffsetBytes(
    const TypeID section_type,
    const std::uint16_t relative_offset_bytes) {
  if (!IsSupportedSectionRegistrationStart(section_type, relative_offset_bytes)) {
    return std::nullopt;
  }

  const auto base_field = GetSectionBaseField(section_type);
  if (!base_field.has_value()) {
    return std::nullopt;
  }

  const std::uint32_t absolute_offset_bytes =
      static_cast<std::uint32_t>(*base_field) * sizeof(std::uint32_t) +
      relative_offset_bytes;
  if (absolute_offset_bytes > std::numeric_limits<std::uint16_t>::max()) {
    return std::nullopt;
  }

  return static_cast<std::uint16_t>(absolute_offset_bytes);
}

std::uint16_t ByteRangeToFieldCount(std::uint16_t offset_bytes,
                                    std::uint16_t size_bytes) {
  const std::uint16_t clamped_size = size_bytes == 0 ? 1 : size_bytes;
  const std::uint16_t last_byte =
      static_cast<std::uint16_t>(offset_bytes + clamped_size - 1);
  const std::uint16_t first_field = ByteOffsetToField(offset_bytes);
  const std::uint16_t last_field = ByteOffsetToField(last_byte);
  return static_cast<std::uint16_t>(last_field - first_field + 1);
}

bool IsSectionTypeCompatibleWithObject(const TypeID section_type,
                                       const TypeID object_type) {
  switch (section_type) {
    case TypeID::kObject:
      return true;
    case TypeID::kItem:
      return object_type == TypeID::kItem || object_type == TypeID::kContainer;
    case TypeID::kContainer:
      return object_type == TypeID::kContainer;
    case TypeID::kUnit:
      return object_type == TypeID::kUnit || object_type == TypeID::kPlayer;
    case TypeID::kPlayer:
      return object_type == TypeID::kPlayer;
    case TypeID::kGameObject:
      return object_type == TypeID::kGameObject;
    case TypeID::kDynamicObject:
      return object_type == TypeID::kDynamicObject;
    case TypeID::kCorpse:
      return object_type == TypeID::kCorpse;
    default:
      return false;
  }
}

std::uint32_t GetWordValueForDispatchStep(
    const CGObject_C& object,
    const std::uint16_t field_index,
    const std::size_t dispatch_step,
    const bool include_current_field,
    const std::unordered_map<std::uint16_t, std::uint32_t>& old_values,
    const std::unordered_map<std::uint16_t, std::size_t>& change_order) {
  const auto order_it = change_order.find(field_index);
  if (order_it == change_order.end()) {
    return object.GetUInt32(field_index);
  }

  const bool change_already_applied =
      include_current_field ? (order_it->second <= dispatch_step)
                            : (order_it->second < dispatch_step);
  if (change_already_applied) {
    return object.GetUInt32(field_index);
  }

  const auto old_it = old_values.find(field_index);
  return old_it != old_values.end() ? old_it->second : object.GetUInt32(field_index);
}

}

DescriptorCallbackRegistry& DescriptorCallbackRegistry::Get() {
  static DescriptorCallbackRegistry registry;
  return registry;
}

DescriptorCallbackRegistry::Handle
DescriptorCallbackRegistry::RegisterTypeCallback(TypeID type_id,
                                                 std::uint16_t offset_bytes,
                                                 std::uint16_t size_bytes,
                                                 DescriptorCallback callback,
                                                 DescriptorCallbackBinding binding) {
  return RegisterCommon(MatchKind::kTypeAbsolute, type_id, ObjectGuid(), false,
                        offset_bytes, size_bytes,
                        std::move(callback), binding);
}

DescriptorCallbackRegistry::Handle
DescriptorCallbackRegistry::RegisterTypeSectionCallback(
    const TypeID section_type,
    const std::uint16_t relative_offset_bytes,
    const std::uint16_t size_bytes,
    DescriptorCallback callback,
    DescriptorCallbackBinding binding) {
  const auto absolute_offset_bytes =
      ResolveSectionOffsetBytes(section_type, relative_offset_bytes);
  if (!absolute_offset_bytes.has_value()) {
    return {};
  }

  return RegisterCommon(MatchKind::kTypeSection, section_type, ObjectGuid(), false,
                        *absolute_offset_bytes,
                        size_bytes, std::move(callback), binding);
}

DescriptorCallbackRegistry::Handle
DescriptorCallbackRegistry::RegisterObjectCallback(ObjectGuid guid,
                                                   std::uint16_t offset_bytes,
                                                   std::uint16_t size_bytes,
                                                   DescriptorCallback callback,
                                                   DescriptorCallbackBinding binding) {
  return RegisterCommon(MatchKind::kGuidAbsolute, TypeID::kObject, guid, true,
                        offset_bytes, size_bytes,
                        std::move(callback), binding);
}

DescriptorCallbackRegistry::Handle
DescriptorCallbackRegistry::RegisterObjectSectionCallback(
    const ObjectGuid guid,
    const TypeID section_type,
    const std::uint16_t relative_offset_bytes,
    const std::uint16_t size_bytes,
    DescriptorCallback callback,
    DescriptorCallbackBinding binding) {
  const auto absolute_offset_bytes =
      ResolveSectionOffsetBytes(section_type, relative_offset_bytes);
  if (!absolute_offset_bytes.has_value()) {
    return {};
  }

  return RegisterCommon(MatchKind::kGuidSection, section_type, guid, true,
                        *absolute_offset_bytes,
                        size_bytes, std::move(callback), binding);
}

DescriptorCallbackRegistry::Handle DescriptorCallbackRegistry::RegisterCommon(
    const MatchKind match_kind,
    TypeID type_id,
    ObjectGuid guid,
    bool has_guid_filter,
    std::uint16_t offset_bytes,
    std::uint16_t size_bytes,
    DescriptorCallback callback,
    DescriptorCallbackBinding binding) {
  if (!callback) {
    return {};
  }

  Entry entry;
  entry.handle = Handle{next_handle_++};
  entry.type_id = type_id;
  entry.guid = guid;
  entry.match_kind = match_kind;
  entry.has_guid_filter = has_guid_filter;
  entry.first_field = ByteOffsetToField(offset_bytes);
  entry.field_count = ByteRangeToFieldCount(offset_bytes, size_bytes);
  entry.offset_bytes = offset_bytes;
  entry.size_bytes = size_bytes == 0 ? 1 : size_bytes;
  entry.binding = binding;
  entry.callback = std::move(callback);
  entries_.push_back(std::move(entry));
  return entries_.back().handle;
}

bool DescriptorCallbackRegistry::Unregister(Handle handle) {
  if (!handle) {
    return false;
  }

  for (auto it = entries_.begin(); it != entries_.end(); ++it) {
    if (it->handle != handle || it->removed) {
      continue;
    }

    it->removed = true;
    if (!it->dispatching && dispatch_depth_ == 0) {
      entries_.erase(it);
    }
    return true;
  }
  return false;
}

std::size_t DescriptorCallbackRegistry::UnregisterObjectCallbacks(const ObjectGuid guid) {
  if (!guid) {
    return 0;
  }

  std::size_t removed_count = 0;
  for (auto& entry : entries_) {
    if (entry.removed || !entry.has_guid_filter || entry.guid != guid) {
      continue;
    }

    entry.removed = true;
    ++removed_count;
  }

  if (removed_count != 0 && dispatch_depth_ == 0) {
    CompactRemoved();
  }

  return removed_count;
}

bool DescriptorCallbackRegistry::UnregisterTypeCallback(
    const TypeID type_id,
    const std::uint16_t offset_bytes,
    const DescriptorCallbackBinding binding) {
  const auto target_field = ByteOffsetToField(offset_bytes);

  for (auto it = entries_.begin(); it != entries_.end(); ++it) {
    if (it->removed || it->match_kind != MatchKind::kTypeAbsolute ||
        it->has_guid_filter || it->type_id != type_id ||
        it->first_field != target_field || it->binding != binding) {
      continue;
    }

    it->removed = true;
    if (!it->dispatching && dispatch_depth_ == 0) {
      entries_.erase(it);
    }
    return true;
  }
  return false;
}

bool DescriptorCallbackRegistry::UnregisterTypeSectionCallback(
    const TypeID section_type,
    const std::uint16_t relative_offset_bytes,
    const DescriptorCallbackBinding binding) {
  const auto absolute_offset_bytes =
      ResolveSectionOffsetBytes(section_type, relative_offset_bytes);
  if (!absolute_offset_bytes.has_value()) {
    return false;
  }

  const auto target_field = ByteOffsetToField(*absolute_offset_bytes);

  for (auto it = entries_.begin(); it != entries_.end(); ++it) {
    if (it->removed || it->match_kind != MatchKind::kTypeSection ||
        it->has_guid_filter || it->type_id != section_type ||
        it->first_field != target_field || it->binding != binding) {
      continue;
    }

    it->removed = true;
    if (!it->dispatching && dispatch_depth_ == 0) {
      entries_.erase(it);
    }
    return true;
  }

  return false;
}

bool DescriptorCallbackRegistry::UnregisterObjectSectionCallback(
    const ObjectGuid guid,
    const TypeID section_type,
    const std::uint16_t relative_offset_bytes,
    const DescriptorCallbackBinding binding) {
  if (!guid) {
    return false;
  }

  const auto absolute_offset_bytes =
      ResolveSectionOffsetBytes(section_type, relative_offset_bytes);
  if (!absolute_offset_bytes.has_value()) {
    return false;
  }

  const auto target_field = ByteOffsetToField(*absolute_offset_bytes);

  for (auto it = entries_.begin(); it != entries_.end(); ++it) {
    if (it->removed || it->match_kind != MatchKind::kGuidSection ||
        !it->has_guid_filter || it->guid != guid ||
        it->type_id != section_type ||
        it->first_field != target_field || it->binding != binding) {
      continue;
    }

    it->removed = true;
    if (!it->dispatching && dispatch_depth_ == 0) {
      entries_.erase(it);
    }
    return true;
  }

  return false;
}

void DescriptorCallbackRegistry::Dispatch(const CGObject_C& object,
                                          const FieldUpdateBatch& updates,
                                          bool is_create) {

  if (is_create || entries_.empty() || updates.value_changes.empty()) {
    return;
  }

  std::unordered_map<std::uint16_t, std::uint32_t> old_values;
  old_values.reserve(updates.value_changes.size());
  for (const auto& change : updates.value_changes) {
    old_values.emplace(change.field_index, change.old_value);
  }

  std::vector<std::uint16_t> ordered_changed_fields;
  ordered_changed_fields.reserve(old_values.size());

  std::unordered_map<std::uint16_t, std::size_t> change_order;
  change_order.reserve(old_values.size());

  const auto append_changed_field = [&](const std::uint16_t field_index) {
    if (!old_values.contains(field_index) || change_order.contains(field_index)) {
      return;
    }
    change_order.emplace(field_index, ordered_changed_fields.size());
    ordered_changed_fields.push_back(field_index);
  };

  for (const auto field_index : updates.updated_fields) {
    append_changed_field(field_index);
  }
  for (const auto& change : updates.value_changes) {
    append_changed_field(change.field_index);
  }

  if (ordered_changed_fields.empty()) {
    return;
  }

  const auto entry_matches_object = [&](const Entry& entry) {
    const bool uses_section_matching =
        entry.match_kind == MatchKind::kTypeSection ||
        entry.match_kind == MatchKind::kGuidSection;
    if (uses_section_matching &&
        !IsSectionTypeCompatibleWithObject(entry.type_id, object.GetTypeId())) {
      return false;
    }

    if (entry.has_guid_filter) {
      return entry.guid == object.GetGuid();
    }
    if (uses_section_matching) {
      return true;
    }
    return entry.type_id == object.GetTypeId();
  };

  const auto entry_overlaps_field = [&](const Entry& entry,
                                        const std::uint16_t field_index) {
    return field_index >= entry.first_field &&
           field_index <
               static_cast<std::uint16_t>(entry.first_field + entry.field_count);
  };

  const auto byte_range_changed = [&](const Entry& entry,
                                      const std::vector<std::uint32_t>& old_words,
                                      const std::vector<std::uint32_t>& new_words) {
    const std::size_t byte_offset_in_words =
        static_cast<std::size_t>(entry.offset_bytes -
                                 entry.first_field * sizeof(std::uint32_t));
    const auto* old_bytes =
        reinterpret_cast<const std::uint8_t*>(old_words.data());
    const auto* new_bytes =
        reinterpret_cast<const std::uint8_t*>(new_words.data());
    for (std::size_t byte_index = 0; byte_index < entry.size_bytes; ++byte_index) {
      const std::size_t absolute_byte_index = byte_offset_in_words + byte_index;
      if (old_bytes[absolute_byte_index] != new_bytes[absolute_byte_index]) {
        return true;
      }
    }
    return false;
  };

  const std::size_t initial_entry_count = entries_.size();
  ++dispatch_depth_;

  for (std::size_t dispatch_step = 0;
       dispatch_step < ordered_changed_fields.size(); ++dispatch_step) {
    const std::uint16_t changed_field = ordered_changed_fields[dispatch_step];

    for (std::size_t index = 0; index < initial_entry_count; ++index) {
      auto& entry = entries_[index];
      if (entry.removed || !entry_matches_object(entry) ||
          !entry_overlaps_field(entry, changed_field)) {
        continue;
      }

      std::vector<std::uint32_t> old_words(entry.field_count);
      std::vector<std::uint32_t> new_words(entry.field_count);
      for (std::uint16_t word_index = 0; word_index < entry.field_count;
           ++word_index) {
        const auto field =
            static_cast<std::uint16_t>(entry.first_field + word_index);
        old_words[word_index] = GetWordValueForDispatchStep(
            object, field, dispatch_step, false, old_values, change_order);
        new_words[word_index] = GetWordValueForDispatchStep(
            object, field, dispatch_step, true, old_values, change_order);
      }

      if (!byte_range_changed(entry, old_words, new_words)) {
        continue;
      }

      DescriptorFieldChangeView view{
          .object = object,
          .guid = object.GetGuid(),
          .type_id = object.GetTypeId(),
          .field_index = entry.first_field,
          .offset_bytes = entry.offset_bytes,
          .size_bytes = entry.size_bytes,
          .is_create = is_create,
          .old_words = old_words,
          .new_words = new_words,
      };

      entry.dispatching = true;
      entry.callback(view);
      entry.dispatching = false;
    }
  }

  --dispatch_depth_;
  if (dispatch_depth_ == 0) {
    CompactRemoved();
  }
}

void DescriptorCallbackRegistry::Reset() {
  if (dispatch_depth_ == 0) {
    entries_.clear();
  } else {
    for (auto& entry : entries_) {
      entry.removed = true;
    }
  }
}

void DescriptorCallbackRegistry::CompactRemoved() {
  std::erase_if(entries_, [](const Entry& entry) { return entry.removed; });
}

}
