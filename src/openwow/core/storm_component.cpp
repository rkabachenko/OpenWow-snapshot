
#include "storm_component.h"
#include "openwow/data/streaming_init.h"
#include "legacy_buffered_log_file.h"
#include "storm_memory.h"
#include "storm_thread.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace openwow::core {

namespace {

constexpr std::size_t kComponentHashSlotCount = 64;
constexpr std::size_t kMaxComponentCount =
    static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()) + 1u;

std::int16_t IncrementRetailCount(const std::int16_t count) noexcept {
    const auto bits =
        (static_cast<std::uint32_t>(static_cast<std::uint16_t>(count)) + 1u) & 0xFFFFu;
    return bits < 0x8000u
        ? static_cast<std::int16_t>(bits)
        : static_cast<std::int16_t>(static_cast<std::int32_t>(bits) - 0x10000);
}

std::int16_t DecrementRetailCount(const std::int16_t count) noexcept {
    const auto bits =
        (static_cast<std::uint32_t>(static_cast<std::uint16_t>(count)) - 1u) & 0xFFFFu;
    return bits < 0x8000u
        ? static_cast<std::int16_t>(bits)
        : static_cast<std::int16_t>(static_cast<std::int32_t>(bits) - 0x10000);
}

}

BlizzardComponent& BlizzardComponent::Instance() {
    static BlizzardComponent inst;
    return inst;
}

std::uint32_t BlizzardComponent::HashName(std::string_view name) {
    std::uint32_t hash = 16777619u;
    for (const unsigned char ch : name) {
        hash = 16777619u * (hash ^ ch);
    }

    return hash == 0 ? 1u : hash;
}

BlizzardComponent::HashLookup BlizzardComponent::FindHashSlotByNameLocked(std::string_view name) const {
    const std::uint32_t hash = HashName(name);
    std::size_t slot = hash & (kComponentHashSlotCount - 1u);

    for (std::size_t probe = 0; probe < hashSlots_.size(); ++probe) {
        const HashSlot& entry = hashSlots_[slot];
        if (entry.hash == 0) {
            return {.slot = slot, .hash = hash, .found = false};
        }
        if (entry.hash == hash) {
            return {.slot = slot, .hash = hash, .found = true};
        }

        slot = (slot + 1u) & (kComponentHashSlotCount - 1u);
    }

    throw std::runtime_error("BlizzardComponent hash table overflow");
}

std::uint16_t BlizzardComponent::GetOrCreateComponentIdLocked(std::string_view name) {
    const HashLookup lookup = FindHashSlotByNameLocked(name);
    if (lookup.found) {
        return hashSlots_[lookup.slot].componentId;
    }

    if (components_.size() >= kMaxComponentCount) {
        throw std::runtime_error("BlizzardComponent id space exhausted");
    }

    const auto componentId = static_cast<std::uint16_t>(components_.size());
    components_.push_back(ComponentRecord{.name = std::string(name)});
    hashSlots_[lookup.slot] = HashSlot{.hash = lookup.hash, .componentId = componentId};
    return componentId;
}

const BlizzardComponent::ComponentRecord* BlizzardComponent::FindComponentLocked(std::string_view name) const {
    const HashLookup lookup = FindHashSlotByNameLocked(name);
    if (!lookup.found) {
        return nullptr;
    }

    return &components_[hashSlots_[lookup.slot].componentId];
}

BlizzardComponent::ComponentRecord* BlizzardComponent::FindComponentLocked(std::string_view name) {
    return const_cast<ComponentRecord*>(
        std::as_const(*this).FindComponentLocked(name));
}

void BlizzardComponent::ProcessPendingShutdownsLocked() {
    for (;;) {
        bool shutdownExecuted = false;

        for (const HashSlot& slot : hashSlots_) {
            if (slot.hash == 0) {
                continue;
            }

            ComponentRecord& component = components_[slot.componentId];
            if (!component.shutdownFn || component.refCount > 0) {
                continue;
            }

            component.shutdownFn();
            component.shutdownFn = nullptr;

            for (const std::uint16_t dependencyId : component.dependencies) {
                ComponentRecord& dependency = components_[dependencyId];
                dependency.refCount = DecrementRetailCount(dependency.refCount);
            }
            component.dependencies.clear();

            shutdownExecuted = true;
        }

        if (!shutdownExecuted) {
            return;
        }
    }
}

