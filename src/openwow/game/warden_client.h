#pragma once

#include "openwow/net/protocol/rc4_cipher.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace openwow::game {

using WardenModuleActivationCallback = int (*)(void);

struct WardenSessionKeys {
  std::array<std::uint8_t, 16> client_to_server{};
  std::array<std::uint8_t, 16> server_to_client{};
};

[[nodiscard]] WardenSessionKeys DeriveWardenSessionKeys(
    std::span<const std::uint8_t> session_key);

enum class WardenServerOpcode : std::uint8_t {
  kModuleUse = 0,
  kModuleCache = 1,
  kCheckRequest = 2,
  kModuleInitialize = 3,
  kMemoryRequest = 4,
  kHashRequest = 5,
};

enum class WardenCheckType : std::uint8_t {
  kTimingCheck = 0x57,
  kMemCheck = 0xF3,
  kPageCheckA = 0xB2,
  kPageCheckB = 0xBF,
  kMpqCheck = 0x98,
  kLuaStrCheck = 0x8B,
  kDriverCheck = 0x71,
  kProcCheck = 0x7E,
  kModuleCheck = 0xD9,
};

using WardenSha1Digest = std::array<std::uint8_t, 20>;

struct WardenSignatureProbe {
  WardenCheckType type = WardenCheckType::kModuleCheck;
  std::array<std::uint8_t, 4> seed{};
  WardenSha1Digest digest{};
  std::string primary_name;
  std::string secondary_name;
  std::uint32_t address = 0;
  std::uint8_t length = 0;
};

struct WardenProbeCallbacks {
  std::function<std::uint32_t()> monotonic_ticks;
  std::function<std::optional<std::vector<std::uint8_t>>(
      std::string_view module_name, std::uint32_t address,
      std::uint8_t length)> read_memory;
  std::function<std::optional<WardenSha1Digest>(std::string_view filename)>
      hash_archive_file;
  std::function<std::optional<std::string>(std::string_view expression)>
      evaluate_lua;
  std::function<bool(const WardenSignatureProbe&)> signature_matches;
};

struct WardenPortableCapabilities {
  bool archive_callbacks = false;
  bool lua_execute = false;
  bool performance_counter = false;

  [[nodiscard]] bool IsComplete() const {
    return archive_callbacks && lua_execute && performance_counter;
  }

  bool operator==(const WardenPortableCapabilities&) const = default;
};

struct WardenClientProtocolTestAccess;

class WardenClient {
 public:
  WardenClient() = default;

  void Init(const std::uint8_t* session_key, std::size_t key_len);

  void Reset();

  [[nodiscard]] bool IsInitialized() const;

  void HandleWardenData(const std::uint8_t* data, std::size_t len);

  [[nodiscard]] std::vector<std::uint8_t> BuildResponse();

  [[nodiscard]] bool HasPendingResponse() const;

  void SetProbeCallbacks(WardenProbeCallbacks callbacks);

  void SetReferenceClientExecutable(std::filesystem::path executable);

  [[nodiscard]] WardenPortableCapabilities GetPortableCapabilities() const;

  bool ProcessLegacyTokenSeed(const std::string& encoded_seed,
                              const std::uint8_t key[16]);

  bool ConsumeLegacyTokenSeedVerification();

  [[nodiscard]] bool IsLegacyTokenSeedVerified() const;

  bool DecryptAndActivateEmbeddedModule(
      const std::uint8_t* key, std::uint32_t key_len,
      WardenModuleActivationCallback callback);

  [[nodiscard]] bool IsEmbeddedModuleActivated() const;

  [[nodiscard]] const std::uint8_t* GetEmbeddedModuleData() const;

 private:
  friend struct WardenClientProtocolTestAccess;

 public:

  using RC4State = openwow::net::RC4State;

 private:

  void HandleModuleUse(std::span<const std::uint8_t> payload);
  void HandleModuleTransfer(std::span<const std::uint8_t> payload);
  void HandleHashRequest(std::span<const std::uint8_t> payload);
  void HandleModuleInitialize(std::span<const std::uint8_t> packet);
  void HandleMemoryRequest(std::span<const std::uint8_t> payload);
  void HandleCheckData(std::span<const std::uint8_t> payload);
  bool EnsureReferenceClientImageLoaded();
  [[nodiscard]] std::optional<std::vector<std::uint8_t>>
  ReadReferenceClientMemory(std::string_view module_name,
                            std::uint32_t address,
                            std::uint8_t length) const;
  [[nodiscard]] std::optional<WardenSha1Digest> HashArchiveFile(
      std::string_view filename);
  bool QueueResponse(std::vector<std::uint8_t> bytes,
                     std::optional<WardenSessionKeys> rekey = std::nullopt);
  void QueueModuleFailure(std::string_view reason);
  void ResetModuleState();

  mutable std::mutex mutex_;
  bool initialized_ = false;

  RC4State encrypt_state_;
  RC4State decrypt_state_;

  bool standard_profile_selected_ = false;
  bool awaiting_module_transfer_ = false;
  bool module_loaded_ = false;
  bool module_payload_validated_ = false;
  bool hash_response_pending_ = false;
  bool post_hash_active_ = false;
  bool module_initialization_failed_ = false;
  std::vector<std::uint8_t> module_data_;
  std::array<std::uint8_t, 16> module_id_{};
  std::array<std::uint8_t, 16> module_key_{};
  std::uint32_t module_size_ = 0;
  std::uint32_t module_received_ = 0;
  std::size_t next_initialize_record_ = 0;
  WardenPortableCapabilities portable_capabilities_{};
  WardenProbeCallbacks probe_callbacks_{};

  std::filesystem::path reference_client_executable_;
  bool reference_client_load_attempted_ = false;
  std::uint32_t reference_client_image_base_ = 0;
  std::vector<std::uint8_t> reference_client_image_;
  std::unordered_map<std::string, WardenSha1Digest> archive_hash_cache_;

  bool legacy_token_seed_verified_ = false;
  std::uint8_t legacy_token_seed_buffer_[4096]{};

  bool embedded_module_activated_ = false;
  std::uint8_t embedded_module_data_[512]{};
  WardenModuleActivationCallback embedded_module_callback_ = nullptr;

  struct PendingResponse {
    std::vector<std::uint8_t> bytes;
    std::optional<WardenSessionKeys> rekey;
  };
  std::deque<PendingResponse> pending_responses_;
};

}
