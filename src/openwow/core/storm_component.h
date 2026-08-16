
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::core {

using ComponentShutdownFn = std::function<void()>;

namespace BlizzardComponents {
    inline constexpr const char* kThread              = "Blizzard::Thread";
    inline constexpr const char* kMemory              = "Blizzard::Memory";
    inline constexpr const char* kThreadLocalStorage  = "Blizzard::ThreadLocalStorage";
    inline constexpr const char* kMopaq               = "Blizzard::Mopaq";
    inline constexpr const char* kFile                = "Blizzard::File";
    inline constexpr const char* kLog                 = "Blizzard::Log";
    inline constexpr const char* kDebug               = "Blizzard::Debug";
}

class BlizzardComponent {
public:
    static BlizzardComponent& Instance();

    void Register(const std::string& name, ComponentShutdownFn shutdownFn = nullptr);

    void AddDependency(const std::string& dependent, const std::string& dependency);

    void SetShutdownCallback(const std::string& name, ComponentShutdownFn fn);

    void Shutdown(const std::string& name);

    void ShutdownAll();

    bool IsRegistered(const std::string& name) const;

    std::int16_t GetRefCount(const std::string& name) const;

    std::vector<std::string> GetDependencies(const std::string& name) const;

    void InitThread();

    void ShutdownThread();

    void InitMopaq();

    void ShutdownMopaq();

    void InitLog();

    void ShutdownLog();

    void InitMemory();

private:
    BlizzardComponent() = default;

    struct HashSlot {
        std::uint32_t hash = 0;
        std::uint16_t componentId = 0;
    };

    struct ComponentRecord {
        std::string              name;
        std::int16_t             refCount = 0;
        ComponentShutdownFn      shutdownFn;
        std::vector<std::uint16_t> dependencies;
    };

    struct HashLookup {
        std::size_t slot = 0;
        std::uint32_t hash = 0;
        bool found = false;
    };

    static std::uint32_t HashName(std::string_view name);
    HashLookup FindHashSlotByNameLocked(std::string_view name) const;
    std::uint16_t GetOrCreateComponentIdLocked(std::string_view name);
    const ComponentRecord* FindComponentLocked(std::string_view name) const;
    ComponentRecord* FindComponentLocked(std::string_view name);
    void ProcessPendingShutdownsLocked();

    mutable std::recursive_mutex                mutex_;
    std::array<HashSlot, 64>                    hashSlots_{};
    std::vector<ComponentRecord>                components_;
};

}
