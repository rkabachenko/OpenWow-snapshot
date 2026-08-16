#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::platform::filesystem {

bool AtomicWriteFile(const std::filesystem::path& path, const std::string& content);
[[nodiscard]] std::optional<std::vector<std::byte>> ReadFileBytes(
    const std::filesystem::path& path);

bool IsSafePathComponent(std::string_view component);

std::optional<std::string> ResolveExistingPathComponentCaseInsensitive(
    const std::filesystem::path& parent, std::string_view requested);
bool CopyFilePath(const std::filesystem::path& source,
                  const std::filesystem::path& destination,
                  bool overwrite);
bool PathIsRegularFile(const std::filesystem::path& path);
bool MovePathNoReplace(const std::filesystem::path& source,
                       const std::filesystem::path& destination);
bool RecursiveCopyDirectory(const std::filesystem::path& source,
                            const std::filesystem::path& destination,
                            bool overwrite);
bool IsSafeChildPath(const std::filesystem::path& root, const std::filesystem::path& candidate);

}
