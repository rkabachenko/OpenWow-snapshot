
#pragma once

#include "openwow/foundation/hashing/retail_adler_seed.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace openwow::audio { class SoundRuntime; }

namespace openwow::core {

int WoWStart();

int WoW_GameEntry();

void WoW_FiberEntry(int* fiber_parameter);
int RunLegacyStartupFiberBootstrap(std::function<int()> entry_point = {});

int WoW_MainInit();

std::int64_t GetInitTimerElapsedTimeNs();

bool ClientInit();

void InitializeClientStartupAdlerSeedState();

[[nodiscard]] foundation::hashing::AdlerSeedState&
GetClientStartupAdlerSeedState();

using LoginSurveyTelemetryBootstrapRecord = std::array<std::uint8_t, 32>;

[[nodiscard]] bool HasLoginSurveyTelemetryBootstrapRecord();
[[nodiscard]] std::optional<LoginSurveyTelemetryBootstrapRecord>
GetLoginSurveyTelemetryBootstrapRecord();
[[nodiscard]] bool AppendLoginSurveyTelemetryBootstrapRecord(
    std::vector<LoginSurveyTelemetryBootstrapRecord>& records);

int PostInitErrorCheck();

int RequestClientShutdownWithErrorCode(std::uint32_t error_code);
bool ConsumeClientShutdownRequest(std::uint32_t* error_code = nullptr);
void ClearClientShutdownRequest();

void Cleanup_FreeAllRegisteredObjects();

void WriteInstallPathToRegistry();

void* ClientAlloc(std::size_t size);

void ClientFree(void* ptr);

std::uint8_t GetExpansionLevel();
void SetExpansionLevel(std::uint8_t level);

struct SignatureFileResult {
  bool ok{false};
  std::uint32_t checksum{0};
  int signature_version{0};
};

SignatureFileResult LoadSignatureFile(std::uint32_t expected_checksum);

constexpr std::uint32_t kWoWBuild = 12340;
std::string BuildBugReport();

struct EnterWorldInitParams {
  std::uint32_t map_id{0};
  float x{0}, y{0}, z{0};
};

bool EnterWorldInit(const EnterWorldInitParams& params,
                    openwow::audio::SoundRuntime& sound_runtime);

void ProcessRunOnceFiles();

void SetConvertedTrialFlag(bool converted);

void InitArchiveIntegrity();

bool RegisterArchiveIntegrityHashForFile(
    const char* filename, const std::uint8_t expected_hash[16]);

std::optional<std::array<std::uint8_t, 16>> FindArchiveIntegrityDigestForFile(
    const char* filename);

void SetArchiveIntegrityValidationMode(bool enable);

void SetStreamingIntegrityFlag(std::int32_t value);

void SetStreamingIntegrityCallback(std::int32_t callback_id);

}
