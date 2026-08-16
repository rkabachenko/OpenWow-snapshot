
#pragma once

#include <cstdint>
#include <cstring>

namespace openwow::game {

enum class BNetWireType : std::int32_t {
  kArray     = 1,
  kString    = 2,
  kBits      = 3,
  kBytes     = 4,
  kSimple    = 5,
  kEnum      = 6,
  kMessage   = 7,
  kCallback  = 8,
  kMessage2  = 9,
  kNoOp      = 10,
  kOptional  = 11,
  kFloat32   = 12,
  kFloat64   = 13,
  kComposite = 14,
  kWString   = 15,
  kReference = 16,
};

struct BNetMessageNode {
  const void* schema;
  const void* parent;
  BNetWireType wire_type;
  void* data;
};

static_assert(sizeof(BNetMessageNode) >= 16, "BNetMessageNode: minimum size check");

struct BNetPackedInteger64 {
  std::uint32_t low = 0;
  std::uint32_t high = 0;
};

struct BNetPackedIntegerRangeDescriptor {
  bool unsigned_compare = false;
  bool allow_range_fallback = false;
  BNetPackedInteger64 minimum{};
  BNetPackedInteger64 maximum{};
};

struct BNetPackedIntegerFieldDescriptor {
  std::uint32_t storage_size = 0;
  BNetPackedIntegerRangeDescriptor range{};
  const BNetPackedInteger64* explicit_values = nullptr;
  std::uint32_t explicit_value_count = 0;
};

struct BNetSimpleFieldDescriptor {
  std::uint32_t storage_size = 0;
};

struct BNetCallbackFieldDescriptor {
  using Getter = std::int32_t (*)(void* data);
  using Setter = void (*)(void* data, std::int32_t value);

  Getter getter = nullptr;
  Setter setter = nullptr;
};

struct BNetStringFieldDescriptor {
  using LengthGetter = std::int32_t (*)(void* data);
  using DataGetter = const void* (*)(void* data);
  using MutableDataGetter = void* (*)(void* data, std::uint32_t byte_count);
  using CodepointCountSetter = void (*)(void* data, std::uint32_t codepoint_count);

  LengthGetter length_getter = nullptr;
  DataGetter data_getter = nullptr;
  MutableDataGetter mutable_data_getter = nullptr;
  CodepointCountSetter codepoint_count_setter = nullptr;
};

struct BNetBinaryFieldDescriptor {
  using CountGetter = std::int32_t (*)(void* data);
  using DataGetter = const void* (*)(void* data);
  using MutableDataGetter = void* (*)(void* data, std::uint32_t count);

  BNetPackedIntegerRangeDescriptor count_range{};
  CountGetter count_getter = nullptr;
  DataGetter data_getter = nullptr;
  MutableDataGetter mutable_data_getter = nullptr;
};

struct BNetReflectedNodeSpec {
  const void* schema = nullptr;
  const void* parent = nullptr;
  BNetWireType wire_type = BNetWireType::kSimple;
};

struct BNetEnumFieldEntry {
  const char* name = nullptr;
  BNetPackedInteger64 value{};
  BNetReflectedNodeSpec node{};
};

struct BNetEnumFieldDescriptor {
  using ValueGetter = std::int32_t (*)(void* data);
  using DataGetter = void* (*)(void* data);
  using ValueSetter = void (*)(void* data, std::uint32_t value_low);

  ValueGetter value_getter = nullptr;
  DataGetter data_getter = nullptr;
  ValueSetter value_setter = nullptr;
  const BNetEnumFieldEntry* fields = nullptr;
  std::uint32_t field_count = 0;
};

struct BNetCompositeFieldEntry {
  const char* name = nullptr;
  std::uint32_t byte_offset = 0;
  BNetReflectedNodeSpec node{};
};

struct BNetCompositeFieldDescriptor {
  const BNetCompositeFieldEntry* fields = nullptr;
  std::uint32_t field_count = 0;
};

struct BNetOptionalFieldDescriptor {
  using PresenceGetter = bool (*)(void* data);
  using DataGetter = void* (*)(void* data);
  using Setter = void (*)(void* data, std::int32_t value);

  PresenceGetter presence_getter = nullptr;
  DataGetter data_getter = nullptr;
  Setter setter = nullptr;
  BNetReflectedNodeSpec child{};
};

struct BNetArrayFieldDescriptor {
  using CountGetter = std::int32_t (*)(void* data);
  using DataGetter = void* (*)(void* data);
  using ResizeSetter = void (*)(void* data, std::uint32_t new_size);

