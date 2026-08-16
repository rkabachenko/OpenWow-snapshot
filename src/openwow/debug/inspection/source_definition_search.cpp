#include "openwow/debug/inspection/source_definition_search.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <set>
#include <span>
#include <system_error>

namespace openwow::debug {
namespace {

using ExtensionGroup = std::vector<std::string_view>;

bool IsAsciiWhitespace(const char value) noexcept {
  switch (value) {
    case ' ':
    case '\t':
    case '\n':
    case '\r':
    case '\f':
    case '\v':
      return true;
    default:
      return false;
  }
}

char AsciiLower(const char value) noexcept {
  if (value >= 'A' && value <= 'Z') {
    return static_cast<char>(value + ('a' - 'A'));
  }
  return value;
}

std::string AsciiLowerCopy(const std::string_view text) {
  std::string result;
  result.reserve(text.size());
  for (const char value : text) {
    result.push_back(AsciiLower(value));
  }
  return result;
}

bool IsHiddenName(const std::filesystem::path& path) {
  const std::string filename = path.filename().string();
  return !filename.empty() && filename.front() == '.';
}

bool IsNibDirectory(const std::filesystem::path& path) {
  std::string extension = path.extension().string();
  if (!extension.empty() && extension.front() == '.') {
    extension.erase(extension.begin());
  }
  return AsciiLowerCopy(extension) == "nib";
}

std::string LowerExtension(const std::filesystem::path& path) {
  std::string extension = path.extension().string();
  if (!extension.empty() && extension.front() == '.') {
    extension.erase(extension.begin());
  }
  return AsciiLowerCopy(extension);
}

std::vector<ExtensionGroup> RetailExtensionGroups(
    const SourceDefinitionName& name, const bool first_result_only) {
  if (name.is_objective_c_message) {
    if (first_result_only) {
      return {{"mm", "cpp"}, {"h"}};
    }
    return {{"mm", "cpp", "h"}};
  }

  if (first_result_only) {
    return {{"cpp"}, {"c", "mm", "h"}};
  }
  return {{"cpp", "mm", "c", "h"}};
}

bool HasExtension(const std::string_view extension,
                  const std::span<const std::string_view> allowed) noexcept {
  return std::find(allowed.begin(), allowed.end(), extension) != allowed.end();
}

std::optional<std::string> ReadBoundedFile(const std::filesystem::path& path,
                                           const std::uintmax_t expected_size) {
  if (expected_size > static_cast<std::uintmax_t>(
                  std::numeric_limits<std::size_t>::max()) ||
      expected_size > static_cast<std::uintmax_t>(
                  std::numeric_limits<std::streamsize>::max())) {
    return std::nullopt;
  }

  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return std::nullopt;
  }

