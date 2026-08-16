#pragma once

#include <iterator>

#define OPENWOW_DBC_SCHEMA(Type, ...)                                             \
  Type Type::Load(const DbcFile &f, const std::uint32_t row) {                   \
    Type e{};                                                                     \
    __VA_ARGS__                                                                   \
    return e;                                                                     \
  }

#define DBC_U32(member, field) e.member = f.GetUInt32(row, field);
#define DBC_I32(member, field) e.member = f.GetInt32(row, field);
#define DBC_F32(member, field) e.member = f.GetFloat(row, field);
#define DBC_STRING(member, field) e.member = f.GetString(row, field);
#define DBC_LOCALIZED(member, field) e.member = f.GetLocalizedString(row, field);
#define DBC_BYTE_OFFSET(member, offset) e.member = f.GetByte(row, offset);
#define DBC_U32_OFFSET(member, offset) e.member = f.GetUInt32AtOffset(row, offset);
#define DBC_I32_OFFSET(member, offset) e.member = f.GetInt32AtOffset(row, offset);
#define DBC_F32_OFFSET(member, offset) e.member = f.GetFloatAtOffset(row, offset);
#define DBC_STRING_OFFSET(member, offset) e.member = f.GetStringAtOffset(row, offset);
#define DBC_ROW_ID() e.id = row;
#define DBC_COMPUTED_ID(value) e.id = (value);
#define DBC_U32_ARRAY(member, first_field)                                      \
  for (std::size_t dbc_index = 0; dbc_index < std::size(e.member); ++dbc_index) {\
    e.member[dbc_index] =                                                       \
        f.GetUInt32(row, (first_field) + static_cast<std::uint32_t>(dbc_index));\
  }
#define DBC_I32_ARRAY(member, first_field)                                      \
  for (std::size_t dbc_index = 0; dbc_index < std::size(e.member); ++dbc_index) {\
    e.member[dbc_index] =                                                       \
        f.GetInt32(row, (first_field) + static_cast<std::uint32_t>(dbc_index)); \
  }
#define DBC_F32_ARRAY(member, first_field)                                      \
  for (std::size_t dbc_index = 0; dbc_index < std::size(e.member); ++dbc_index) {\
    e.member[dbc_index] =                                                       \
        f.GetFloat(row, (first_field) + static_cast<std::uint32_t>(dbc_index)); \
  }
#define DBC_STRING_ARRAY(member, first_field)                                   \
  for (std::size_t dbc_index = 0; dbc_index < std::size(e.member); ++dbc_index) {\
    e.member[dbc_index] =                                                       \
        f.GetString(row, (first_field) + static_cast<std::uint32_t>(dbc_index));\
  }
#define DBC_U32_OFFSET_ARRAY(member, first_offset, stride)                       \
  for (std::size_t dbc_index = 0; dbc_index < std::size(e.member); ++dbc_index) {\
    e.member[dbc_index] = f.GetUInt32AtOffset(                                  \
        row, (first_offset) +                                                   \
                 static_cast<std::uint32_t>(dbc_index) * (stride));              \
  }
#define DBC_I32_OFFSET_ARRAY(member, first_offset, stride)                       \
  for (std::size_t dbc_index = 0; dbc_index < std::size(e.member); ++dbc_index) {\
    e.member[dbc_index] = f.GetInt32AtOffset(                                   \
        row, (first_offset) +                                                   \
                 static_cast<std::uint32_t>(dbc_index) * (stride));              \
  }