  std::uint32_t element_size = 0;
  CountGetter count_getter = nullptr;
  DataGetter data_getter = nullptr;
  BNetReflectedNodeSpec element{};
  BNetPackedIntegerRangeDescriptor count_range{};
  ResizeSetter resize_setter = nullptr;
};

struct BNetReferenceFieldDescriptor {
  BNetReflectedNodeSpec child{};
};

class BNetMessageReflect {
 public:
  virtual ~BNetMessageReflect() = default;

  virtual BNetMessageNode* Initialize(BNetMessageNode* node);

  virtual std::int64_t GetInt64(const BNetMessageNode* node);

  virtual std::int64_t GetInt64Indirect(const BNetMessageNode* node);

  virtual bool HasValue(const BNetMessageNode* node);

  virtual double GetFloat(const BNetMessageNode* node);

  virtual std::int32_t InvokeCallback(const BNetMessageNode* node);

  virtual std::int32_t InvokeAccessor(const BNetMessageNode* node);

  virtual bool GetString(const BNetMessageNode* node, char* buffer,
                         std::size_t buffer_size);

  virtual std::int32_t GetBitCount(const BNetMessageNode* node);

  virtual bool GetBitData(const BNetMessageNode* node, void* buffer,
                          std::uint32_t max_bits);

  virtual std::int32_t GetByteCount(const BNetMessageNode* node);

  virtual bool GetByteData(const BNetMessageNode* node, void* buffer,
                           std::size_t max_bytes);

  virtual bool ResolveFieldByName(const BNetMessageNode* node,
                                  const char* field_name,
                                  BNetMessageNode* result);

  virtual bool GetArrayElement(const BNetMessageNode* node,
                               std::uint32_t index,
                               BNetMessageNode* result);

  virtual bool GetEnumName(const BNetMessageNode* node, char* buffer,
                           std::size_t buffer_size);

  virtual std::int32_t GetArrayCount(const BNetMessageNode* node);

  virtual bool HasOptional(const BNetMessageNode* node);

  virtual bool SetKeyValue(const BNetMessageNode* node,
                           std::uint32_t key_lo, std::uint32_t key_hi);

  virtual bool SetKeyValueIndirect(const BNetMessageNode* node,
                                   std::uint32_t key_lo,
                                   std::uint32_t key_hi);

  virtual bool SetSimpleValue(const BNetMessageNode* node,
                              std::uint8_t value);

  virtual bool SetFloat(const BNetMessageNode* node, double value);

  virtual bool SetViaCallback(const BNetMessageNode* node,
                              std::int32_t value);

  virtual bool SetString(const BNetMessageNode* node, const char* value);

  virtual bool SetBitData(const BNetMessageNode* node, const void* data,
                          std::uint32_t bit_count);

  virtual bool SetByteData(const BNetMessageNode* node, const void* data,
                           std::size_t byte_count);

  virtual bool IsComposite(const BNetMessageNode* node);

  virtual bool SetFieldByName(const BNetMessageNode* node,
                              std::int32_t value, const char* name);

  virtual bool ResizeArray(const BNetMessageNode* node,
                           std::uint32_t new_size);

  virtual bool SetOptional(const BNetMessageNode* node,
                           std::int32_t value);

  virtual bool ComposeForward(const BNetMessageNode* source_node,
                              BNetMessageReflect& destination_reflect,
                              const BNetMessageNode* destination_node);

  virtual bool ComposeReverse(BNetMessageReflect& source_reflect,
                              const BNetMessageNode* source_node,
                              const BNetMessageNode* destination_node);
};

bool BNetReflect_FindKeyByValue(const void* schema, std::uint32_t val_lo,
                                std::uint32_t val_hi,
                                std::uint32_t* out_index);

bool BNetReflect_IndexToKeyPair(const void* schema, std::uint32_t index,
                                std::uint32_t* out_pair);

bool BNetReflect_FindKeyByValueLinear(const void* schema,
                                      std::uint32_t val_lo,
                                      std::uint32_t val_hi,
                                      std::uint32_t* out_index);

bool BNetReflect_ValidateUTF8(std::uint32_t byte_count, const char* data,
                              std::uint32_t* out_char_count);

}