  std::string source(static_cast<std::size_t>(expected_size), '\0');
  if (!source.empty()) {
    stream.read(source.data(), static_cast<std::streamsize>(source.size()));
    if (!stream || static_cast<std::size_t>(stream.gcount()) != source.size()) {
      return std::nullopt;
    }
  }
  return source;
}

bool IsIdentifierCharacter(const char value) noexcept {
  return (value >= 'a' && value <= 'z') ||
         (value >= 'A' && value <= 'Z') ||
         (value >= '0' && value <= '9') || value == '_';
}

std::string CodeMask(const std::string_view source,
                     const openwow::core::stop_token stop_token) {
  enum class State : std::uint8_t {
    kCode,
    kLineComment,
    kBlockComment,
    kString,
    kCharacter,
    kRawString,
  };
  State state = State::kCode;
  bool escaped = false;
  std::string raw_terminator;
  std::string code(source);
  for (std::size_t index = 0; index < code.size(); ++index) {
    if ((index & 0x3fffU) == 0U && stop_token.stop_requested()) {
      return {};
    }
    const char value = source[index];
    const char next = index + 1U < source.size() ? source[index + 1U] : '\0';
    if (state == State::kCode) {
      if (value == 'R' && next == '"') {
        const std::size_t delimiter_end = source.find('(', index + 2U);
        if (delimiter_end != std::string_view::npos &&
            delimiter_end - (index + 2U) <= 16U &&
            source.substr(index + 2U, delimiter_end - (index + 2U))
                    .find_first_of(" ()\\\t\r\n") == std::string_view::npos) {
          raw_terminator = ")";
          raw_terminator.append(source.substr(index + 2U,
                                              delimiter_end - (index + 2U)));
          raw_terminator.push_back('"');
          std::fill(code.begin() + static_cast<std::ptrdiff_t>(index),
                    code.begin() + static_cast<std::ptrdiff_t>(delimiter_end + 1U), ' ');
          index = delimiter_end;
          state = State::kRawString;
        }
      } else if (value == '/' && next == '/') {
        code[index] = code[index + 1U] = ' ';
        ++index;
        state = State::kLineComment;
      } else if (value == '/' && next == '*') {
        code[index] = code[index + 1U] = ' ';
        ++index;
        state = State::kBlockComment;
      } else if (value == '"') {
        code[index] = ' ';
        state = State::kString;
        escaped = false;
      } else if (value == '\'') {
        code[index] = ' ';
        state = State::kCharacter;
        escaped = false;
      }
      continue;
    }
    if (state == State::kRawString) {
      if (source.substr(index, raw_terminator.size()) == raw_terminator) {
        std::fill(code.begin() + static_cast<std::ptrdiff_t>(index),
                  code.begin() + static_cast<std::ptrdiff_t>(index + raw_terminator.size()),
                  ' ');
        index += raw_terminator.size() - 1U;
        state = State::kCode;
      } else if (value != '\n') {
        code[index] = ' ';
      }
      continue;
    }
    if (value == '\n') {
      if (state == State::kLineComment) {
        state = State::kCode;
      } else if ((state == State::kString || state == State::kCharacter) &&
                 !escaped) {
        state = State::kCode;
      }
      escaped = false;
      continue;
    }
    code[index] = ' ';
    if (state == State::kBlockComment && value == '*' && next == '/') {
      code[index + 1U] = ' ';
      ++index;
      state = State::kCode;
    } else if (state == State::kString || state == State::kCharacter) {
      const char delimiter = state == State::kString ? '"' : '\'';
      if (!escaped && value == delimiter) {
        state = State::kCode;
      }
      const bool was_escaped = escaped;
      escaped = !was_escaped && value == '\\';
    }
  }
  return code;
}

std::vector<std::size_t> LineStarts(const std::string_view source) {
  std::vector<std::size_t> starts{0U};
  for (std::size_t index = 0; index < source.size(); ++index) {
    if (source[index] == '\n') {
      starts.push_back(index + 1U);
    }
  }
  return starts;
}

std::string_view LineAt(const std::string_view source,
                        const std::vector<std::size_t>& starts,
                        const std::size_t line) {
  const std::size_t begin = starts[line];
  std::size_t end = line + 1U < starts.size() ? starts[line + 1U] - 1U
                                               : source.size();
  if (end > begin && source[end - 1U] == '\r') {
    --end;
  }
  return source.substr(begin, end - begin);
}

struct RankedResult {
  SourceDefinitionResult result;
  std::size_t name_rank{0};
  std::size_t extension_rank{0};
  std::size_t root_rank{0};
};

bool RankedLess(const RankedResult& lhs, const RankedResult& rhs) {
  if (lhs.name_rank != rhs.name_rank) {
    return lhs.name_rank < rhs.name_rank;
  }
  if (lhs.extension_rank != rhs.extension_rank) {
    return lhs.extension_rank < rhs.extension_rank;
  }
  if (lhs.root_rank != rhs.root_rank) {
    return lhs.root_rank < rhs.root_rank;
  }
  const std::string lhs_path = lhs.result.filename.generic_string();
  const std::string rhs_path = rhs.result.filename.generic_string();
  if (lhs_path != rhs_path) {
    return lhs_path < rhs_path;
  }
  if (lhs.result.line_number != rhs.result.line_number) {
    return lhs.result.line_number < rhs.result.line_number;
  }
  return lhs.result.column_number < rhs.result.column_number;
}

bool NameBoundaryMatches(const std::string_view code, const std::size_t position,
                         const std::size_t length) noexcept {
  if (position != 0U && IsIdentifierCharacter(code[position - 1U])) {
    return false;
  }
  const std::size_t end = position + length;
  return end == code.size() || !IsIdentifierCharacter(code[end]);
}

std::size_t SkipWhitespace(const std::string_view code, std::size_t position) {
  while (position < code.size() && IsAsciiWhitespace(code[position])) {
    ++position;
  }
  return position;
}

bool HasFunctionBody(const std::string_view code, std::size_t position) {
  position = SkipWhitespace(code, position);
  if (position >= code.size() || code[position] != '(') {
    return false;
  }
  std::size_t depth = 0;
  for (; position < code.size(); ++position) {
    if (code[position] == '(') {
      ++depth;
    } else if (code[position] == ')' && --depth == 0U) {
      ++position;
      break;
    }
  }
  if (depth != 0U) {
    return false;
  }
  std::size_t suffix_parentheses = 0;
  std::size_t suffix_brackets = 0;
  for (; position < code.size(); ++position) {
    if (code[position] == '{') {
      return true;
    }
    if (code[position] == '(') {
      ++suffix_parentheses;
    } else if (code[position] == ')') {
      if (suffix_parentheses == 0U) {
        return false;
      }
      --suffix_parentheses;
    } else if (code[position] == '[') {
      ++suffix_brackets;
    } else if (code[position] == ']') {
      if (suffix_brackets == 0U) {
        return false;
      }
      --suffix_brackets;
    } else if ((code[position] == ',' && suffix_parentheses == 0U &&
                suffix_brackets == 0U) ||
               code[position] == ';' || code[position] == '}') {
      return false;
    }
  }
  return false;
}

bool IsObjectiveCMethodLine(const std::string_view line,
                            const std::size_t column) noexcept {
  const auto first = std::find_if_not(line.begin(), line.end(), [](const char value) {
    return IsAsciiWhitespace(value);
  });
  return first != line.end() && (*first == '-' || *first == '+') &&
         static_cast<std::size_t>(first - line.begin()) < column;
}

bool HasObjectiveCBody(const std::string_view code, const std::size_t position) {
  const std::size_t body = code.find('{', position);
  const std::size_t declaration = code.find(';', position);
  const std::size_t implementation_end = code.find("@end", position);
  return body != std::string_view::npos &&
         (declaration == std::string_view::npos || body < declaration) &&
         (implementation_end == std::string_view::npos || body < implementation_end);
}

void SearchPreparedFile(const std::filesystem::path& path, const SourceDefinitionName& name,
                        const std::string& source, const std::size_t extension_rank,
                        const std::size_t root_rank,
                        std::vector<RankedResult>& results,
                        const openwow::core::stop_token stop_token) {
  const std::string code = CodeMask(source, stop_token);
  if (stop_token.stop_requested()) {
    return;
  }
  const auto starts = LineStarts(source);
  std::vector<std::pair<std::string_view, std::size_t>> wanted{{name.function_name, 0U}};
  if (!name.is_objective_c_message) {
    const std::size_t qualification = name.function_name.rfind("::");
    if (qualification != std::string::npos && qualification + 2U < name.function_name.size()) {
      wanted.emplace_back(std::string_view(name.function_name).substr(qualification + 2U), 1U);
    }
  }
  bool inside_objective_c_implementation = false;
  for (std::size_t line_index = 0; line_index < starts.size(); ++line_index) {
    if (stop_token.stop_requested()) {
      return;
    }
    const std::string_view line = LineAt(source, starts, line_index);
    const std::string_view code_line = LineAt(code, starts, line_index);
    const std::size_t first_code = SkipWhitespace(code_line, 0U);
    if (first_code < code_line.size() && code_line[first_code] == '#') {
      continue;
    }
    if (name.is_objective_c_message) {
      const std::size_t implementation = code_line.find("@implementation");
      if (implementation != std::string_view::npos) {
        const std::size_t class_at = code_line.find(name.objective_c_class_name,
                                                    implementation + 15U);
        inside_objective_c_implementation =
            class_at != std::string_view::npos &&
            NameBoundaryMatches(code_line, class_at, name.objective_c_class_name.size());
      }
      if (code_line.find("@end") != std::string_view::npos) {
        inside_objective_c_implementation = false;
      }
      if (!inside_objective_c_implementation) {
        continue;
      }
    }
    bool matched_line = false;
    for (const auto& [symbol, name_rank] : wanted) {
      std::size_t position = 0;
      while ((position = code_line.find(symbol, position)) != std::string_view::npos) {
        const std::size_t next = position + symbol.size();
        if (NameBoundaryMatches(code_line, position, symbol.size()) &&
            ((!name.is_objective_c_message &&
              HasFunctionBody(code, starts[line_index] + next)) ||
             (name.is_objective_c_message &&
              IsObjectiveCMethodLine(code_line, position) &&
              HasObjectiveCBody(code, starts[line_index] + next)))) {
          results.push_back({.result = {.filename = path,
                                        .line_number = line_index + 1U,
                                        .line_text = std::string(line),
                                        .column_number = position + 1U},
                             .name_rank = name_rank,
                             .extension_rank = extension_rank,
                             .root_rank = root_rank});
          matched_line = true;
          break;
        }
        position = next;
      }
      if (matched_line) {
        break;
      }
    }
  }
}

bool IsWithinRoot(const std::filesystem::path& root,
                  const std::filesystem::path& path) {
  auto root_part = root.begin();
  auto path_part = path.begin();
  for (; root_part != root.end(); ++root_part, ++path_part) {
    if (path_part == path.end() || *root_part != *path_part) {
      return false;
    }
  }
  return true;
}

void CollectRootFiles(const std::filesystem::path& root,
                      const SourceDefinitionSearchOptions& options,
                      std::size_t& traversal_entries, bool& file_limit_reached,
                      bool& traversal_limit_reached,
                      std::set<std::string>& seen_files,
                      std::vector<std::pair<std::filesystem::path, std::size_t>>& files,
                      const std::size_t root_rank,
                      const openwow::core::stop_token stop_token) {
  std::vector<std::filesystem::path> pending{root};
  std::error_code error;
  while (!pending.empty() && !file_limit_reached && !traversal_limit_reached) {
    if (stop_token.stop_requested()) {
      return;
    }
    const std::filesystem::path directory = std::move(pending.back());
    pending.pop_back();
    std::vector<std::filesystem::directory_entry> entries;
    std::filesystem::directory_iterator iterator(
        directory, std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::directory_iterator end;
    for (; !error && iterator != end; iterator.increment(error)) {
      if (++traversal_entries > options.max_traversal_entries) {
        traversal_limit_reached = true;
        break;
      }
      entries.push_back(*iterator);
    }
    if (traversal_limit_reached) {
      entries.clear();
    }
    error.clear();
    std::sort(entries.begin(), entries.end(), [](const auto& lhs, const auto& rhs) {
      return lhs.path().generic_string() < rhs.path().generic_string();
    });
    for (auto entry = entries.rbegin(); entry != entries.rend(); ++entry) {
      const auto status = entry->symlink_status(error);
      if (error || std::filesystem::is_symlink(status) || IsHiddenName(entry->path())) {
        error.clear();
        continue;
      }
      if (std::filesystem::is_directory(status)) {
        if (!IsNibDirectory(entry->path())) {
          pending.push_back(entry->path());
        }
      } else if (std::filesystem::is_regular_file(status)) {
        const std::string extension = LowerExtension(entry->path());
        constexpr std::array<std::string_view, 4> kExtensions{"c", "cpp", "h", "mm"};
        if (!HasExtension(extension, kExtensions)) {
          continue;
        }
        const auto canonical = std::filesystem::weakly_canonical(entry->path(), error);
        if (error || !IsWithinRoot(root, canonical)) {
          error.clear();
          continue;
        }
        if (seen_files.insert(canonical.generic_string()).second) {
          if (files.size() >= options.max_files) {
            file_limit_reached = true;
            return;
          }
          files.emplace_back(canonical, root_rank);
        }
      }
    }
  }
}

}

std::string_view StripRetailStackOffset(
    const std::string_view definition) noexcept {
  const std::size_t suffix = definition.find(" +");
  return suffix == std::string_view::npos ? definition
                                          : definition.substr(0, suffix);
}

std::optional<SourceDefinitionName> ParseRetailSourceDefinition(
    const std::string_view definition) {
  if (definition.empty()) {
    return std::nullopt;
  }

  if (definition.front() != '-' && definition.front() != '+') {
    const std::size_t arguments = definition.rfind('(');
    std::string_view function = definition;
    if (arguments != std::string_view::npos) {
      function = definition.substr(0, arguments);
    }
    if (function.empty()) {
      return std::nullopt;
    }
    return SourceDefinitionName{.function_name = std::string(function)};
  }

  constexpr std::string_view kTrimCharacters{"-+[]"};
  std::size_t begin = 0;
  std::size_t end = definition.size();
  while (begin < end &&
         kTrimCharacters.find(definition[begin]) != std::string_view::npos) {
    ++begin;
  }
  while (end > begin &&
         kTrimCharacters.find(definition[end - 1U]) !=
             std::string_view::npos) {
    --end;
  }
  std::string_view trimmed = definition.substr(begin, end - begin);
  if (const std::size_t colon = trimmed.find(':');
      colon != std::string_view::npos) {

    trimmed = trimmed.substr(0, colon + 1U);
  }

  const std::size_t separator = trimmed.find(' ');
  if (separator == std::string_view::npos || separator == 0U ||
      separator + 1U >= trimmed.size()) {
    return std::nullopt;
  }
  return SourceDefinitionName{
      .function_name = std::string(trimmed.substr(separator + 1U)),
      .objective_c_class_name = std::string(trimmed.substr(0, separator)),
      .is_objective_c_message = true,
  };
}

SourceDefinitionSearchResult SearchSourceDefinitionsOnWorker(
    const SourceDefinitionName& name,
    const SourceDefinitionSearchOptions& options,
    const openwow::core::stop_token stop_token) {
  SourceDefinitionSearchResult result;
  if (stop_token.stop_requested()) {
    result.status = SourceDefinitionSearchStatus::kCancelled;
    return result;
  }
  if (name.function_name.empty() ||
      (name.is_objective_c_message && name.objective_c_class_name.empty())) {
    return result;
  }
  if (options.max_traversal_entries == 0U) {
    result.status = SourceDefinitionSearchStatus::kTraversalLimitReached;
    return result;
  }
  if (options.max_files == 0U) {
    result.status = SourceDefinitionSearchStatus::kFileLimitReached;
    return result;
  }
  if (options.max_results == 0U) {
    result.status = SourceDefinitionSearchStatus::kResultLimitReached;
    return result;
  }
  if (options.max_file_bytes == 0U || options.max_total_bytes == 0U) {
    result.status = SourceDefinitionSearchStatus::kByteLimitReached;
    return result;
  }

  bool file_limit_reached = false;
  bool traversal_limit_reached = false;
  std::size_t traversal_entries = 0;
  std::set<std::string> seen_files;
  std::set<std::string> seen_roots;
  std::vector<std::pair<std::filesystem::path, std::size_t>> files;
  std::error_code error;
  for (std::size_t root_rank = 0; root_rank < options.roots.size(); ++root_rank) {
    const auto& configured_root = options.roots[root_rank];
    if (stop_token.stop_requested()) {
      result.status = SourceDefinitionSearchStatus::kCancelled;
      return result;
    }
    const auto root = std::filesystem::weakly_canonical(configured_root, error);
    const auto status = error ? std::filesystem::file_status{}
                              : std::filesystem::status(root, error);
    if (error || !std::filesystem::is_directory(status)) {
      error.clear();
      continue;
    }
    if (seen_roots.insert(root.generic_string()).second) {
      CollectRootFiles(root, options, traversal_entries, file_limit_reached,
                       traversal_limit_reached, seen_files, files, root_rank,
                       stop_token);
    }
    if (file_limit_reached || traversal_limit_reached) {
      break;
    }
  }
  result.candidate_files = files.size();

  std::sort(files.begin(), files.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.second != rhs.second
               ? lhs.second < rhs.second
               : lhs.first.generic_string() < rhs.first.generic_string();
  });
  const auto groups = RetailExtensionGroups(name, options.first_result_only);
  std::vector<RankedResult> ranked_results;
  std::uintmax_t total_bytes = 0;
  bool byte_limit_reached = false;
  bool result_limit_reached = false;
  for (std::size_t extension_rank = 0; extension_rank < groups.size();
       ++extension_rank) {
    for (const auto& [path, root_rank] : files) {
      if (stop_token.stop_requested()) {
        result.status = SourceDefinitionSearchStatus::kCancelled;
        return result;
      }
      if (!HasExtension(LowerExtension(path), groups[extension_rank])) {
        continue;
      }
      const auto current_status = std::filesystem::symlink_status(path, error);
      const auto current_path = error ? std::filesystem::path{}
                                      : std::filesystem::weakly_canonical(path, error);
      if (error || std::filesystem::is_symlink(current_status) ||
          !std::filesystem::is_regular_file(current_status) ||
          current_path != path) {
        error.clear();
        continue;
      }
      const std::uintmax_t size = std::filesystem::file_size(path, error);
      if (error || size > options.max_file_bytes) {
        error.clear();
        continue;
      }
      if (size > options.max_total_bytes - total_bytes) {
        byte_limit_reached = true;
        break;
      }
      auto source = ReadBoundedFile(path, size);
      if (!source.has_value()) {
        continue;
      }
      total_bytes += size;
      SearchPreparedFile(path, name, *source, extension_rank, root_rank,
                         ranked_results, stop_token);
      if (ranked_results.size() > options.max_results) {
        result_limit_reached = true;
        std::sort(ranked_results.begin(), ranked_results.end(), RankedLess);
        ranked_results.resize(options.max_results);
      }
    }
    if (byte_limit_reached) {
      break;
    }
  }

  std::sort(ranked_results.begin(), ranked_results.end(), RankedLess);
  const std::size_t result_count =
      options.first_result_only ? std::min<std::size_t>(1U, ranked_results.size())
                                : std::min(options.max_results, ranked_results.size());
  result.matches.reserve(result_count);
  for (std::size_t index = 0; index < result_count; ++index) {
    result.matches.push_back(std::move(ranked_results[index].result));
  }
  if (stop_token.stop_requested()) {
    result.status = SourceDefinitionSearchStatus::kCancelled;
  } else if (traversal_limit_reached) {
    result.status = SourceDefinitionSearchStatus::kTraversalLimitReached;
  } else if (file_limit_reached) {
    result.status = SourceDefinitionSearchStatus::kFileLimitReached;
  } else if (byte_limit_reached) {
    result.status = SourceDefinitionSearchStatus::kByteLimitReached;
  } else if (result_limit_reached) {
    result.status = SourceDefinitionSearchStatus::kResultLimitReached;
  }
  return result;
}

}
