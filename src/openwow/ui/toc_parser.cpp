
#include "openwow/ui/toc_parser.h"

#include "openwow/ui/ui_enum_helpers.h"
#include "openwow/foundation/text/ascii.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace openwow::ui {

using openwow::text::StripUtf8Bom;

namespace {

constexpr std::size_t kRetailTocLineCapacity = 0x400;

bool AsciiCaseInsensitiveEquals(const std::string_view lhs,
                                const std::string_view rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t index = 0; index < lhs.size(); ++index) {
    if (std::tolower(static_cast<unsigned char>(lhs[index])) !=
        std::tolower(static_cast<unsigned char>(rhs[index]))) {
      return false;
    }
  }
  return true;
}

bool AsciiCaseInsensitiveStartsWith(const std::string_view value,
                                    const std::string_view prefix) {
  return value.size() >= prefix.size() &&
         AsciiCaseInsensitiveEquals(value.substr(0, prefix.size()), prefix);
}

template <typename Callback>
void ForEachRetailTocLine(const std::string_view content,
                          const bool strip_utf8_bom,
                          Callback&& callback) {
  const std::string_view text = strip_utf8_bom ? StripUtf8Bom(content) : content;
  std::size_t cursor = 0;

  for (;;) {
    while (cursor < text.size() && text[cursor] != '\0' &&
           (text[cursor] == '\r' || text[cursor] == '\n')) {
      ++cursor;
    }

    std::string line;
    line.reserve(std::min(kRetailTocLineCapacity - 1u,
                          text.size() - std::min(cursor, text.size())));
    while (cursor < text.size() && text[cursor] != '\0' &&
           text[cursor] != '\r' && text[cursor] != '\n') {
      if (line.size() < kRetailTocLineCapacity - 1u) {
        line.push_back(text[cursor]);
      }
      ++cursor;
    }

    if (cursor < text.size() && text[cursor] != '\0') {
      ++cursor;
    }

    callback(line);
    if (line.empty()) {
      return;
    }
  }
}

std::string_view TrimTrailingSpaces(std::string_view value) {
  while (!value.empty() && value.back() == ' ') {
    value.remove_suffix(1);
  }
  return value;
}

std::string_view DirectiveValue(const std::string_view body) {
  const std::size_t colon = body.find(':');
  if (colon == std::string_view::npos) {
    return {};
  }

  std::size_t value = colon;
  while (value < body.size() && (body[value] == ':' || body[value] == ' ')) {
    ++value;
  }
  return body.substr(value);
}

std::string DirectiveKey(const std::string_view body) {
  const std::size_t colon = body.find(':');
  const std::string_view raw_key = body.substr(0, colon);
  return std::string(TrimTrailingSpaces(raw_key));
}

void RecordDirective(TOCFile& toc, const std::string_view body,
                     const std::string_view value) {
  std::string key = DirectiveKey(body);
  if (key.empty()) {
    return;
  }
  std::string stored_value(value);
  toc.directive_entries.emplace_back(key, stored_value);
  toc.directives[std::move(key)] = std::move(stored_value);
}

bool IsMetadataDirective(const std::string_view body) {
  return AsciiCaseInsensitiveStartsWith(body, "Title") ||
         AsciiCaseInsensitiveStartsWith(body, "Notes") ||
         AsciiCaseInsensitiveStartsWith(body, "Author") ||
         AsciiCaseInsensitiveStartsWith(body, "Version") ||
         AsciiCaseInsensitiveStartsWith(body, "X-");
}

void ApplyMetadataDirective(const std::string_view body, TOCFile& toc) {
  const std::string key = DirectiveKey(body);
  const std::string value(DirectiveValue(body));
  if (key.empty()) {
    return;
  }

  toc.directive_entries.emplace_back(key, value);
  toc.directives[key] = value;

  if (AsciiCaseInsensitiveEquals(key, "Title")) {
    toc.title = value;
  } else if (AsciiCaseInsensitiveEquals(key, "Notes")) {
    toc.notes = value;
  } else if (AsciiCaseInsensitiveEquals(key, "Author")) {
    toc.author = value;
  } else if (AsciiCaseInsensitiveEquals(key, "Version")) {
    toc.version = value;
  }
}

