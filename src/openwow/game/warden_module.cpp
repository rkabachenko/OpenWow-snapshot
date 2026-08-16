
#include "openwow/game/warden_module.h"
#include "openwow/platform/system/os_system_info.h"
#include "openwow/data/db_cache_instances.h"
#include "openwow/data/wdb_persistence.h"
#include "openwow/net/wotlk/realm_connection.h"
#include "openwow/render/platform/renderer_backend_selection.h"
#include "openwow/render/resources/textures/texture_cache_budget.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace openwow::game {

namespace {

constexpr std::uint32_t kWardenCacheBuild = 12340;
constexpr std::uint32_t kWardenCacheRecordSize = 8;
constexpr std::string_view kWardenCacheFilename = "wowcache.wdb";

using WardenModuleId = std::array<std::uint8_t, 16>;

[[nodiscard]] std::uint32_t HashWardenModuleKey(const std::uint8_t key[16]) {
    return (static_cast<std::uint32_t>(key[12] & 0x7Fu) << 24) |
           (static_cast<std::uint32_t>(key[13]) << 16) |
           (static_cast<std::uint32_t>(key[14]) << 8) |
           static_cast<std::uint32_t>(key[15]);
}

struct WardenModuleIdHash {
    std::size_t operator()(const WardenModuleId& module_id) const noexcept {
        return HashWardenModuleKey(module_id.data());
    }
};

struct WardenModuleCacheStore {
    std::unordered_map<WardenModuleId, std::vector<std::uint8_t>, WardenModuleIdHash> modules;
    std::vector<WardenModuleId> persistence_order;
    std::filesystem::path cache_dir;
    std::uint32_t locale = 0;
    std::uint32_t format_version = 0;
    std::uint32_t cache_version_token = 0;
    bool loaded = false;
    bool dirty = false;
    std::mutex mutex;
};

[[nodiscard]] WardenModuleId ToWardenModuleId(const std::uint8_t key[16]) {
    WardenModuleId module_id{};
    std::memcpy(module_id.data(), key, module_id.size());
    return module_id;
}

void AppendU32LE(std::vector<std::uint8_t>& out, const std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFu));
}

[[nodiscard]] bool ReadU32LE(const std::vector<std::uint8_t>& data,
                             std::size_t& pos,
                             std::uint32_t& value) {
    if (pos + 4 > data.size()) {
        return false;
    }

    value = static_cast<std::uint32_t>(data[pos]) |
            (static_cast<std::uint32_t>(data[pos + 1]) << 8) |
            (static_cast<std::uint32_t>(data[pos + 2]) << 16) |
            (static_cast<std::uint32_t>(data[pos + 3]) << 24);
    pos += 4;
    return true;
}

[[nodiscard]] std::uint32_t ResolveWardenCacheVersion() {
    const auto backend =
        openwow::render::TextureCacheBackendForRenderer(
            openwow::render::ResolveRendererBackend(
                openwow::render::ParseRendererBackend(
                    openwow::ui::game::CVarSystem::Instance().GetCVar("gxApi"))));
    return backend == openwow::render::TextureCacheBackendClass::kOpenGl ? 0u : 2u;
}

[[nodiscard]] WardenModuleCacheStore& GetWardenModuleCacheStore() {
    static WardenModuleCacheStore store;
    return store;
}

void ResetWardenModuleCacheStoreLocked(WardenModuleCacheStore& store,
                                       const std::filesystem::path& cache_dir,
                                       const std::uint32_t locale,
                                       const std::uint32_t format_version,
                                       const std::uint32_t cache_version_token = 0) {
    store.modules.clear();
    store.persistence_order.clear();
    store.cache_dir = cache_dir;
    store.locale = locale;
    store.format_version = format_version;
    store.cache_version_token = cache_version_token;
    store.loaded = true;
    store.dirty = false;
}

void MarkWardenModuleCacheLoadedLocked(WardenModuleCacheStore& store,
                                       const std::filesystem::path& cache_dir,
                                       const std::uint32_t locale,
                                       const std::uint32_t format_version) {
    store.cache_dir = cache_dir;
    store.locale = locale;
    store.format_version = format_version;
    store.loaded = true;
}

