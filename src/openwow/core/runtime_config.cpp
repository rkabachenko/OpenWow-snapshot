
#include "runtime_config.h"

#include <cstdlib>
#include <stdexcept>

namespace openwow::core {

RuntimeConfig& RuntimeConfig::GetInstance() {
    static RuntimeConfig instance;
    return instance;
}

RuntimeConfig::RuntimeConfig() {
    SetDefaults();
}

void RuntimeConfig::SetDefaults() {
    m_buildInfo = BuildInfo{};

    for (int i = 0; i < static_cast<int>(FeatureFlag::COUNT); ++i)
        m_features[i] = false;

    m_features[static_cast<int>(FeatureFlag::VSync)]               = true;
    m_features[static_cast<int>(FeatureFlag::Shadows)]             = true;
    m_features[static_cast<int>(FeatureFlag::Particles)]           = true;
    m_features[static_cast<int>(FeatureFlag::WeatherEffects)]      = true;
    m_features[static_cast<int>(FeatureFlag::AddonSupport)]        = true;
    m_features[static_cast<int>(FeatureFlag::HardwareAcceleration)] = true;

    m_dataPath.clear();
    m_userPath.clear();
    m_logPath.clear();
    m_kvStore.clear();
}

void RuntimeConfig::Reset() {
    std::lock_guard lock(m_mutex);
    SetDefaults();
}

void RuntimeConfig::SetBuildInfo(const BuildInfo& info) {
    std::lock_guard lock(m_mutex);
    m_buildInfo = info;
}

BuildInfo RuntimeConfig::GetBuildInfo() const {
    std::lock_guard lock(m_mutex);
    return m_buildInfo;
}

void RuntimeConfig::SetFeatureEnabled(FeatureFlag flag, bool enabled) {
    std::lock_guard lock(m_mutex);
    int idx = static_cast<int>(flag);
    if (idx < 0 || idx >= static_cast<int>(FeatureFlag::COUNT)) return;
    m_features[idx] = enabled;
}

bool RuntimeConfig::IsFeatureEnabled(FeatureFlag flag) const {
    std::lock_guard lock(m_mutex);
    int idx = static_cast<int>(flag);
    if (idx < 0 || idx >= static_cast<int>(FeatureFlag::COUNT)) return false;
    return m_features[idx];
}

std::vector<FeatureFlag> RuntimeConfig::GetEnabledFeatures() const {
    std::lock_guard lock(m_mutex);
    std::vector<FeatureFlag> result;
    for (int i = 0; i < static_cast<int>(FeatureFlag::COUNT); ++i) {
        if (m_features[i])
            result.push_back(static_cast<FeatureFlag>(i));
    }
    return result;
}

void RuntimeConfig::SetDataPath(const std::string& path) {
    std::lock_guard lock(m_mutex);
    m_dataPath = path;
}

std::string RuntimeConfig::GetDataPath() const {
    std::lock_guard lock(m_mutex);
    return m_dataPath;
}

void RuntimeConfig::SetUserPath(const std::string& path) {
    std::lock_guard lock(m_mutex);
    m_userPath = path;
}

std::string RuntimeConfig::GetUserPath() const {
    std::lock_guard lock(m_mutex);
    return m_userPath;
}

void RuntimeConfig::SetLogPath(const std::string& path) {
    std::lock_guard lock(m_mutex);
    m_logPath = path;
}

std::string RuntimeConfig::GetLogPath() const {
    std::lock_guard lock(m_mutex);
    return m_logPath;
}

void RuntimeConfig::SetValue(const std::string& key, const std::string& value) {
    std::lock_guard lock(m_mutex);
    m_kvStore[key] = value;
}

std::optional<std::string> RuntimeConfig::GetValue(const std::string& key) const {
    std::lock_guard lock(m_mutex);
    auto it = m_kvStore.find(key);
    if (it == m_kvStore.end()) return std::nullopt;
    return it->second;
}

std::string RuntimeConfig::GetValueOr(const std::string& key, const std::string& defaultVal) const {
    std::lock_guard lock(m_mutex);
    auto it = m_kvStore.find(key);
    if (it == m_kvStore.end()) return defaultVal;
    return it->second;
}

std::optional<int32_t> RuntimeConfig::GetIntValue(const std::string& key) const {
    std::lock_guard lock(m_mutex);
    auto it = m_kvStore.find(key);
    if (it == m_kvStore.end()) return std::nullopt;
    try {
        return static_cast<int32_t>(std::stoi(it->second));
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<float> RuntimeConfig::GetFloatValue(const std::string& key) const {
    std::lock_guard lock(m_mutex);
    auto it = m_kvStore.find(key);
    if (it == m_kvStore.end()) return std::nullopt;
    try {
        return std::stof(it->second);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<bool> RuntimeConfig::GetBoolValue(const std::string& key) const {
    std::lock_guard lock(m_mutex);
    auto it = m_kvStore.find(key);
    if (it == m_kvStore.end()) return std::nullopt;
    const auto& v = it->second;
    if (v == "1" || v == "true" || v == "True" || v == "TRUE" || v == "yes" || v == "on")
        return true;
    if (v == "0" || v == "false" || v == "False" || v == "FALSE" || v == "no" || v == "off")
        return false;
    return std::nullopt;
}

std::vector<std::string> RuntimeConfig::GetAllKeys() const {
    std::lock_guard lock(m_mutex);
    std::vector<std::string> keys;
    keys.reserve(m_kvStore.size());
    for (auto& [k, _] : m_kvStore)
        keys.push_back(k);
    return keys;
}

}
