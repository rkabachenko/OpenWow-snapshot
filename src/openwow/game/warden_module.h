#pragma once

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <vector>

namespace openwow::game {

struct WardenModuleData {
    uint32_t size = 0;
    uint8_t* data = nullptr;
};

struct WardenModuleState {
    void* pending_module = nullptr;
    void* active_module = nullptr;
    uintptr_t module_vtable = 0;
    uint8_t* packet_data = nullptr;
    uint32_t packet_data_size = 0;
    int processor_count = 0;

    std::function<int()> query_processor_count;
    std::function<void(int)> on_processor_delta;
};

void WardenClient_WriteKey(void* data_store, const uint8_t key[16]);

void WardenClient_ReadKey(void* data_store, uint8_t key[16]);

void WardenClient_FreeModuleData(WardenModuleData* mod);

void WardenClient_WriteModuleData(const WardenModuleData* mod, void* data_store);

void WardenClient_ReadModuleData(WardenModuleData* mod, void* data_store);

void WardenClient_UnloadPendingModule(WardenModuleState& state);

void WardenClient_UnloadActiveModule(WardenModuleState& state);

bool WardenClient_InitializeModule(WardenModuleState& state);

void WardenClient_CheckProcessorCount(WardenModuleState& state);

int InitGameSubsystems_WardenMaintenanceTick(
    WardenModuleState& state,
    const std::function<void()>& pre_maintenance = {});

void WardenClient_Shutdown(WardenModuleState& state);

void* WardenClient_ModuleAlloc(uint32_t size);

void WardenClient_ModuleFree(void* ptr);

void WardenClient_SetPacketData(WardenModuleState& state,
                                 const void* data, uint32_t size);

bool WardenClient_GetPacketData(WardenModuleState& state,
                                 void* out_data, uint32_t* inout_size);

void WardenClient_CopyModuleData(WardenModuleData* dst, const WardenModuleData* src);

[[nodiscard]] bool WardenModuleCache_Load(const uint8_t module_id[16],
                                          WardenModuleData& out);
void WardenModuleCache_Store(const uint8_t module_id[16],
                             const void* data,
                             uint32_t size);

void WardenModuleCache_LoadStartup(const std::filesystem::path& cache_directory,
                                   std::uint32_t locale);

void ResetWardenModuleCacheForTests();

void WardenModuleCache_Destroy();

void WardenModuleCache_ApplyClientCacheVersion(std::uint32_t version);

void* WardenClient_PrepareModule(uint32_t data_size, const void* data,
                                  const uint8_t rc4_key[16]);

int WardenClient_HandlePacket(WardenModuleState& state,
                               uint32_t opcode, const void* data, uint32_t size);

void WardenClient_Initialize(WardenModuleState& state);

bool WardenClient_LoadModuleFromCache(WardenModuleState& state,
                                       const uint8_t module_id[16],
                                       const uint8_t rc4_key[16]);

void WardenClient_CacheModule(const uint8_t module_id[16],
                                const void* data, uint32_t size);

void WardenClient_SendToServer(const void* data, uint32_t size);

}