[[nodiscard]] bool LoadWardenModuleCacheFromDiskLocked(WardenModuleCacheStore& store,
                                                       const std::filesystem::path& cache_dir,
                                                       const std::uint32_t locale,
                                                       const std::uint32_t format_version) {
    if (cache_dir.empty()) {
        ResetWardenModuleCacheStoreLocked(store, cache_dir, locale, format_version);
        return true;
    }

    const auto path = cache_dir / kWardenCacheFilename;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        ResetWardenModuleCacheStoreLocked(store, cache_dir, locale, format_version);
        return true;
    }

    const auto end_pos = file.tellg();
    if (end_pos < 0) {
        MarkWardenModuleCacheLoadedLocked(store, cache_dir, locale, format_version);
        return false;
    }
    const auto file_size =
        static_cast<std::uintmax_t>(static_cast<std::streamoff>(end_pos));
    if (file_size > std::numeric_limits<std::size_t>::max() ||
        file_size > static_cast<std::uintmax_t>(
                        std::numeric_limits<std::streamsize>::max())) {
        MarkWardenModuleCacheLoadedLocked(store, cache_dir, locale, format_version);
        return false;
    }
    file.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> data(static_cast<std::size_t>(file_size));
    if (!data.empty() &&
        !file.read(reinterpret_cast<char*>(data.data()),
                   static_cast<std::streamsize>(data.size()))) {
        MarkWardenModuleCacheLoadedLocked(store, cache_dir, locale, format_version);
        return false;
    }

    if (data.size() < 24) {
        MarkWardenModuleCacheLoadedLocked(store, cache_dir, locale, format_version);
        return false;
    }

    std::size_t pos = 0;
    if (std::memcmp(data.data(), openwow::data::wdb_magic::kWarden,
                    sizeof(openwow::data::wdb_magic::kWarden)) != 0) {
        MarkWardenModuleCacheLoadedLocked(store, cache_dir, locale, format_version);
        return false;
    }
    pos += sizeof(openwow::data::wdb_magic::kWarden);

    std::uint32_t build = 0;
    std::uint32_t file_locale = 0;
    std::uint32_t record_size = 0;
    std::uint32_t file_version = 0;
    std::uint32_t cache_version_token = 0;
    if (!ReadU32LE(data, pos, build) || !ReadU32LE(data, pos, file_locale) ||
        !ReadU32LE(data, pos, record_size) || !ReadU32LE(data, pos, file_version) ||
        !ReadU32LE(data, pos, cache_version_token)) {
        MarkWardenModuleCacheLoadedLocked(store, cache_dir, locale, format_version);
        return false;
    }

    if (build != kWardenCacheBuild || file_locale != locale ||
        record_size != kWardenCacheRecordSize || file_version != format_version) {
        MarkWardenModuleCacheLoadedLocked(store, cache_dir, locale, format_version);
        return false;
    }

    std::unordered_map<WardenModuleId, std::vector<std::uint8_t>, WardenModuleIdHash> loaded_modules;
    std::vector<WardenModuleId> loaded_order;

    while (pos + 20 <= data.size()) {
        WardenModuleId module_id{};
        std::memcpy(module_id.data(), data.data() + pos, module_id.size());
        pos += module_id.size();

        std::uint32_t payload_size = 0;
        if (!ReadU32LE(data, pos, payload_size)) {
            loaded_modules.clear();
            loaded_order.clear();
            break;
        }

        const bool zero_key = std::all_of(
            module_id.begin(), module_id.end(),
            [](const std::uint8_t byte) { return byte == 0; });
        if (zero_key) {
            for (const auto& module_key : loaded_order) {
                auto loaded_it = loaded_modules.find(module_key);
                if (loaded_it == loaded_modules.end()) {
                    continue;
                }
                auto existing = store.modules.find(module_key);
                if (existing != store.modules.end()) {
                    existing->second = std::move(loaded_it->second);
                    continue;
                }
                store.modules.emplace(module_key, std::move(loaded_it->second));
                store.persistence_order.insert(store.persistence_order.begin(),
                                               module_key);
            }
            MarkWardenModuleCacheLoadedLocked(store, cache_dir, locale,
                                              format_version);
            store.cache_version_token = cache_version_token;
            return true;
        }

        if (payload_size > data.size() - pos || payload_size < 4) {
            loaded_modules.clear();
            loaded_order.clear();
            break;
        }

        std::size_t payload_pos = pos;
        std::uint32_t module_size = 0;
        if (!ReadU32LE(data, payload_pos, module_size) ||
            module_size != payload_size - 4) {
            loaded_modules.clear();
            loaded_order.clear();
            break;
        }

        std::vector<std::uint8_t> module_data(module_size);
        if (module_size != 0) {
            std::memcpy(module_data.data(), data.data() + payload_pos, module_size);
        }
        pos += payload_size;

        const bool first_occurrence =
            loaded_modules.find(module_id) == loaded_modules.end();
        loaded_modules[module_id] = std::move(module_data);
        if (first_occurrence) {

            loaded_order.push_back(module_id);
        }
    }

    ResetWardenModuleCacheStoreLocked(store, cache_dir, locale, format_version,
                                      cache_version_token);
    return false;
}

