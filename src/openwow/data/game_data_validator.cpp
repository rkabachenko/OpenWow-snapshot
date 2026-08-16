
#include "openwow/data/game_data_validator.h"

#include <algorithm>
#include <filesystem>

namespace openwow::data {

namespace {

bool FileExistsCI(const std::filesystem::path& dir, const std::string& name) {
  std::error_code ec;

  if (std::filesystem::exists(dir / name, ec)) return true;

  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    auto fn = entry.path().filename().string();
    if (fn.size() != name.size()) continue;
    bool match = true;
    for (size_t i = 0; i < fn.size(); ++i) {
      if (std::tolower(static_cast<unsigned char>(fn[i])) !=
          std::tolower(static_cast<unsigned char>(name[i]))) {
        match = false;
        break;
      }
    }
    if (match) return true;
  }
  return false;
}

bool HasMpqFiles(const std::filesystem::path& data_dir) {
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(data_dir, ec)) {
    auto ext = entry.path().extension().string();

    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (ext == ".mpq") return true;
  }
  return false;
}

}

ValidationResult ValidateWotlkGameDataPath(const std::string& path) {
  ValidationResult result;
  std::error_code ec;
  const auto root = std::filesystem::path(path);

  if (path.empty() || !std::filesystem::exists(root, ec) ||
      !std::filesystem::is_directory(root, ec)) {
    result.errors.emplace_back("Path does not exist or is not a directory.");
    return result;
  }

  const auto data_dir = root / "Data";
  if (!std::filesystem::exists(data_dir, ec) ||
      !std::filesystem::is_directory(data_dir, ec)) {
    result.errors.emplace_back("Missing Data directory.");
  } else {

    if (!HasMpqFiles(data_dir)) {
      result.errors.emplace_back(
          "Data directory contains no .MPQ files. "
          "This does not look like a WotLK installation.");
    }

    bool has_locale = false;
    const char* locales[] = {
        "enUS", "enGB", "deDE", "esES", "esMX",
        "frFR", "koKR", "ruRU", "zhCN", "zhTW",
    };
    for (const char* loc : locales) {
      auto locale_dir = data_dir / loc;
      if (std::filesystem::exists(locale_dir, ec) &&
          std::filesystem::is_directory(locale_dir, ec)) {
        has_locale = true;
        break;
      }
    }
    if (!has_locale) {
      result.errors.emplace_back(
          "No locale directory found in Data/ (e.g., enUS, deDE).");
    }
  }

  if (!FileExistsCI(root, "Wow.exe") && !FileExistsCI(root, "wow.exe")) {

    if (!FileExistsCI(root, "Wow") && !FileExistsCI(root, "wow")) {
      result.errors.emplace_back("Missing Wow.exe (or Wow binary).");
    }
  }

  const auto interface_dir = root / "Interface";
  if (!std::filesystem::exists(interface_dir, ec)) {

    result.errors.emplace_back(
        "[Warning] Interface directory not found. "
        "AddOns and custom UI elements may not work.");
  }

  const auto wtf_dir = root / "WTF";
  if (!std::filesystem::exists(wtf_dir, ec)) {
    result.errors.emplace_back(
        "[Warning] WTF directory not found. "
        "Config and save data will be created on first run.");
  }

  auto realmlist = data_dir / "realmlist.wtf";
  if (std::filesystem::exists(data_dir, ec) &&
      !std::filesystem::exists(realmlist, ec)) {

    realmlist = root / "realmlist.wtf";
    if (!std::filesystem::exists(realmlist, ec)) {
      result.errors.emplace_back(
          "[Warning] realmlist.wtf not found in Data/ or root. "
          "Server address will need to be configured.");
    }
  }

  bool has_real_errors = false;
  for (const auto& e : result.errors) {
    if (e.find("[Warning]") == std::string::npos) {
      has_real_errors = true;
      break;
    }
  }

  result.ok = !has_real_errors;
  return result;
}

}
