
#include "openwow/game/bnet_message_reflect.h"

#include <array>
#include <limits>
#include <string>
#include <vector>

namespace openwow::game {

namespace {

struct Utf8ValidationRange {
  std::uint8_t min_byte;
  std::uint8_t max_byte;
};

struct Utf8ValidationEntry {
  std::uint32_t sequence_length;
  std::array<Utf8ValidationRange, 4> ranges;
};

constexpr std::array<Utf8ValidationEntry, 9> kUtf8ValidationTable = {{
    {1, {{{0x00, 0x7F}, {0x00, 0x00}, {0x00, 0x00}, {0x00, 0x00}}}},
    {2, {{{0xC2, 0xDF}, {0x80, 0xBF}, {0x00, 0x00}, {0x00, 0x00}}}},
    {3, {{{0xE0, 0xE0}, {0xA0, 0xBF}, {0x80, 0xBF}, {0x00, 0x00}}}},
    {3, {{{0xE1, 0xEC}, {0x80, 0xBF}, {0x80, 0xBF}, {0x00, 0x00}}}},
    {3, {{{0xED, 0xED}, {0x80, 0x9F}, {0x80, 0xBF}, {0x00, 0x00}}}},
    {3, {{{0xEE, 0xEF}, {0x80, 0xBF}, {0x80, 0xBF}, {0x00, 0x00}}}},
    {4, {{{0xF0, 0xF0}, {0x90, 0xBF}, {0x80, 0xBF}, {0x80, 0xBF}}}},
    {4, {{{0xF1, 0xF3}, {0x80, 0xBF}, {0x80, 0xBF}, {0x80, 0xBF}}}},
    {4, {{{0xF4, 0xF4}, {0x80, 0x8F}, {0x80, 0xBF}, {0x80, 0xBF}}}},
}};

[[nodiscard]] bool BNetReflect_IntegerRangeContains(
    const BNetPackedIntegerRangeDescriptor& range,
    std::uint32_t value_low,
    std::uint32_t value_high) {
  if (range.unsigned_compare) {
    if (value_high < range.minimum.high) {
      return false;
    }
    if (value_high <= range.minimum.high && value_low < range.minimum.low) {
      return false;
    }
    if (value_high > range.maximum.high) {
      return false;
    }
    if (value_high >= range.maximum.high) {
      return value_low <= range.maximum.low;
    }
    return true;
  }

  const auto signed_high = static_cast<std::int32_t>(value_high);
  const auto signed_min_high = static_cast<std::int32_t>(range.minimum.high);
  const auto signed_max_high = static_cast<std::int32_t>(range.maximum.high);
  if (signed_high < signed_min_high) {
    return false;
  }
  if (signed_high <= signed_min_high && value_low < range.minimum.low) {
    return false;
  }
  if (signed_high > signed_max_high) {
    return false;
  }
  if (signed_high < signed_max_high) {
    return true;
  }
  return value_low <= range.maximum.low;
}

void BNetReflect_WriteLittleEndianIntegerBytes(
    void* destination,
    std::uint32_t value_low,
    std::uint32_t value_high,
    std::uint32_t byte_count) {
  auto* out = static_cast<std::uint8_t*>(destination);
  std::uint32_t low = value_low;
  std::uint32_t high = value_high;

  for (std::uint32_t index = 0; index < byte_count; ++index) {
    out[index] = static_cast<std::uint8_t>(low & 0xFFu);
    low = (low >> 8) | (high << 24);
    high >>= 8;
  }
}

[[nodiscard]] std::int64_t BNetReflect_ReadLittleEndianInteger64(
    const void* source,
    std::uint32_t byte_count,
    bool sign_extend) {
  if (byte_count == 0 || byte_count > sizeof(std::uint64_t) || !source) {
    return 0;
  }

  const auto* bytes = static_cast<const std::uint8_t*>(source);
  std::uint64_t result =
      sign_extend && bytes[byte_count - 1] >= 0x80u ? std::numeric_limits<std::uint64_t>::max()
                                                    : 0u;

  for (std::uint32_t remaining = byte_count; remaining != 0; --remaining) {
    result <<= 8u;
    result |= bytes[remaining - 1];
  }

  return static_cast<std::int64_t>(result);
}

[[nodiscard]] const BNetPackedIntegerFieldDescriptor* BNetReflect_GetPackedIntegerDescriptor(
    const BNetMessageNode* node) {
  if (!node) {
    return nullptr;
  }
  return static_cast<const BNetPackedIntegerFieldDescriptor*>(node->schema);
}

[[nodiscard]] const BNetSimpleFieldDescriptor* BNetReflect_GetSimpleDescriptor(
    const BNetMessageNode* node) {
  if (!node) {
    return nullptr;
  }
  return static_cast<const BNetSimpleFieldDescriptor*>(node->schema);
}

[[nodiscard]] const BNetCallbackFieldDescriptor* BNetReflect_GetCallbackDescriptor(
    const BNetMessageNode* node) {
  if (!node) {
    return nullptr;
  }
  return static_cast<const BNetCallbackFieldDescriptor*>(node->schema);
}

[[nodiscard]] const BNetStringFieldDescriptor* BNetReflect_GetStringDescriptor(
    const BNetMessageNode* node) {
  if (!node) {
    return nullptr;
  }
  return static_cast<const BNetStringFieldDescriptor*>(node->schema);
}

[[nodiscard]] const BNetBinaryFieldDescriptor* BNetReflect_GetBinaryDescriptor(
    const BNetMessageNode* node) {
  if (!node) {
    return nullptr;
  }
  return static_cast<const BNetBinaryFieldDescriptor*>(node->schema);
}

[[nodiscard]] const BNetEnumFieldDescriptor* BNetReflect_GetEnumDescriptor(
    const BNetMessageNode* node) {
  if (!node) {
    return nullptr;
  }
  return static_cast<const BNetEnumFieldDescriptor*>(node->schema);
}

[[nodiscard]] const BNetCompositeFieldDescriptor* BNetReflect_GetCompositeDescriptor(
    const BNetMessageNode* node) {
  if (!node) {
    return nullptr;
  }
  return static_cast<const BNetCompositeFieldDescriptor*>(node->schema);
}

[[nodiscard]] const BNetOptionalFieldDescriptor* BNetReflect_GetOptionalDescriptor(
    const BNetMessageNode* node) {
  if (!node) {
    return nullptr;
  }
  return static_cast<const BNetOptionalFieldDescriptor*>(node->schema);
}

[[nodiscard]] const BNetArrayFieldDescriptor* BNetReflect_GetArrayDescriptor(
    const BNetMessageNode* node) {
  if (!node) {
    return nullptr;
  }
  return static_cast<const BNetArrayFieldDescriptor*>(node->schema);
}

[[nodiscard]] const BNetReferenceFieldDescriptor* BNetReflect_GetReferenceDescriptor(
    const BNetMessageNode* node) {
  if (!node) {
    return nullptr;
  }
  return static_cast<const BNetReferenceFieldDescriptor*>(node->schema);
}

void BNetReflect_WriteResolvedNode(BNetMessageNode* result,
                                   const BNetReflectedNodeSpec& spec,
                                   void* data) {
  if (!result) {
    return;
  }

  result->schema = spec.schema;
  result->parent = spec.parent;
  result->wire_type = spec.wire_type;
  result->data = data;
}

[[nodiscard]] bool BNetReflect_ComposeNode(BNetMessageReflect& source_reflect,
                                           const BNetMessageNode* source_node,
                                           BNetMessageReflect& destination_reflect,
                                           const BNetMessageNode* destination_node) {
  if (!source_node || !destination_node) {
    return false;
  }

  switch (source_node->wire_type) {
  case BNetWireType::kArray: {
    const auto reported_count = source_reflect.GetArrayCount(source_node);
    if (reported_count < 0) {
      return false;
    }
    const auto count = static_cast<std::uint32_t>(reported_count);
    if (!destination_reflect.ResizeArray(destination_node, count)) {
      return false;
    }

    for (std::uint32_t index = 0; index < count; ++index) {
      BNetMessageNode source_element{};
      BNetMessageNode destination_element{};
      if (!source_reflect.GetArrayElement(source_node, index, &source_element) ||
          !destination_reflect.GetArrayElement(destination_node, index, &destination_element) ||
          !BNetReflect_ComposeNode(
              source_reflect, &source_element, destination_reflect, &destination_element)) {
        return false;
      }
    }

    return true;
  }

  case BNetWireType::kString:
  case BNetWireType::kWString: {
    const auto string_length = source_reflect.InvokeAccessor(source_node);
    const auto buffer_size =
        static_cast<std::size_t>(string_length > 0 ? string_length : 0) + 1u;
    std::string value(buffer_size, '\0');
    if (!source_reflect.GetString(source_node, value.data(), value.size())) {
      return false;
    }
    return destination_reflect.SetString(destination_node, value.c_str());
  }

  case BNetWireType::kBits: {
    const auto bit_count = source_reflect.GetBitCount(source_node);
    if (bit_count < 0) {
      return false;
    }

    std::vector<std::uint8_t> buffer(
        (static_cast<std::size_t>(bit_count) + 7u) >> 3u, 0u);
    if (!source_reflect.GetBitData(source_node, buffer.data(), static_cast<std::uint32_t>(bit_count))) {
      return false;
    }

    return destination_reflect.SetBitData(
        destination_node, buffer.data(), static_cast<std::uint32_t>(bit_count));
  }

  case BNetWireType::kBytes: {
    const auto byte_count = source_reflect.GetByteCount(source_node);
    if (byte_count < 0) {
      return false;
    }

    std::vector<std::uint8_t> buffer(static_cast<std::size_t>(byte_count), 0u);
    if (!source_reflect.GetByteData(source_node, buffer.data(), buffer.size())) {
      return false;
    }

    return destination_reflect.SetByteData(destination_node, buffer.data(), buffer.size());
  }

  case BNetWireType::kSimple:
    return destination_reflect.SetSimpleValue(
        destination_node, source_reflect.HasValue(source_node) ? 1u : 0u);

  case BNetWireType::kEnum: {
    std::array<char, 1024> field_name{};
    if (!source_reflect.GetEnumName(source_node, field_name.data(), field_name.size()) ||
        !destination_reflect.SetFieldByName(destination_node, 0, field_name.data())) {
      return false;
    }

    BNetMessageNode source_field{};
    BNetMessageNode destination_field{};
    if (!source_reflect.ResolveFieldByName(source_node, field_name.data(), &source_field) ||
        !destination_reflect.ResolveFieldByName(
            destination_node, field_name.data(), &destination_field)) {
      return false;
    }

    return BNetReflect_ComposeNode(
        source_reflect, &source_field, destination_reflect, &destination_field);
  }

  case BNetWireType::kMessage:
  case BNetWireType::kMessage2: {
    const auto value = static_cast<std::uint64_t>(source_reflect.GetInt64(source_node));
    return destination_reflect.SetKeyValue(
        destination_node,
        static_cast<std::uint32_t>(value & 0xFFFFFFFFu),
        static_cast<std::uint32_t>(value >> 32u));
  }

  case BNetWireType::kCallback:
    return destination_reflect.SetViaCallback(
        destination_node, source_reflect.InvokeCallback(source_node));

  case BNetWireType::kNoOp:
    return true;

  case BNetWireType::kOptional: {
    const bool is_present = source_reflect.HasOptional(source_node);
    if (!destination_reflect.SetOptional(destination_node, is_present ? 1 : 0)) {
      return false;
    }
    if (!is_present) {
      return true;
    }

    BNetMessageNode source_child{};
    BNetMessageNode destination_child{};
    if (!source_reflect.ResolveFieldByName(source_node, nullptr, &source_child) ||
        !destination_reflect.ResolveFieldByName(destination_node, nullptr, &destination_child)) {
      return false;
    }

    return BNetReflect_ComposeNode(
        source_reflect, &source_child, destination_reflect, &destination_child);
  }

  case BNetWireType::kFloat32:
  case BNetWireType::kFloat64:
    return destination_reflect.SetFloat(destination_node, source_reflect.GetFloat(source_node));

  case BNetWireType::kComposite: {
    const auto* descriptor = BNetReflect_GetCompositeDescriptor(source_node);
    if (!descriptor || !destination_reflect.IsComposite(destination_node)) {
      return false;
    }

    for (std::uint32_t index = 0; index < descriptor->field_count; ++index) {
      const auto& field = descriptor->fields[index];
      if (!field.name) {
        return false;
      }

      BNetMessageNode source_field{};
      BNetMessageNode destination_field{};
      if (!source_reflect.ResolveFieldByName(source_node, field.name, &source_field) ||
          !destination_reflect.ResolveFieldByName(
              destination_node, field.name, &destination_field) ||
          !BNetReflect_ComposeNode(
              source_reflect, &source_field, destination_reflect, &destination_field)) {
        return false;
      }
    }

    return true;
  }

  case BNetWireType::kReference: {
    const auto* descriptor = BNetReflect_GetReferenceDescriptor(source_node);
    if (!descriptor) {
      return false;
    }

    BNetMessageNode source_child{};
    BNetReflect_WriteResolvedNode(&source_child, descriptor->child, source_node->data);
    return BNetReflect_ComposeNode(
        source_reflect, &source_child, destination_reflect, destination_node);
  }
  }

  return false;
}

[[nodiscard]] const BNetEnumFieldEntry* BNetReflect_FindEnumFieldByName(
    const BNetEnumFieldDescriptor* descriptor,
    const char* name) {
  if (!descriptor || !name) {
    return nullptr;
  }

  for (std::uint32_t index = 0; index < descriptor->field_count; ++index) {
    const auto& field = descriptor->fields[index];
    if (field.name && std::strcmp(field.name, name) == 0) {
      return &field;
    }
  }

  return nullptr;
}

[[nodiscard]] const BNetEnumFieldEntry* BNetReflect_FindEnumFieldByValue(
    const BNetEnumFieldDescriptor* descriptor,
    std::uint32_t value_low,
    std::uint32_t value_high) {
  if (!descriptor) {
    return nullptr;
  }

  for (std::uint32_t index = 0; index < descriptor->field_count; ++index) {
    const auto& field = descriptor->fields[index];
    if (field.value.low == value_low && field.value.high == value_high) {
      return &field;
    }
  }

  return nullptr;
}

[[nodiscard]] BNetPackedInteger64 BNetReflect_AddPackedIntegerOffset(
    const BNetPackedInteger64& value,
    std::uint32_t offset) {
  const std::uint64_t composed_value =
      (static_cast<std::uint64_t>(value.high) << 32u) | value.low;
  const std::uint64_t result = composed_value + offset;

  return {
      .low = static_cast<std::uint32_t>(result & 0xFFFFFFFFu),
      .high = static_cast<std::uint32_t>(result >> 32u),
  };
}

[[nodiscard]] bool BNetReflect_FindPackedIntegerIndex(
    const BNetPackedIntegerFieldDescriptor* descriptor,
    std::uint32_t value_low,
    std::uint32_t value_high,
    std::uint32_t* out_index) {
  if (out_index) {
    *out_index = 0;
  }
  if (!descriptor) {
    return false;
  }

  if (descriptor->explicit_value_count == 0) {
    if (!BNetReflect_IntegerRangeContains(descriptor->range, value_low, value_high)) {
      return false;
    }

    if (out_index) {
      *out_index = value_low - descriptor->range.minimum.low;
    }
    return true;
  }

  if (!descriptor->explicit_values) {
    return false;
  }

  for (std::uint32_t index = 0; index < descriptor->explicit_value_count; ++index) {
    const auto& candidate = descriptor->explicit_values[index];
    if (candidate.low == value_low && candidate.high == value_high) {
      if (out_index) {
        *out_index = index;
      }
      return true;
    }
  }

  return false;
}

[[nodiscard]] const BNetCompositeFieldEntry* BNetReflect_FindCompositeField(
    const BNetCompositeFieldDescriptor* descriptor,
    const char* name) {
  if (!descriptor || !name) {
    return nullptr;
  }

  for (std::uint32_t index = 0; index < descriptor->field_count; ++index) {
    const auto& field = descriptor->fields[index];
    if (field.name && std::strcmp(field.name, name) == 0) {
      return &field;
    }
  }

  return nullptr;
}

[[nodiscard]] bool BNetReflect_IsAccessorStringType(const BNetMessageNode* node) {
  if (!node) {
    return false;
  }

  return node->wire_type == BNetWireType::kString ||
         node->wire_type == BNetWireType::kWString;
}

[[nodiscard]] std::int32_t BNetReflect_GetBinaryCount(const BNetMessageNode* node,
                                                      BNetWireType expected_type) {
  const auto* descriptor = BNetReflect_GetBinaryDescriptor(node);
  if (!descriptor || !node || node->wire_type != expected_type || !descriptor->count_getter) {
    return 0;
  }

  return descriptor->count_getter(node->data);
}

[[nodiscard]] bool BNetReflect_CopyBinaryData(const BNetMessageNode* node,
                                              BNetWireType expected_type,
                                              void* buffer,
                                              std::size_t max_count,
                                              bool count_is_bits) {
  const auto* descriptor = BNetReflect_GetBinaryDescriptor(node);
  if (!descriptor || !node || node->wire_type != expected_type ||
      !descriptor->count_getter || !descriptor->data_getter) {
    return false;
  }

  const auto reported_count = descriptor->count_getter(node->data);
  if (reported_count < 0) {
    return false;
  }

  auto copy_count = static_cast<std::size_t>(reported_count);
  if (copy_count > max_count) {
    copy_count = max_count;
  }

  const auto* source = descriptor->data_getter(node->data);
  const auto bytes_to_copy =
      count_is_bits ? ((copy_count >> 3u) + ((copy_count & 7u) != 0u)) : copy_count;
  if (bytes_to_copy != 0) {
    if (!buffer || !source) {
      return false;
    }
    std::memcpy(buffer, source, bytes_to_copy);
  }
  return true;
}

[[nodiscard]] bool BNetReflect_SetBinaryData(const BNetMessageNode* node,
                                             BNetWireType expected_type,
                                             const void* data,
                                             std::uint32_t count,
                                             bool count_is_bits) {
  const auto* descriptor = BNetReflect_GetBinaryDescriptor(node);
  if (!descriptor || !node || node->wire_type != expected_type ||
      !descriptor->mutable_data_getter) {
    return false;
  }

  if (!BNetReflect_IntegerRangeContains(descriptor->count_range, count, 0)) {
    return false;
  }

  void* destination = descriptor->mutable_data_getter(node->data, count);
  const auto bytes_to_copy = count_is_bits
                                 ? (static_cast<std::size_t>(count >> 3u) +
                                    ((count & 7u) != 0u))
                                 : static_cast<std::size_t>(count);
  if (bytes_to_copy != 0) {
    if (!destination || !data) {
      return false;
    }
    std::memcpy(destination, data, bytes_to_copy);
  }
  return true;
}

[[nodiscard]] bool BNetReflect_SetStringData(const BNetMessageNode* node,
                                             const char* value) {
  const auto* descriptor = BNetReflect_GetStringDescriptor(node);
  if (!descriptor || !node || !value || !BNetReflect_IsAccessorStringType(node) ||
      !descriptor->mutable_data_getter) {
    return false;
  }

  const auto length = std::strlen(value);
  if (length > std::numeric_limits<std::uint32_t>::max()) {
    return false;
  }
  const auto byte_count = static_cast<std::uint32_t>(length);
  auto* destination =
      static_cast<char*>(descriptor->mutable_data_getter(node->data, byte_count));
  if (!destination) {
    return false;
  }
  if (byte_count != 0) {
    std::memcpy(destination, value, byte_count);
  }
  destination[byte_count] = '\0';

  if (node->wire_type != BNetWireType::kWString) {
    return true;
  }

  if (!descriptor->codepoint_count_setter) {
    return false;
  }

  std::uint32_t codepoint_count = 0;
  if (!BNetReflect_ValidateUTF8(byte_count, destination, &codepoint_count)) {
    return false;
  }

  descriptor->codepoint_count_setter(node->data, codepoint_count);
  return true;
}

}

BNetMessageNode* BNetMessageReflect::Initialize(BNetMessageNode* node) {
  if (!node) return nullptr;
  node->schema = nullptr;
  node->parent = nullptr;
  node->data = node;
  return node;
}

std::int64_t BNetMessageReflect::GetInt64(const BNetMessageNode* node) {
  const auto* descriptor = BNetReflect_GetPackedIntegerDescriptor(node);
  if (!descriptor || !node) {
    return 0;
  }

  switch (node->wire_type) {
  case BNetWireType::kMessage:
  case BNetWireType::kMessage2:
    break;
  default:
    return 0;
  }

  if (descriptor->storage_size != 0 && !node->data) {
    return 0;
  }

  return BNetReflect_ReadLittleEndianInteger64(
      node->data, descriptor->storage_size, !descriptor->range.unsigned_compare);
}

std::int64_t BNetMessageReflect::GetInt64Indirect(
    const BNetMessageNode* node) {
  return GetInt64(node);
}

bool BNetMessageReflect::HasValue(const BNetMessageNode* node) {
  const auto* descriptor = BNetReflect_GetSimpleDescriptor(node);
  if (!descriptor || !node || node->wire_type != BNetWireType::kSimple) {
    return false;
  }

  if (descriptor->storage_size != 0 && !node->data) {
    return false;
  }

  return BNetReflect_ReadLittleEndianInteger64(node->data, descriptor->storage_size, false) != 0;
}

double BNetMessageReflect::GetFloat(const BNetMessageNode* node) {
  if (!node || !node->data) {
    return 0.0;
  }

  switch (node->wire_type) {
  case BNetWireType::kFloat32:
    return static_cast<double>(*static_cast<const float*>(node->data));
  case BNetWireType::kFloat64:
    return *static_cast<const double*>(node->data);
  default:
    return 0.0;
  }
}

std::int32_t BNetMessageReflect::InvokeCallback(const BNetMessageNode* node) {
  if (!node) {
    return 0;
  }

  const auto* descriptor = BNetReflect_GetCallbackDescriptor(node);
  if (!descriptor || node->wire_type != BNetWireType::kCallback || !descriptor->getter) {
    return 0;
  }

  return descriptor->getter(node->data);
}

std::int32_t BNetMessageReflect::InvokeAccessor(const BNetMessageNode* node) {
  if (!BNetReflect_IsAccessorStringType(node)) {
    return 0;
  }

  const auto* descriptor = BNetReflect_GetStringDescriptor(node);
  if (!descriptor || !descriptor->length_getter) {
    return 0;
  }

  return descriptor->length_getter(node->data);
}

bool BNetMessageReflect::GetString(const BNetMessageNode* node,
                                   char* buffer,
                                   std::size_t buffer_size) {
  if (!buffer || buffer_size == 0) {
    return false;
  }

  if (!BNetReflect_IsAccessorStringType(node)) {
    buffer[0] = '\0';
    return false;
  }

  const auto* descriptor = BNetReflect_GetStringDescriptor(node);
  if (!descriptor || !descriptor->length_getter || !descriptor->data_getter) {
    buffer[0] = '\0';
    return false;
  }

  const auto reported_length = descriptor->length_getter(node->data);
  if (reported_length < 0) {
    buffer[0] = '\0';
    return false;
  }

  auto byte_count = static_cast<std::size_t>(reported_length);
  if (byte_count >= buffer_size) {
    byte_count = buffer_size - 1;
  }

  const auto* source = descriptor->data_getter(node->data);
  if (byte_count != 0) {
    if (!source) {
      buffer[0] = '\0';
      return false;
    }
    std::memcpy(buffer, source, byte_count);
  }
  buffer[byte_count] = '\0';
  return true;
}

std::int32_t BNetMessageReflect::GetBitCount(const BNetMessageNode* node) {
  return BNetReflect_GetBinaryCount(node, BNetWireType::kBits);
}

bool BNetMessageReflect::GetBitData(const BNetMessageNode* node,
                                    void* buffer,
                                    std::uint32_t max_bits) {
  return BNetReflect_CopyBinaryData(node, BNetWireType::kBits, buffer, max_bits, true);
}

std::int32_t BNetMessageReflect::GetByteCount(const BNetMessageNode* node) {
  return BNetReflect_GetBinaryCount(node, BNetWireType::kBytes);
}

bool BNetMessageReflect::GetByteData(const BNetMessageNode* node,
                                     void* buffer,
                                     std::size_t max_bytes) {
  return BNetReflect_CopyBinaryData(node, BNetWireType::kBytes, buffer, max_bytes, false);
}

bool BNetMessageReflect::ResolveFieldByName(const BNetMessageNode* node,
                                            const char* field_name,
                                            BNetMessageNode* result) {
  if (result) {
    result->schema = node ? node->schema : nullptr;
  }
  if (!node || !result) {
    return false;
  }

  switch (node->wire_type) {
  case BNetWireType::kEnum: {
    const auto* descriptor = BNetReflect_GetEnumDescriptor(node);
    if (!descriptor || !descriptor->data_getter) {
      return false;
    }

    const auto* field = BNetReflect_FindEnumFieldByName(descriptor, field_name);
    if (!field) {
      return false;
    }

    BNetReflect_WriteResolvedNode(result, field->node, descriptor->data_getter(node->data));
    return true;
  }

  case BNetWireType::kOptional: {
    const auto* descriptor = BNetReflect_GetOptionalDescriptor(node);
    if (!descriptor || !descriptor->presence_getter || !descriptor->data_getter ||
        !descriptor->presence_getter(node->data)) {
      return false;
    }

    BNetReflect_WriteResolvedNode(result, descriptor->child, descriptor->data_getter(node->data));
    return true;
  }

  case BNetWireType::kComposite: {
    const auto* descriptor = BNetReflect_GetCompositeDescriptor(node);
    const auto* field = BNetReflect_FindCompositeField(descriptor, field_name);
    if (!field) {
      return false;
    }

    auto* base = static_cast<char*>(node->data);
    BNetReflect_WriteResolvedNode(result, field->node, base ? base + field->byte_offset : nullptr);
    return true;
  }

  default:
    return false;
  }
}

bool BNetMessageReflect::GetArrayElement(const BNetMessageNode* node,
                                         std::uint32_t index,
                                         BNetMessageNode* result) {
  if (result) {
    result->schema = node ? node->schema : nullptr;
  }
  if (!node || !result) {
    return false;
  }

  const auto* descriptor = BNetReflect_GetArrayDescriptor(node);
  if (!descriptor || node->wire_type != BNetWireType::kArray || !descriptor->count_getter ||
      !descriptor->data_getter) {
    return false;
  }

  const auto count = descriptor->count_getter(node->data);
  if (count < 0 || index >= static_cast<std::uint32_t>(count)) {
    return false;
  }

  auto* base = static_cast<char*>(descriptor->data_getter(node->data));
  if (!base && descriptor->element_size != 0) {
    return false;
  }

  if (descriptor->element_size != 0 &&
      index > std::numeric_limits<std::size_t>::max() / descriptor->element_size) {
    return false;
  }
  const auto byte_offset = static_cast<std::size_t>(index) * descriptor->element_size;
  BNetReflect_WriteResolvedNode(
      result, descriptor->element, base ? base + byte_offset : nullptr);
  return true;
}

bool BNetMessageReflect::GetEnumName(const BNetMessageNode* node,
                                     char* buffer,
                                     std::size_t buffer_size) {
  if (buffer && buffer_size > 0) {
    buffer[0] = '\0';
  }
  if (!node || !buffer || buffer_size == 0) {
    return false;
  }

  const auto* descriptor = BNetReflect_GetEnumDescriptor(node);
  if (!descriptor || node->wire_type != BNetWireType::kEnum || !descriptor->value_getter) {
    return false;
  }

  const auto value = descriptor->value_getter(node->data);
  const std::uint32_t value_low = static_cast<std::uint32_t>(value);
  const std::uint32_t value_high = value < 0 ? 0xFFFFFFFFu : 0u;
  const auto* field = BNetReflect_FindEnumFieldByValue(descriptor, value_low, value_high);
  if (!field || !field->name) {
    return false;
  }

  auto byte_count = std::strlen(field->name);
  if (byte_count >= buffer_size) {
    byte_count = buffer_size - 1;
  }

  std::memcpy(buffer, field->name, byte_count);
  buffer[byte_count] = '\0';
  return true;
}

std::int32_t BNetMessageReflect::GetArrayCount(
    const BNetMessageNode* node) {
  const auto* descriptor = BNetReflect_GetArrayDescriptor(node);
  if (!descriptor || !node || node->wire_type != BNetWireType::kArray ||
      !descriptor->count_getter) {
    return 0;
  }

  return descriptor->count_getter(node->data);
}

bool BNetMessageReflect::HasOptional(const BNetMessageNode* node) {
  const auto* descriptor = BNetReflect_GetOptionalDescriptor(node);
  if (!descriptor || !node || node->wire_type != BNetWireType::kOptional ||
      !descriptor->presence_getter) {
    return false;
  }

  return descriptor->presence_getter(node->data);
}

bool BNetMessageReflect::SetKeyValue(const BNetMessageNode* node,
                                     std::uint32_t key_lo,
                                     std::uint32_t key_hi) {
  const auto* descriptor = BNetReflect_GetPackedIntegerDescriptor(node);
  if (!descriptor || !node) {
    return false;
  }

  bool accepted = false;
  switch (node->wire_type) {
  case BNetWireType::kMessage2:
    accepted = BNetReflect_IntegerRangeContains(descriptor->range, key_lo, key_hi);
    break;

  case BNetWireType::kMessage:
    accepted = BNetReflect_FindKeyByValueLinear(
        descriptor, key_lo, key_hi, nullptr);
    if (!accepted && descriptor->range.allow_range_fallback) {
      accepted = BNetReflect_IntegerRangeContains(descriptor->range, key_lo, key_hi);
    }
    break;

  default:
    return false;
  }

  if (!accepted) {
    return false;
  }

  if (descriptor->storage_size != 0 && !node->data) {
    return false;
  }
  if (descriptor->storage_size > sizeof(std::uint64_t)) {
    return false;
  }

  BNetReflect_WriteLittleEndianIntegerBytes(
      node->data, key_lo, key_hi, descriptor->storage_size);
  return true;
}

bool BNetMessageReflect::SetKeyValueIndirect(const BNetMessageNode* node,
                                             std::uint32_t key_lo,
                                             std::uint32_t key_hi) {
  return SetKeyValue(node, key_lo, key_hi);
}

bool BNetMessageReflect::SetSimpleValue(const BNetMessageNode* node,
                                        std::uint8_t value) {
  const auto* descriptor = BNetReflect_GetSimpleDescriptor(node);
  if (!descriptor || !node || node->wire_type != BNetWireType::kSimple) {
    return false;
  }
  if (descriptor->storage_size != 0 && !node->data) {
    return false;
  }
  if (descriptor->storage_size > sizeof(std::uint64_t)) {
    return false;
  }

  BNetReflect_WriteLittleEndianIntegerBytes(
      node->data, value, 0, descriptor->storage_size);
  return true;
}

bool BNetMessageReflect::SetFloat(const BNetMessageNode* node, double value) {
  if (!node || !node->data) {
    return false;
  }

  switch (node->wire_type) {
  case BNetWireType::kFloat32:
    *static_cast<float*>(node->data) = static_cast<float>(value);
    return true;
  case BNetWireType::kFloat64:
    *static_cast<double*>(node->data) = value;
    return true;
  default:
    return false;
  }
}

bool BNetMessageReflect::SetViaCallback(const BNetMessageNode* node,
                                        std::int32_t value) {
  if (!node) {
    return false;
  }

  const auto* descriptor = BNetReflect_GetCallbackDescriptor(node);
  if (!descriptor || node->wire_type != BNetWireType::kCallback || !descriptor->setter) {
    return false;
  }

  descriptor->setter(node->data, value);
  return true;
}

bool BNetMessageReflect::SetString(const BNetMessageNode* node,
                                   const char* value) {
  return BNetReflect_SetStringData(node, value);
}

bool BNetMessageReflect::SetBitData(const BNetMessageNode* node,
                                    const void* data,
                                    std::uint32_t bit_count) {
  return BNetReflect_SetBinaryData(node, BNetWireType::kBits, data, bit_count, true);
}

bool BNetMessageReflect::SetByteData(const BNetMessageNode* node,
                                     const void* data,
                                     std::size_t byte_count) {
  if (byte_count > std::numeric_limits<std::uint32_t>::max()) {
    return false;
  }

  return BNetReflect_SetBinaryData(
      node, BNetWireType::kBytes, data, static_cast<std::uint32_t>(byte_count), false);
}

bool BNetMessageReflect::IsComposite(const BNetMessageNode* node) {
  if (!node) return false;
  return node->wire_type == BNetWireType::kComposite;
}

bool BNetMessageReflect::SetFieldByName(const BNetMessageNode* node,
                                        std::int32_t ,
                                        const char* name) {
  const auto* descriptor = BNetReflect_GetEnumDescriptor(node);
  if (!descriptor || !node || node->wire_type != BNetWireType::kEnum || !name ||
      !descriptor->value_setter) {
    return false;
  }

  const auto* field = BNetReflect_FindEnumFieldByName(descriptor, name);
  if (!field) {
    return false;
  }

  descriptor->value_setter(node->data, field->value.low);
  return true;
}

bool BNetMessageReflect::ResizeArray(const BNetMessageNode* node,
                                     std::uint32_t new_size) {
  const auto* descriptor = BNetReflect_GetArrayDescriptor(node);
  if (!descriptor || !node || node->wire_type != BNetWireType::kArray ||
      !descriptor->resize_setter) {
    return false;
  }

  if (!BNetReflect_IntegerRangeContains(descriptor->count_range, new_size, 0)) {
    return false;
  }

  descriptor->resize_setter(node->data, new_size);
  return true;
}

bool BNetMessageReflect::SetOptional(const BNetMessageNode* node,
                                     std::int32_t value) {
  const auto* descriptor = BNetReflect_GetOptionalDescriptor(node);
  if (!descriptor || !node || node->wire_type != BNetWireType::kOptional ||
      !descriptor->setter) {
    return false;
  }

  descriptor->setter(node->data, value);
  return true;
}

bool BNetMessageReflect::ComposeForward(const BNetMessageNode* source_node,
                                        BNetMessageReflect& destination_reflect,
                                        const BNetMessageNode* destination_node) {
  return BNetReflect_ComposeNode(*this, source_node, destination_reflect, destination_node);
}

bool BNetMessageReflect::ComposeReverse(BNetMessageReflect& source_reflect,
                                        const BNetMessageNode* source_node,
                                        const BNetMessageNode* destination_node) {
  return BNetReflect_ComposeNode(source_reflect, source_node, *this, destination_node);
}

bool BNetReflect_FindKeyByValue(const void* schema,
                                std::uint32_t val_lo,
                                std::uint32_t val_hi,
                                std::uint32_t* out_index) {
  return BNetReflect_FindPackedIntegerIndex(
      static_cast<const BNetPackedIntegerFieldDescriptor*>(schema),
      val_lo,
      val_hi,
      out_index);
}

bool BNetReflect_IndexToKeyPair(const void* schema,
                                std::uint32_t index,
                                std::uint32_t* out_pair) {
  const auto* descriptor = static_cast<const BNetPackedIntegerFieldDescriptor*>(schema);
  if (!descriptor) {
    return false;
  }

  BNetPackedInteger64 resolved_pair{};
  if (descriptor->explicit_value_count == 0) {
    resolved_pair = BNetReflect_AddPackedIntegerOffset(descriptor->range.minimum, index);
    if (out_pair) {
      out_pair[0] = resolved_pair.low;
      out_pair[1] = resolved_pair.high;
    }
    return BNetReflect_IntegerRangeContains(
        descriptor->range, resolved_pair.low, resolved_pair.high);
  }

  if (!descriptor->explicit_values) {
    return false;
  }
  if (index >= descriptor->explicit_value_count) {
    return false;
  }

  resolved_pair = descriptor->explicit_values[index];

  if (out_pair) {
    out_pair[0] = resolved_pair.low;
    out_pair[1] = resolved_pair.high;
  }
  return true;
}

bool BNetReflect_FindKeyByValueLinear(const void* schema,
                                      std::uint32_t val_lo,
                                      std::uint32_t val_hi,
                                      std::uint32_t* out_index) {
  return BNetReflect_FindPackedIntegerIndex(
      static_cast<const BNetPackedIntegerFieldDescriptor*>(schema),
      val_lo,
      val_hi,
      out_index);
}

bool BNetReflect_ValidateUTF8(std::uint32_t byte_count, const char* data,
                              std::uint32_t* out_char_count) {
  if (out_char_count) *out_char_count = 0;
  if (!data || byte_count == 0) return true;

  std::uint32_t validated_codepoints = 0;
  std::uint32_t offset = 0;

  while (offset < byte_count) {
    const auto lead = static_cast<std::uint8_t>(data[offset]);
    const Utf8ValidationEntry* matched_entry = nullptr;

    for (const auto& entry : kUtf8ValidationTable) {
      const auto& lead_range = entry.ranges[0];
      if (lead >= lead_range.min_byte && lead <= lead_range.max_byte) {
        matched_entry = &entry;
        break;
      }
    }

    if (!matched_entry) {
      return false;
    }

    if (byte_count - offset < matched_entry->sequence_length) {
      return false;
    }

    for (std::uint32_t byte_index = 1; byte_index < matched_entry->sequence_length; ++byte_index) {
      const auto value = static_cast<std::uint8_t>(data[offset + byte_index]);
      const auto& range = matched_entry->ranges[byte_index];
      if (value < range.min_byte || value > range.max_byte) {
        return false;
      }
    }

    offset += matched_entry->sequence_length;
    ++validated_codepoints;
  }

  if (out_char_count) *out_char_count = validated_codepoints;
  return true;
}

}