void LoadWardenModuleCacheLocked(WardenModuleCacheStore& store,
                                 const std::filesystem::path& cache_dir,
                                 const std::uint32_t locale) {
    const auto format_version = ResolveWardenCacheVersion();
    if (store.loaded && store.cache_dir == cache_dir && store.locale == locale &&
        store.format_version == format_version) {
        return;
    }

    (void)LoadWardenModuleCacheFromDiskLocked(
        store, cache_dir, locale, format_version);
}

void PersistWardenModuleCacheLocked(const WardenModuleCacheStore& store) {
    if (store.cache_dir.empty()) {
        return;
    }

    std::error_code ec;
    std::filesystem::create_directories(store.cache_dir, ec);
    if (ec) {
        return;
    }

    std::vector<std::uint8_t> payload;
    payload.reserve(24 + store.modules.size() * 32);
    payload.insert(payload.end(), std::begin(openwow::data::wdb_magic::kWarden),
                   std::end(openwow::data::wdb_magic::kWarden));
    AppendU32LE(payload, kWardenCacheBuild);
    AppendU32LE(payload, store.locale);
    AppendU32LE(payload, kWardenCacheRecordSize);
    AppendU32LE(payload, store.format_version);
    AppendU32LE(payload, store.cache_version_token);

    for (const auto& module_id : store.persistence_order) {
        const auto module_it = store.modules.find(module_id);
        if (module_it == store.modules.end()) {
            continue;
        }

        payload.insert(payload.end(), module_id.begin(), module_id.end());
        AppendU32LE(payload, static_cast<std::uint32_t>(4u + module_it->second.size()));
        AppendU32LE(payload, static_cast<std::uint32_t>(module_it->second.size()));
        payload.insert(payload.end(), module_it->second.begin(), module_it->second.end());
    }

    payload.insert(payload.end(), 16, 0);
    AppendU32LE(payload, 0);

    const auto path = store.cache_dir / kWardenCacheFilename;
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        return;
    }

    file.write(reinterpret_cast<const char*>(payload.data()),
               static_cast<std::streamsize>(payload.size()));
}

int QueryProcessorCount(WardenModuleState& state) {
    if (state.query_processor_count) {
        const int processor_count = state.query_processor_count();
        return processor_count > 0 ? processor_count : 1;
    }

    auto& detector = openwow::core::OsSystemInfoDetector::Instance();
    detector.Init();
    const auto processor_count = detector.GetInfo().processorCount;
    return processor_count == 0 ? 1 : static_cast<int>(processor_count);
}

}

void WardenClient_WriteKey(void* data_store, const uint8_t key[16]) {

    (void)data_store;
    (void)key;
}

void WardenClient_ReadKey(void* data_store, uint8_t key[16]) {
    (void)data_store;
    std::memset(key, 0, 16);
}

void WardenClient_FreeModuleData(WardenModuleData* mod) {
    if (mod->data) {
        std::free(mod->data);
    }
}

void WardenClient_WriteModuleData(const WardenModuleData* mod, void* data_store) {

    (void)data_store;
    (void)mod;
}

void WardenClient_ReadModuleData(WardenModuleData* mod, void* data_store) {
    (void)data_store;
    mod->size = 0;
    mod->data = nullptr;
}

