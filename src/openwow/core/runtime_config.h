#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::core {

enum class FeatureFlag : uint8_t {
    VSync               = 0,
    TripleBuffering     = 1,
    MSAA                = 2,
    Shadows             = 3,
    Particles           = 4,
    WeatherEffects      = 5,
    Phasing             = 6,
    AddonSupport        = 7,
    VoiceChat           = 8,
    Streaming           = 9,
    HardwareAcceleration = 10,

    COUNT               = 11
};

struct BuildInfo {
    uint16_t    majorVersion = 3;
    uint16_t    minorVersion = 3;
    uint16_t    patchVersion = 5;
    uint32_t    buildNumber  = 12340;
    std::string buildDate    = "Feb 28 2026";
    std::string gitHash;
};

class RuntimeConfig {
public:

    static RuntimeConfig& GetInstance();

    RuntimeConfig(const RuntimeConfig&) = delete;
    RuntimeConfig& operator=(const RuntimeConfig&) = delete;

    void            SetBuildInfo(const BuildInfo& info);
    [[nodiscard]] BuildInfo GetBuildInfo() const;

    void            SetFeatureEnabled(FeatureFlag flag, bool enabled);
    [[nodiscard]] bool IsFeatureEnabled(FeatureFlag flag) const;
    [[nodiscard]] std::vector<FeatureFlag> GetEnabledFeatures() const;

    void            SetDataPath(const std::string& path);
    [[nodiscard]] std::string GetDataPath() const;

    void            SetUserPath(const std::string& path);
    [[nodiscard]] std::string GetUserPath() const;

    void            SetLogPath(const std::string& path);
    [[nodiscard]] std::string GetLogPath() const;

    void            SetValue(const std::string& key, const std::string& value);
    [[nodiscard]] std::optional<std::string> GetValue(const std::string& key) const;
    [[nodiscard]] std::string GetValueOr(const std::string& key, const std::string& defaultVal) const;
    [[nodiscard]] std::optional<int32_t> GetIntValue(const std::string& key) const;
    [[nodiscard]] std::optional<float>   GetFloatValue(const std::string& key) const;
    [[nodiscard]] std::optional<bool>    GetBoolValue(const std::string& key) const;
    [[nodiscard]] std::vector<std::string> GetAllKeys() const;

    void Reset();

private:
    RuntimeConfig();
    ~RuntimeConfig() = default;

    void SetDefaults();

    mutable std::mutex m_mutex;

    BuildInfo   m_buildInfo;
    bool        m_features[static_cast<int>(FeatureFlag::COUNT)]{};

    std::string m_dataPath;
    std::string m_userPath;
    std::string m_logPath;

    std::unordered_map<std::string, std::string> m_kvStore;
};

}
