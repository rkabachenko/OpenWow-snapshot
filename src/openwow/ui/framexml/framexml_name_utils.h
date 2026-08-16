#pragma once

#include "openwow/foundation/text/ascii.h"
#include "openwow/vfs/client_path_identity.h"

#include <string>
#include <string_view>
#include <vector>

namespace openwow::ui::framexml {

enum class TemplateListSyntax {
  kCommaSeparated,
  kCreateFrame,
};

inline std::string NormalizeVirtualPath(std::string_view raw_path) {
  return openwow::vfs::NormalizeClientDisplayPath(raw_path);
}

inline std::string ResolveVirtualPath(std::string_view raw_path, std::string_view base_dir) {
  return openwow::vfs::ResolveClientPathIdentity(raw_path, base_dir).display_path;
}

inline std::vector<std::string> SplitTemplateList(std::string_view inherits,
                                                  TemplateListSyntax syntax) {
  std::vector<std::string> names;
  std::string current;

  const auto flush = [&]() {
    auto token = openwow::text::Trim(current);
    if (!token.empty()) {
      names.push_back(std::move(token));
    }
    current.clear();
  };

  for (const char ch : inherits) {
    const bool is_separator =
        ch == ',' || (syntax == TemplateListSyntax::kCreateFrame && ch == ' ');
    if (is_separator) {
      flush();
      continue;
    }
    current.push_back(ch);
  }

  flush();
  return names;
}

}
