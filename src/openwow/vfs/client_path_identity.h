#pragma once

#include "openwow/foundation/text/ascii.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace openwow::vfs {

struct ClientPathIdentity {
  std::string display_path;
  std::string lookup_path;

  [[nodiscard]] bool empty() const noexcept { return display_path.empty(); }
};

[[nodiscard]] inline std::string NormalizeClientDisplayPath(
    const std::string_view raw_path) {
  std::string path = openwow::text::Trim(std::string(raw_path));
  if (path.empty()) {
    return path;
  }

  std::replace(path.begin(), path.end(), '\\', '/');
  if (path.front() != '/') {
    path.insert(path.begin(), '/');
  }

  std::vector<std::string> parts;
  parts.reserve(16);
  std::string current;
  for (std::size_t index = 1; index <= path.size(); ++index) {
    const char character = index < path.size() ? path[index] : '/';
    if (character != '/') {
      current.push_back(character);
      continue;
    }
    if (current.empty() || current == ".") {
      current.clear();
      continue;
    }
    if (current == "..") {
      if (!parts.empty()) {
        parts.pop_back();
      }
      current.clear();
      continue;
    }
    parts.push_back(std::move(current));
    current.clear();
  }

  std::string display_path = "/";
  for (std::size_t index = 0; index < parts.size(); ++index) {
    if (index != 0) {
      display_path.push_back('/');
    }
    display_path += parts[index];
  }

  return display_path;
}

[[nodiscard]] inline std::string NormalizeClientLookupPath(
    const std::string_view raw_path) {
  return openwow::text::ToLowerAscii(NormalizeClientDisplayPath(raw_path));
}

[[nodiscard]] inline ClientPathIdentity MakeClientPathIdentity(
    const std::string_view raw_path) {
  std::string display_path = NormalizeClientDisplayPath(raw_path);
  std::string lookup_path = openwow::text::ToLowerAscii(display_path);
  return {
      .display_path = std::move(display_path),
      .lookup_path = std::move(lookup_path),
  };
}

[[nodiscard]] inline ClientPathIdentity ResolveClientPathIdentity(
    const std::string_view authored_path,
    const std::string_view display_base_directory) {
  std::string path = openwow::text::Trim(std::string(authored_path));
  if (path.empty()) {
    return {};
  }
  if (path.front() != '/' && path.front() != '\\' &&
      !display_base_directory.empty()) {
    std::string joined(display_base_directory);
    if (joined.back() != '/' && joined.back() != '\\') {
      joined.push_back('/');
    }
    joined += path;
    path = std::move(joined);
  }
  return MakeClientPathIdentity(path);
}

[[nodiscard]] inline std::string BuildClientLuaChunkName(
    const std::string_view display_path) {
  std::string source_path = NormalizeClientDisplayPath(display_path);
  while (!source_path.empty() && source_path.front() == '/') {
    source_path.erase(source_path.begin());
  }
  std::replace(source_path.begin(), source_path.end(), '/', '\\');

  constexpr std::size_t kMaxRetailPathLength = 259;
  if (source_path.size() > kMaxRetailPathLength) {
    source_path.resize(kMaxRetailPathLength);
  }
  source_path.insert(source_path.begin(), '@');
  return source_path;
}

}