void WardenClient_UnloadPendingModule(WardenModuleState& state) {
    if (state.pending_module) {

        state.pending_module = nullptr;
    }
}

void WardenClient_UnloadActiveModule(WardenModuleState& state) {
    if (state.active_module) {

        state.active_module = nullptr;
        state.module_vtable = 0;
    }
}

bool WardenClient_InitializeModule(WardenModuleState& state) {
    if (!state.pending_module)
        return false;

    WardenClient_UnloadActiveModule(state);

    state.active_module = state.pending_module;
    state.pending_module = nullptr;
    state.module_vtable = 0;

    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                       "WardenModule: initialized (stub)");
    return true;
}

void WardenClient_CheckProcessorCount(WardenModuleState& state) {
    const int new_count = QueryProcessorCount(state);
    const int delta = new_count - state.processor_count;
    state.processor_count = new_count;

    if (!state.active_module) {
        return;
    }

    if (state.on_processor_delta) {
        state.on_processor_delta(delta);
    }

    static_cast<void>(WardenClient_InitializeModule(state));
}

int InitGameSubsystems_WardenMaintenanceTick(
    WardenModuleState& state,
    const std::function<void()>& pre_maintenance) {
    if (pre_maintenance) {
        pre_maintenance();
    } else {
        openwow::net::wotlk::RealmConnection::DrainActiveQueuedEvents();
    }

    WardenClient_CheckProcessorCount(state);
    return 1;
}

void WardenClient_Shutdown(WardenModuleState& state) {
    WardenClient_UnloadPendingModule(state);
    WardenClient_UnloadActiveModule(state);

    if (state.packet_data) {
        std::free(state.packet_data);
        state.packet_data = nullptr;
    }
    state.packet_data_size = 0;

    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                       "WardenModule: shutdown");
}

void* WardenClient_ModuleAlloc(uint32_t size) {
    return std::malloc(size);
}

void WardenClient_ModuleFree(void* ptr) {
    std::free(ptr);
}

void WardenClient_SetPacketData(WardenModuleState& state,
                                 const void* data, uint32_t size) {
    if (state.packet_data) {
        std::free(state.packet_data);
        state.packet_data = nullptr;
    }

    if (size > 0 && data) {
        state.packet_data = static_cast<uint8_t*>(std::malloc(size));
        state.packet_data_size = size;
        std::memcpy(state.packet_data, data, size);
    } else {
        state.packet_data_size = 0;
    }
}

bool WardenClient_GetPacketData(WardenModuleState& state,
                                 void* out_data, uint32_t* inout_size) {
    if (!state.packet_data)
        return false;

    uint32_t copy_size = *inout_size;
    if (copy_size > state.packet_data_size)
        copy_size = state.packet_data_size;

    *inout_size = copy_size;
    std::memcpy(out_data, state.packet_data, copy_size);

    std::free(state.packet_data);
    state.packet_data = nullptr;
    state.packet_data_size = 0;

    return true;
}

void WardenClient_CopyModuleData(WardenModuleData* dst, const WardenModuleData* src) {
    if (!src->data) {
        dst->size = 0;
        dst->data = nullptr;
        return;
    }

    dst->size = src->size;
    dst->data = static_cast<uint8_t*>(std::malloc(src->size));
    std::memcpy(dst->data, src->data, src->size);
}

bool WardenModuleCache_Load(const uint8_t module_id[16], WardenModuleData& out) {
    auto& store = GetWardenModuleCacheStore();
    const std::lock_guard lock(store.mutex);

    out.size = 0;
    out.data = nullptr;

    if (!store.loaded || module_id == nullptr ||
        std::all_of(module_id, module_id + 16,
                    [](const std::uint8_t byte) { return byte == 0; })) {
        return false;
    }

    const auto it = store.modules.find(ToWardenModuleId(module_id));
    if (it == store.modules.end()) {
        return false;
    }

    out.size = static_cast<std::uint32_t>(it->second.size());
    if (out.size == 0) {
        return true;
    }

    out.data = static_cast<std::uint8_t*>(std::malloc(out.size));
    if (!out.data) {
        out.size = 0;
        return false;
    }

    std::memcpy(out.data, it->second.data(), out.size);
    return true;
}