void AppendDirectiveTokens(std::vector<std::string>& destination,
                           const std::string_view value) {
  auto tokens = TOCParser::SplitComma(std::string(value));
  destination.insert(destination.end(),
                     std::make_move_iterator(tokens.begin()),
                     std::make_move_iterator(tokens.end()));
}

void AppendDirectiveValue(std::string& destination,
                          const std::string_view value) {
  if (value.empty()) {
    return;
  }
  if (!destination.empty()) {
    destination += ", ";
  }
  destination.append(value);
}

}

bool TOCFile::IsLoadOnDemand() const {
  return ParseBooleanString(load_on_demand.c_str(), 0) != 0;
}

bool TOCFile::IsDefaultEnabled() const {
  return !AsciiCaseInsensitiveEquals(default_state, "disabled");
}

bool TOCFile::IsSecure() const {
  return ParseBooleanString(secure.c_str(), 0) != 0;
}

uint32_t TOCFile::GetInterfaceVersion() const {
  if (interface_version.empty()) {
    return 0;
  }
  uint32_t value = 0;
  const auto [_, error] = std::from_chars(
      interface_version.data(), interface_version.data() + interface_version.size(), value);
  return error == std::errc() ? value : 0;
}

uint32_t TOCFile::GetRevision() const {
  if (revision.empty()) {
    return 0;
  }
  uint32_t value = 0;
  const auto [end, error] = std::from_chars(
      revision.data(), revision.data() + revision.size(), value);
  return error == std::errc() && end != revision.data() ? value : 0;
}

std::vector<std::string> TOCParser::ExtractVisibleFileEntries(
    const std::string_view content, const bool strip_utf8_bom) {
  std::vector<std::string> entries;
  ForEachRetailTocLine(content, strip_utf8_bom, [&](std::string line) {
    if (line.empty() || line.front() == '#') {
      return;
    }

    while (!line.empty() && line.back() == ' ') {
      line.pop_back();
    }
    entries.push_back(std::move(line));
  });
  return entries;
}

std::size_t TOCParser::CountVisibleFileEntries(
    const std::string_view content, const bool strip_utf8_bom) {
  std::size_t count = 0;
  ForEachRetailTocLine(content, strip_utf8_bom, [&](const std::string& line) {
    if (!line.empty() && line.front() != '#') {
      ++count;
    }
  });
  return count;
}

std::vector<std::string> TOCParser::SplitComma(const std::string& value) {
  std::vector<std::string> result;
  std::string current;
  for (const char ch : value) {
    if (ch == ',' || ch == ' ') {
      if (!current.empty()) {
        result.push_back(std::move(current));
        current.clear();
      }
      continue;
    }
    current.push_back(ch);
  }
  if (!current.empty()) {
    result.push_back(std::move(current));
  }
  return result;
}

