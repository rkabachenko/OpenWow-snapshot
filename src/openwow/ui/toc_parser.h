#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <unordered_map>
#include <vector>

namespace openwow::ui {

struct TOCEntry {
  std::string filename;
  bool is_xml = false;
  bool is_lua = false;
};

struct TOCFile {
  std::string path;

  std::string title;
  std::string notes;
  std::string author;
  std::string version;
  std::string revision;
  std::string interface_version;
  std::string saved_variables;
  std::string saved_variables_per_character;
  std::string default_state;
  std::string load_on_demand;
  std::string secure;

  std::vector<std::string> dependencies;
  std::vector<std::string> optional_deps;
  std::vector<std::string> load_with;
  std::vector<std::string> load_managers;

  std::vector<TOCEntry> files;

  std::unordered_map<std::string, std::string> directives;

  std::vector<std::pair<std::string, std::string>> directive_entries;

  [[nodiscard]] bool IsLoadOnDemand() const;

  [[nodiscard]] bool IsDefaultEnabled() const;

  [[nodiscard]] bool IsSecure() const;

  [[nodiscard]] uint32_t GetInterfaceVersion() const;

  [[nodiscard]] uint32_t GetRevision() const;
};

class TOCParser {
 public:

  static std::vector<std::string> ExtractVisibleFileEntries(
      std::string_view content, bool strip_utf8_bom);

  static std::size_t CountVisibleFileEntries(std::string_view content,
                                             bool strip_utf8_bom);

  static std::optional<TOCFile> Parse(const std::string& content,
                                      const std::string& basePath = "");

  static std::optional<TOCFile> ParseFile(const std::string& path);

  static std::vector<std::string> SplitComma(const std::string& value);

 private:

  static void ParseDirective(const std::string& line, TOCFile& toc);

  static void ParseFileEntry(const std::string& line,
                             const std::string& basePath, TOCFile& toc);
};

}