void WardenModuleCache_Store(const uint8_t module_id[16], const void* data, uint32_t size) {
    auto& store = GetWardenModuleCacheStore();
    const std::lock_guard lock(store.mutex);

    if (!store.loaded || module_id == nullptr ||
        std::all_of(module_id, module_id + 16,
                    [](const std::uint8_t byte) { return byte == 0; })) {
        return;
    }

    const auto key = ToWardenModuleId(module_id);
    const auto effective_size = data == nullptr ? 0u : size;
    auto existing = store.modules.find(key);
    if (existing == store.modules.end()) {
        existing = store.modules.emplace(key, std::vector<std::uint8_t>{}).first;
        store.persistence_order.insert(store.persistence_order.begin(), key);
    }
    auto& module_data = existing->second;
    module_data.resize(effective_size);
    if (effective_size != 0) {
        std::memcpy(module_data.data(), data, effective_size);
    }
    store.dirty = true;

}

void WardenModuleCache_LoadStartup(
    const std::filesystem::path& cache_directory,
    const std::uint32_t locale) {
    auto& store = GetWardenModuleCacheStore();
    const std::lock_guard lock(store.mutex);
    LoadWardenModuleCacheLocked(store, cache_directory, locale);
}

void ResetWardenModuleCacheForTests() {
    auto& store = GetWardenModuleCacheStore();
    const std::lock_guard lock(store.mutex);
    store.modules.clear();
    store.persistence_order.clear();
    store.cache_dir.clear();
    store.locale = 0;
    store.format_version = 0;
    store.cache_version_token = 0;
    store.loaded = false;
    store.dirty = false;
}

void WardenModuleCache_Destroy() {
    auto& store = GetWardenModuleCacheStore();
    const std::lock_guard lock(store.mutex);

    if (store.loaded && store.dirty) {
        PersistWardenModuleCacheLocked(store);
    }

    store.modules.clear();
    store.persistence_order.clear();

    store.loaded = false;
    store.dirty = false;
}

void WardenModuleCache_ApplyClientCacheVersion(const std::uint32_t version) {
    auto& store = GetWardenModuleCacheStore();
    const std::lock_guard lock(store.mutex);

    if (!store.loaded) {
        return;
    }

    if (store.cache_version_token == version) {
        return;
    }

    store.modules.clear();
    store.persistence_order.clear();

    store.cache_version_token = version;
    store.dirty = true;
}

void* WardenClient_PrepareModule(uint32_t data_size, const void* data,
                                  const uint8_t rc4_key[16]) {
    if (data_size < 0x108)
        return nullptr;

    (void)data;
    (void)rc4_key;

    static uint8_t stub_module_handle = 1;
    return &stub_module_handle;
}

int WardenClient_HandlePacket(WardenModuleState& state,
                               uint32_t opcode, const void* data, uint32_t size) {
    if (opcode != 742)
        return 0;

    (void)state;
    (void)data;
    (void)size;

    return 1;
}

void WardenClient_Initialize(WardenModuleState& state) {
    WardenClient_Shutdown(state);

    state.pending_module = WardenClient_PrepareModule(0x12B3, nullptr, nullptr);
    WardenClient_InitializeModule(state);

    state.packet_data = nullptr;
    state.packet_data_size = 0;
    state.processor_count = QueryProcessorCount(state);

    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                       "WardenModule: initialized, opcode 742 registered");
}

bool WardenClient_LoadModuleFromCache(WardenModuleState& state,
                                       const uint8_t module_id[16],
                                       const uint8_t rc4_key[16]) {
    WardenModuleData cached_module{};
    if (!WardenModuleCache_Load(module_id, cached_module)) {
        return false;
    }

    WardenClient_UnloadPendingModule(state);
    state.pending_module =
        WardenClient_PrepareModule(cached_module.size, cached_module.data, rc4_key);
    WardenClient_FreeModuleData(&cached_module);
    return state.pending_module != nullptr;
}

void WardenClient_CacheModule(const uint8_t module_id[16],
                                const void* data, uint32_t size) {
    WardenModuleCache_Store(module_id, data, size);
}

void WardenClient_SendToServer(const void* data, uint32_t size) {
    (void)data;
    (void)size;

}

}
