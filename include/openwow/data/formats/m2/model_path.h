#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <utility>

namespace openwow::data::m2 {

[[nodiscard]] inline std::string NormalizeModelPath(std::string value) {
  std::replace(value.begin(), value.end(), '\\', '/');

  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.front())) != 0) {
    value.erase(value.begin());
  }
  while (!value.empty()) {
    const auto ch = static_cast<unsigned char>(value.back());
    if (ch != 0 && std::isspace(ch) == 0) {
      break;
    }
    value.pop_back();
  }
  if (value.empty()) {
    return {};
  }

  std::string normalized;
  normalized.reserve(value.size());
  for (const char ch : value) {
    if (ch != '/' || normalized.empty() || normalized.back() != '/') {
      normalized.push_back(ch);
    }
  }
  value = std::move(normalized);

  auto path = std::filesystem::path(value);
  std::string extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](const unsigned char ch) {
                   return static_cast<char>(std::tolower(ch));
                 });
  if (extension == ".mdx" || extension == ".mdl") {
    return path.replace_extension(".m2").generic_string();
  }
  if (extension.empty()) {
    value += ".m2";
  }
  return value;
}

}
