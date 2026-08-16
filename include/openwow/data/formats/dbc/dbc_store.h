#pragma once
#include "openwow/data/formats/dbc/dbc_file.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace openwow::data::dbc {

enum class DbcStoreLoadError {
  kNone,
  kUnexpectedFieldCount,
  kUnexpectedRecordSize,
  kIndexRangeTooLarge,
};

struct DbcStoreLoadResult {
  DbcStoreLoadError error = DbcStoreLoadError::kNone;
  std::string source_name;
  std::uint32_t observed = 0;
  std::uint32_t expected = 0;

  [[nodiscard]] bool succeeded() const {
    return error == DbcStoreLoadError::kNone;
  }

  [[nodiscard]] operator bool() const {
    return succeeded();
  }
};

template <typename T>
struct DbcTableSchema {
  using Decoder = T (*)(const DbcFile &, std::uint32_t);

  std::string_view retail_path;
  std::uint32_t field_count;
  std::uint32_t record_size;
  Decoder decode;
};

struct RetailDbcDescriptor {
  std::string_view retail_path;
  std::uint32_t field_count;
  std::uint32_t record_size;

  [[nodiscard]] constexpr std::string_view filename() const {
    const auto separator = retail_path.rfind('\\');
    return separator == std::string_view::npos
               ? retail_path
               : retail_path.substr(separator + 1u);
  }
};

[[nodiscard]] const RetailDbcDescriptor &
FindRetailDbcDescriptor(std::string_view filename);

template <typename T>
[[nodiscard]] DbcTableSchema<T>
RetailDbcSchema(const std::string_view filename) {
  const auto &descriptor = FindRetailDbcDescriptor(filename);
  return {
      .retail_path = descriptor.retail_path,
      .field_count = descriptor.field_count,
      .record_size = descriptor.record_size,
      .decode = &T::Load,
  };
}

namespace detail {

template <typename T>
concept HasCaseInsensitiveNameLookup = requires(const T &entry) {
  std::string_view{entry.name};
};

inline std::string FoldAsciiCase(const std::string_view value) {
  std::string folded(value);
  for (char &character : folded) {
    if (character >= 'A' && character <= 'Z') {
      character = static_cast<char>(character + ('a' - 'A'));
    }
  }
  return folded;
}

}

template <typename T> class DbcStore {
public:
  DbcStoreLoadResult LoadFromFile(DbcFile file, const DbcTableSchema<T> &schema,
                                  std::string_view source_name = {}) {
    if (!entries_.empty()) {
      return {};
    }

    if (file.record_count() == 0u) {
      return {};
    }
    if (file.field_count() != schema.field_count) {
      return {
          .error = DbcStoreLoadError::kUnexpectedFieldCount,
          .source_name = std::string(source_name),
          .observed = file.field_count(),
          .expected = schema.field_count,
      };
    }
    if (file.record_size() != schema.record_size) {
      return {
          .error = DbcStoreLoadError::kUnexpectedRecordSize,
          .source_name = std::string(source_name),
          .observed = file.record_size(),
          .expected = schema.record_size,
      };
    }

    auto owned_file = std::make_unique<DbcFile>(std::move(file));
    std::vector<T> entries;
    entries.reserve(owned_file->record_count());
    for (std::uint32_t row = 0; row < owned_file->record_count(); ++row) {
      entries.push_back(schema.decode(*owned_file, row));
    }

    auto min_id = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t max_id = 0;
    for (const auto &entry : entries) {
      min_id = std::min(min_id, entry.id);
      max_id = std::max(max_id, entry.id);
    }

    const auto index_width =
        static_cast<std::uint64_t>(max_id) - min_id + 1u;
    if (index_width > std::numeric_limits<std::size_t>::max()) {
      return {
          .error = DbcStoreLoadError::kIndexRangeTooLarge,
          .source_name = std::string(source_name),
      };
    }
    const auto index_size = static_cast<std::size_t>(index_width);
    std::vector<std::size_t> index(index_size, kMissingIndex);
    std::unordered_map<std::string, std::size_t> name_index;

    for (std::size_t entry_index = 0; entry_index < entries.size(); ++entry_index) {
      const auto &entry = entries[entry_index];
      index[static_cast<std::size_t>(entry.id - min_id)] = entry_index;
      if constexpr (detail::HasCaseInsensitiveNameLookup<T>) {
        const std::string_view name = entry.name;
        if (!name.empty()) {
          name_index[detail::FoldAsciiCase(name)] = entry_index;
        }
      }
    }

    file_ = std::move(owned_file);
    entries_ = std::move(entries);
    index_ = std::move(index);
    name_index_ = std::move(name_index);
    min_id_ = min_id;
    max_id_ = max_id;
    return {};
  }

  [[nodiscard]] const T *LookupEntry(std::uint32_t id) const {
    if (entries_.empty() || id < min_id_ || id > max_id_) {
      return nullptr;
    }
    const auto entry_index = index_[static_cast<std::size_t>(id - min_id_)];
    return entry_index != kMissingIndex ? &entries_[entry_index] : nullptr;
  }

  [[nodiscard]] const T *LookupEntryByRowIndex(const int row_index) const {
    if (row_index < 0) {
      return nullptr;
    }

    const auto index = static_cast<std::size_t>(row_index);
    if (index >= entries_.size()) {
      return nullptr;
    }

    return &entries_[index];
  }

  [[nodiscard]] const T *LookupByNameCaseInsensitive(std::string_view name) const {
    if constexpr (!detail::HasCaseInsensitiveNameLookup<T>) {
      (void)name;
      return nullptr;
    } else {
      if (name.empty()) {
        return nullptr;
      }
      const auto it = name_index_.find(detail::FoldAsciiCase(name));
      return (it != name_index_.end()) ? &entries_[it->second] : nullptr;
    }
  }

  [[nodiscard]] const std::vector<T> &entries() const {
    return entries_;
  }
  [[nodiscard]] std::uint32_t size() const {
    return static_cast<std::uint32_t>(entries_.size());
  }
  [[nodiscard]] std::uint32_t max_id() const {
    return max_id_;
  }
  [[nodiscard]] bool empty() const {
    return entries_.empty();
  }

  auto begin() const {
    return entries_.begin();
  }
  auto end() const {
    return entries_.end();
  }

private:
  static constexpr auto kMissingIndex =
      std::numeric_limits<std::size_t>::max();

  std::unique_ptr<DbcFile> file_;
  std::vector<T> entries_;
  std::vector<std::size_t> index_;
  std::unordered_map<std::string, std::size_t> name_index_;
  std::uint32_t min_id_ = 0;
  std::uint32_t max_id_ = 0;
};

}
