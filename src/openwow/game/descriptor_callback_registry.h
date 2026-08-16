#pragma once

#include "openwow/game/object_guid.h"
#include "openwow/game/object_types.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <span>
#include <vector>

namespace openwow::game {

class CGObject_C;

struct FieldValueChange {
  std::uint16_t field_index{0};
  std::uint32_t old_value{0};
  std::uint32_t new_value{0};
};

struct FieldUpdateBatch {
  std::vector<std::uint16_t> updated_fields;
  std::vector<FieldValueChange> value_changes;
};

struct DescriptorFieldChangeView {
  const CGObject_C& object;
  ObjectGuid guid;
  TypeID type_id{TypeID::kObject};
  std::uint16_t field_index{0};
  std::uint16_t offset_bytes{0};
  std::uint16_t size_bytes{0};
  bool is_create{false};
  std::span<const std::uint32_t> old_words;
  std::span<const std::uint32_t> new_words;
};

using DescriptorCallback = std::function<void(const DescriptorFieldChangeView&)>;

struct DescriptorCallbackBinding {
  std::uintptr_t callback_key{0};
  std::uintptr_t callback_context{0};

  [[nodiscard]] bool operator==(const DescriptorCallbackBinding&) const = default;
};

class DescriptorCallbackRegistry {
 public:
  struct Handle {
    std::uint64_t value{0};

    [[nodiscard]] explicit operator bool() const { return value != 0; }
    [[nodiscard]] bool operator==(const Handle&) const = default;
  };

  static DescriptorCallbackRegistry& Get();

  [[nodiscard]] Handle RegisterTypeCallback(TypeID type_id,
                                            std::uint16_t offset_bytes,
                                            std::uint16_t size_bytes,
                                            DescriptorCallback callback,
                                            DescriptorCallbackBinding binding = {});

  [[nodiscard]] Handle RegisterTypeSectionCallback(TypeID section_type,
                                                   std::uint16_t relative_offset_bytes,
                                                   std::uint16_t size_bytes,
                                                   DescriptorCallback callback,
                                                   DescriptorCallbackBinding binding = {});
  [[nodiscard]] Handle RegisterObjectCallback(ObjectGuid guid,
                                              std::uint16_t offset_bytes,
                                              std::uint16_t size_bytes,
                                              DescriptorCallback callback,
                                              DescriptorCallbackBinding binding = {});
  [[nodiscard]] Handle RegisterObjectSectionCallback(
      ObjectGuid guid,
      TypeID section_type,
      std::uint16_t relative_offset_bytes,
      std::uint16_t size_bytes,
      DescriptorCallback callback,
      DescriptorCallbackBinding binding = {});

  bool Unregister(Handle handle);
  std::size_t UnregisterObjectCallbacks(ObjectGuid guid);
  bool UnregisterTypeCallback(TypeID type_id,
                              std::uint16_t offset_bytes,
                              DescriptorCallbackBinding binding);
  bool UnregisterTypeSectionCallback(TypeID section_type,
                                     std::uint16_t relative_offset_bytes,
                                     DescriptorCallbackBinding binding);
  bool UnregisterObjectSectionCallback(ObjectGuid guid,
                                       TypeID section_type,
                                       std::uint16_t relative_offset_bytes,
                                       DescriptorCallbackBinding binding);
  void Dispatch(const CGObject_C& object,
                const FieldUpdateBatch& updates,
                bool is_create);
  void Reset();

 private:
  DescriptorCallbackRegistry() = default;

  enum class MatchKind : std::uint8_t {
    kTypeAbsolute,
    kTypeSection,
    kGuidAbsolute,
    kGuidSection,
  };

  struct Entry {
    Handle handle{};
    TypeID type_id{TypeID::kObject};
    ObjectGuid guid{};
    MatchKind match_kind{MatchKind::kTypeAbsolute};
    bool has_guid_filter{false};
    std::uint16_t first_field{0};
    std::uint16_t field_count{0};
    std::uint16_t offset_bytes{0};
    std::uint16_t size_bytes{0};
    DescriptorCallbackBinding binding{};
    DescriptorCallback callback;
    bool dispatching{false};
    bool removed{false};
  };

  void CompactRemoved();
  [[nodiscard]] Handle RegisterCommon(MatchKind match_kind,
                                      TypeID type_id,
                                      ObjectGuid guid,
                                      bool has_guid_filter,
                                      std::uint16_t offset_bytes,
                                      std::uint16_t size_bytes,
                                      DescriptorCallback callback,
                                      DescriptorCallbackBinding binding);

  std::deque<Entry> entries_;
  std::uint64_t next_handle_{1};
  std::size_t dispatch_depth_{0};
};

}
