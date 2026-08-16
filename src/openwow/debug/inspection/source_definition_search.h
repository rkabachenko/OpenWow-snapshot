#pragma once

#include "openwow/runtime/scheduling/jthread_compat.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::debug {

struct SourceDefinitionName {
  std::string function_name;
  std::string objective_c_class_name;
  bool is_objective_c_message{false};

  bool operator==(const SourceDefinitionName&) const = default;
};

[[nodiscard]] std::string_view StripRetailStackOffset(
    std::string_view definition) noexcept;

[[nodiscard]] std::optional<SourceDefinitionName>
ParseRetailSourceDefinition(std::string_view definition);

struct SourceDefinitionResult {
  std::filesystem::path filename;
  std::size_t line_number{0};
  std::string line_text;
  std::size_t column_number{0};

  bool operator==(const SourceDefinitionResult&) const = default;
};

struct SourceDefinitionSearchOptions {

  std::vector<std::filesystem::path> roots;
  bool first_result_only{false};
  std::size_t max_files{100'000};
  std::uintmax_t max_file_bytes{8U * 1024U * 1024U};
  std::size_t max_traversal_entries{500'000};
  std::size_t max_results{10'000};
  std::uintmax_t max_total_bytes{256U * 1024U * 1024U};
};

enum class SourceDefinitionSearchStatus : std::uint8_t {
  kComplete,
  kCancelled,
  kFileLimitReached,
  kTraversalLimitReached,
  kByteLimitReached,
  kResultLimitReached,
};

struct SourceDefinitionSearchResult {
  SourceDefinitionSearchStatus status{SourceDefinitionSearchStatus::kComplete};
  std::vector<SourceDefinitionResult> matches;
  std::size_t candidate_files{0};
};

[[nodiscard]] SourceDefinitionSearchResult SearchSourceDefinitionsOnWorker(
    const SourceDefinitionName& name,
    const SourceDefinitionSearchOptions& options,
    openwow::core::stop_token stop_token = {});

}
