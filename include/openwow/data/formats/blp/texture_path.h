#pragma once

#include <algorithm>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace openwow::data::blp {

[[nodiscard]] inline std::string NormalizeTexturePath(std::string value) {
  std::replace(value.begin(), value.end(), '\\', '/');
  while (!value.empty() && value.front() == ' ') {
    value.erase(value.begin());
  }
  while (!value.empty() && value.back() == ' ') {
    value.pop_back();
  }
  if (value.empty()) {
    return {};
  }
  if (std::filesystem::path(value).extension().empty()) {
    value += ".blp";
  }
  return value;
}

[[nodiscard]] inline const std::string& ResolveNormalizedTexturePath(
    const std::string& path,
    std::unordered_map<std::string, std::string>& normalized_path_cache) {
  if (const auto it = normalized_path_cache.find(path);
      it != normalized_path_cache.end()) {
    return it->second;
  }
  auto [inserted, _] =
      normalized_path_cache.emplace(path, NormalizeTexturePath(path));
  return inserted->second;
}

}