void TOCParser::ParseDirective(const std::string& line, TOCFile& toc) {
  std::size_t body_start = 2;
  while (body_start < line.size() && line[body_start] == ' ') {
    ++body_start;
  }
  const std::string_view body(line.data() + body_start, line.size() - body_start);

  if (AsciiCaseInsensitiveStartsWith(body, "Interface:")) {
    const std::string_view value = DirectiveValue(body);
    toc.interface_version = value;
    RecordDirective(toc, body, value);
    return;
  }

  if (IsMetadataDirective(body)) {
    ApplyMetadataDirective(body, toc);
    return;
  }

  if (AsciiCaseInsensitiveStartsWith(body, "Revision:")) {
    const std::string_view value = DirectiveValue(body);
    toc.revision = value;
    RecordDirective(toc, body, value);
    return;
  }

  if (AsciiCaseInsensitiveStartsWith(body, "OptionalDep")) {
    if (body.find(':') != std::string_view::npos) {
      const std::string_view value = DirectiveValue(body);
      AppendDirectiveTokens(toc.optional_deps, value);
      RecordDirective(toc, body, value);
    }
    return;
  }

  if (AsciiCaseInsensitiveStartsWith(body, "RequiredDep") ||
      AsciiCaseInsensitiveStartsWith(body, "Dep")) {
    if (body.find(':') != std::string_view::npos) {
      const std::string_view value = DirectiveValue(body);
      AppendDirectiveTokens(toc.dependencies, value);
      RecordDirective(toc, body, value);
    }
    return;
  }

  if (AsciiCaseInsensitiveStartsWith(body, "LoadWith:")) {
    const std::string_view value = DirectiveValue(body);
    AppendDirectiveTokens(toc.load_with, value);
    RecordDirective(toc, body, value);
    return;
  }

  if (AsciiCaseInsensitiveStartsWith(body, "LoadManagers:")) {
    const std::string_view value = DirectiveValue(body);
    AppendDirectiveTokens(toc.load_managers, value);
    RecordDirective(toc, body, value);
    return;
  }

  if (AsciiCaseInsensitiveStartsWith(body, "SavedVariables:")) {
    const std::string_view value = DirectiveValue(body);
    AppendDirectiveValue(toc.saved_variables, value);
    RecordDirective(toc, body, value);
    return;
  }

  if (AsciiCaseInsensitiveStartsWith(body, "SavedVariablesPerCharacter:")) {
    const std::string_view value = DirectiveValue(body);
    AppendDirectiveValue(toc.saved_variables_per_character, value);
    RecordDirective(toc, body, value);
    return;
  }

  if (AsciiCaseInsensitiveStartsWith(body, "DefaultState:")) {
    const std::string_view value = DirectiveValue(body);
    if (AsciiCaseInsensitiveEquals(value, "enabled") ||
        AsciiCaseInsensitiveEquals(value, "disabled")) {
      toc.default_state = value;
    }
    RecordDirective(toc, body, value);
    return;
  }

  if (AsciiCaseInsensitiveStartsWith(body, "LoadOnDemand:")) {
    const std::string_view value = DirectiveValue(body);
    toc.load_on_demand = value;
    RecordDirective(toc, body, value);
    return;
  }

  if (AsciiCaseInsensitiveStartsWith(body, "Secure:")) {
    const std::string_view value = DirectiveValue(body);
    toc.secure = value;
    RecordDirective(toc, body, value);
  }
}

void TOCParser::ParseFileEntry(const std::string& line,
                               const std::string& ,
                               TOCFile& toc) {
  std::string filename(TrimTrailingSpaces(line));
  if (filename.empty()) {
    return;
  }

  std::replace(filename.begin(), filename.end(), '\\', '/');

  TOCEntry entry;
  entry.filename = filename;
  const auto extension = openwow::text::ToLowerAscii(
      std::filesystem::path(filename).extension().string());
  entry.is_xml = extension == ".xml";
  entry.is_lua = extension == ".lua";
  toc.files.push_back(std::move(entry));
}

std::optional<TOCFile> TOCParser::Parse(const std::string& content,
                                        const std::string& basePath) {
  TOCFile toc;
  toc.path = basePath;

  ForEachRetailTocLine(content, true, [&](const std::string& line) {
    if (line.size() >= 2u && line[0] == '#' && line[1] == '#') {
      ParseDirective(line, toc);
    } else if (!line.empty() && line[0] != '#') {
      ParseFileEntry(line, basePath, toc);
    }
  });

  return toc;
}

std::optional<TOCFile> TOCParser::ParseFile(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    return std::nullopt;
  }

  const std::string content((std::istreambuf_iterator<char>(input)),
                            std::istreambuf_iterator<char>());
  auto result = Parse(content, path);
  result->path = path;
  return result;
}

}
