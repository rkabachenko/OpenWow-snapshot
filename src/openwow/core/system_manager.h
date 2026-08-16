#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace openwow::core {

using SystemUpdateFn = std::function<void(float dt)>;
using SystemPriority = std::int32_t;

struct SystemInfo {
    std::string name;
    SystemPriority priority{0};
    bool enabled{true};
    std::uint64_t lastUpdateTimeNs{0};
    float avgUpdateMs{0.0f};
};

class SystemManager {
public:

    bool RegisterSystem(const std::string& name, SystemUpdateFn fn, SystemPriority priority);

    bool UnregisterSystem(const std::string& name);

    void EnableSystem(const std::string& name, bool enabled);
    [[nodiscard]] bool IsEnabled(const std::string& name) const;

    void UpdateAll(float dt);

    [[nodiscard]] std::optional<SystemInfo> GetSystemInfo(const std::string& name) const;
    [[nodiscard]] std::vector<SystemInfo> GetAllSystems() const;
    [[nodiscard]] std::size_t GetSystemCount() const;

    [[nodiscard]] float GetTotalUpdateTimeMs() const;

    void ClearAll();

private:
    struct SystemEntry {
        SystemInfo info;
        SystemUpdateFn fn;
    };

    mutable std::vector<SystemEntry> systems_;
    float totalUpdateTimeMs_{0.0f};
    mutable bool sorted_{true};

    void EnsureSorted() const;
    SystemEntry* FindEntry(const std::string& name);
    const SystemEntry* FindEntry(const std::string& name) const;
};

}
