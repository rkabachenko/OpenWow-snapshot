#pragma once

#include <filesystem>

namespace openwow::storage::persistence {

std::filesystem::path GetDefaultProfileRoot();
std::filesystem::path GetConfigPath();

}