void BlizzardComponent::Register(const std::string& name, ComponentShutdownFn shutdownFn) {
    std::lock_guard lock(mutex_);
    ComponentRecord& component = components_[GetOrCreateComponentIdLocked(name)];
    component.refCount = IncrementRetailCount(component.refCount);
    if (shutdownFn) {
        component.shutdownFn = std::move(shutdownFn);
    }
}

void BlizzardComponent::AddDependency(const std::string& dependent, const std::string& dependency) {
    std::lock_guard lock(mutex_);

    const std::uint16_t dependentId = GetOrCreateComponentIdLocked(dependent);
    const std::uint16_t dependencyId = GetOrCreateComponentIdLocked(dependency);
    ComponentRecord& component = components_[dependentId];

    if (std::find(component.dependencies.begin(), component.dependencies.end(), dependencyId) !=
        component.dependencies.end()) {
        return;
    }

    component.dependencies.push_back(dependencyId);
    ComponentRecord& dependencyRecord = components_[dependencyId];
    dependencyRecord.refCount = IncrementRetailCount(dependencyRecord.refCount);
}

void BlizzardComponent::SetShutdownCallback(const std::string& name, ComponentShutdownFn fn) {
    std::lock_guard lock(mutex_);
    components_[GetOrCreateComponentIdLocked(name)].shutdownFn = std::move(fn);
}

void BlizzardComponent::Shutdown(const std::string& name) {
    std::lock_guard lock(mutex_);
    ComponentRecord& component = components_[GetOrCreateComponentIdLocked(name)];
    component.refCount = DecrementRetailCount(component.refCount);
    ProcessPendingShutdownsLocked();
}

void BlizzardComponent::ShutdownAll() {
    std::lock_guard lock(mutex_);

    for (ComponentRecord& component : components_) {
        component.refCount = 0;
    }

    ProcessPendingShutdownsLocked();
    hashSlots_.fill({});
    components_.clear();
}

bool BlizzardComponent::IsRegistered(const std::string& name) const {
    std::lock_guard lock(mutex_);
    const ComponentRecord* component = FindComponentLocked(name);
    return component != nullptr && component->refCount > 0;
}

std::int16_t BlizzardComponent::GetRefCount(const std::string& name) const {
    std::lock_guard lock(mutex_);
    const ComponentRecord* component = FindComponentLocked(name);
    return component != nullptr ? component->refCount : 0;
}

std::vector<std::string> BlizzardComponent::GetDependencies(const std::string& name) const {
    std::lock_guard lock(mutex_);
    const ComponentRecord* component = FindComponentLocked(name);
    if (!component) {
        return {};
    }

    std::vector<std::string> dependencies;
    dependencies.reserve(component->dependencies.size());
    for (const std::uint16_t dependencyId : component->dependencies) {
        dependencies.push_back(components_[dependencyId].name);
    }
    return dependencies;
}

void BlizzardComponent::InitThread() {
    Register(BlizzardComponents::kThread, [this]() { ShutdownThread(); });

    SetShutdownCallback(BlizzardComponents::kThreadLocalStorage, []() {});
    AddDependency(BlizzardComponents::kThread, BlizzardComponents::kMemory);
    AddDependency(BlizzardComponents::kThreadLocalStorage, BlizzardComponents::kThread);
    StormMemory::Instance().Init();
}

void BlizzardComponent::ShutdownThread() {
    StormThread::Instance().ShutdownPrimaryThread();
    StormThread::Instance().ShutdownSingletonWorkerPool();
}

void BlizzardComponent::InitMopaq() {
    Register(BlizzardComponents::kMopaq, [this]() { ShutdownMopaq(); });
    AddDependency(BlizzardComponents::kMopaq, BlizzardComponents::kMemory);
    AddDependency(BlizzardComponents::kMopaq, BlizzardComponents::kFile);
    AddDependency(BlizzardComponents::kMopaq, BlizzardComponents::kThreadLocalStorage);
    AddDependency(BlizzardComponents::kThreadLocalStorage, BlizzardComponents::kMemory);
}

void BlizzardComponent::ShutdownMopaq() {
    openwow::data::ShutdownStreaming();
}

void BlizzardComponent::InitLog() {
    Register(BlizzardComponents::kLog, [this]() { ShutdownLog(); });
    AddDependency(BlizzardComponents::kLog, BlizzardComponents::kFile);
}

void BlizzardComponent::ShutdownLog() {
    LegacyBufferedLogFile::ShutdownAll();
}

void BlizzardComponent::InitMemory() {
    Register(BlizzardComponents::kMemory, []() {});
}

}
