
#pragma once

#include <functional>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "openwow/game/async_query_channel.h"
#include "openwow/game/packet_reader.h"

namespace openwow::data {
class DBCacheRuntime;
}

namespace openwow::game {
class QueryCache;
class ObjectManager;
}

namespace openwow::net::wotlk {
struct WorldPacket;
}

namespace openwow::game {

enum class TrainerBuyFailReason : std::int32_t {
  kUnavailable      = 0,
  kNotEnoughMoney   = 1,
  kNotEnoughSkill   = 2,
};

struct TrainerBuyResult {
  std::uint64_t trainer_guid = 0;
  std::int32_t spell_id = 0;
  std::int32_t fail_reason = 0;
  bool succeeded = false;
};

struct PetitionType {
  std::uint32_t index = 0;
  std::uint32_t charter_entry = 0;
  std::uint32_t charter_display_id = 0;
  std::uint32_t cost = 0;
  std::uint32_t unk_value = 0;
  std::uint32_t required_signatures = 0;
};

struct PetitionShowList {
  std::uint64_t npc_guid = 0;
  std::vector<PetitionType> types;
};

struct GuildRegistrarState {
  std::uint64_t npc_guid = 0;
  PetitionType charter_offer{};
};

struct PetitionSignature {
  std::uint64_t signer_guid = 0;
  std::uint32_t unk = 0;
};

struct PetitionShowSignatures {
  std::uint64_t petition_guid = 0;
  std::uint64_t owner_guid = 0;
  std::uint32_t petition_id = 0;
  std::vector<PetitionSignature> signatures;
};

struct PetitionSignResult {
  std::uint64_t petition_guid = 0;
  std::uint64_t player_guid = 0;
  std::uint32_t error = 0;
};

struct PetitionQueryResponse {
  std::uint32_t petition_id = 0;
  std::uint64_t owner_guid = 0;
  std::string name;
  std::string body_text;
  std::uint32_t min_signatures = 0;
  std::uint32_t max_signatures = 0;
  std::array<std::uint32_t, 5> unknown_header_u32s{};
  std::uint16_t unknown_header_u16 = 0;
  std::uint32_t allowed_min_level = 0;
  std::uint32_t allowed_max_level = 0;
  std::uint32_t unknown_pre_name_u32 = 0;
  std::array<std::string, 10> extra_strings{};
  std::uint32_t unknown_post_name_u32 = 0;
  std::uint32_t petition_type = 0;
};

struct TurnInPetitionResult {
  std::uint32_t result = 0;
};

struct PetitionCursorSelection {
  std::uint64_t item_guid = 0;
  std::uint64_t container_guid = 0;
  std::uint32_t source_slot = 0;
};

inline constexpr std::uint32_t kItemTemplateFlagPetition = 0x2000u;

struct PetitionRenameResult {
  std::uint64_t guid = 0;
  std::string name;
};

struct PetitionUiTransition {
  bool fire_closed = false;
  bool fire_show = false;
  std::optional<int> system_message_id;
  std::string system_message_arg;
  std::string console_line;
};

class PetitionHandler {
 public:
  using PacketSender = std::function<bool(const net::wotlk::WorldPacket&)>;

  explicit PetitionHandler(openwow::data::DBCacheRuntime& db_cache_runtime)
      : db_cache_runtime_(db_cache_runtime) {}

  bool HandleTrainerBuySucceeded(const std::uint8_t* data, std::size_t len);
  bool HandleTrainerBuyFailed(const std::uint8_t* data, std::size_t len);

  bool ParsePetitionShowList(const std::uint8_t* data, std::size_t len,
                             PetitionShowList* out) const;
  bool HandlePetitionShowList(const std::uint8_t* data, std::size_t len);
  bool HandlePetitionShowSignatures(const std::uint8_t* data, std::size_t len);
  bool HandlePetitionSignResults(const std::uint8_t* data, std::size_t len);
  bool HandlePetitionQueryResponse(const std::uint8_t* data, std::size_t len);
  bool HandleTurnInPetitionResults(const std::uint8_t* data, std::size_t len);

  bool HandleSaveGuildEmblem(const std::uint8_t* data, std::size_t len);
  bool HandleTabardVendorActivate(const std::uint8_t* data, std::size_t len);
  bool HandlePetitionDecline(const std::uint8_t* data, std::size_t len);
  bool HandlePetitionRename(const std::uint8_t* data, std::size_t len);
  bool UpdateCachedPetitionName(std::uint32_t petition_id,
                                const std::string& new_name);
  bool HandleOfferPetitionError(const std::uint8_t* data, std::size_t len);

  [[nodiscard]] PetitionUiTransition BeginPetitionSignatureDisplay(
      QueryCache& query_cache, const ObjectManager& objects,
      const PacketSender& send_packet);
  [[nodiscard]] PetitionUiTransition OnPetitionQueryResponseUpdated();
  [[nodiscard]] PetitionUiTransition OnPlayerNameResolved(
      std::uint64_t raw_guid, QueryCache& query_cache,
      const ObjectManager& objects);
  [[nodiscard]] PetitionUiTransition ApplyPetitionSignResult(
      std::uint64_t active_player_guid, QueryCache& query_cache,
      const ObjectManager& objects, const PacketSender& send_packet);

  const TrainerBuyResult& last_trainer_buy() const { return last_trainer_buy_; }
  const PetitionShowList& last_petition_list() const { return last_petition_list_; }
  const GuildRegistrarState& guild_registrar() const { return guild_registrar_; }
  const PetitionShowSignatures& last_signatures() const { return last_signatures_; }
  const PetitionSignResult& last_sign_result() const { return last_sign_result_; }
  const PetitionQueryResponse& last_petition_query() const { return last_petition_query_; }
  const TurnInPetitionResult& last_turn_in() const { return last_turn_in_; }
  [[nodiscard]] std::uint32_t guild_emblem_result() const { return guild_emblem_result_; }
  [[nodiscard]] bool tabard_save_pending() const { return tabard_save_pending_; }
  [[nodiscard]] std::uint64_t petition_vendor_guid() const { return petition_vendor_guid_; }
  [[nodiscard]] std::uint64_t tabard_vendor_guid() const { return tabard_vendor_guid_; }
  [[nodiscard]] std::uint64_t guild_registrar_guid() const { return guild_registrar_.npc_guid; }
  [[nodiscard]] std::uint64_t petition_decline_guid() const { return petition_decline_guid_; }
  [[nodiscard]] std::uint64_t active_petition_guid() const { return active_petition_guid_; }
  [[nodiscard]] const PetitionCursorSelection& selected_petition_cursor() const {
    return selected_petition_cursor_;
  }
  [[nodiscard]] const std::optional<PetitionRenameResult>& last_petition_rename() const {
    return last_petition_rename_;
  }
  [[nodiscard]] std::uint64_t petition_error_guid() const { return petition_error_guid_; }
  [[nodiscard]] bool last_petition_query_was_update() const {
    return last_petition_query_was_update_;
  }
  [[nodiscard]] std::size_t GetPetitionVendorOfferCount() const;
  [[nodiscard]] const PetitionType* GetPetitionVendorOffer(std::size_t index) const;
  [[nodiscard]] bool HasActivePetitionQuery() const;
  [[nodiscard]] std::size_t GetDisplayedPetitionSignatureCount() const;
  [[nodiscard]] bool TryGetDisplayedPetitionSignerName(
      std::size_t index, const QueryCache& query_cache,
      const ObjectManager& objects, std::string* out_name) const;
  [[nodiscard]] bool TryGetDisplayedPetitionOwnerName(
      const QueryCache& query_cache, const ObjectManager& objects,
      std::string* out_name) const;
  [[nodiscard]] bool ResolveOrRequestDisplayedPetitionSignerName(
      std::size_t index, QueryCache& query_cache, const ObjectManager& objects,
      const PacketSender& send_packet, std::string* out_name) const;
  [[nodiscard]] bool ResolveOrRequestDisplayedPetitionOwnerName(
      QueryCache& query_cache, const ObjectManager& objects,
      const PacketSender& send_packet, std::string* out_name) const;
  [[nodiscard]] bool CanActivePlayerSign(std::uint64_t active_player_guid,
                                         bool active_player_is_guilded) const;
  [[nodiscard]] const PetitionQueryResponse* FindCachedPetitionQuery(
      std::uint32_t petition_id) const;
  void HandleClientCacheVersionInvalidation();
  void ClearPendingQueriesOnLogout();

  void ResetPetitionVendorOfferCache();
  void ResetActivePetitionState();
  void ClearPetitionVendorInteraction();
  void ClearTabardSavePending();
  void ClearTabardVendorInteraction();
  void ClearGuildRegistrarGuid();
  void ClearGuildRegistrarInteraction();
  void ResetGuildRegistrarCharterOffer();
  void MarkTabardSavePending();
  void SetPetitionShowList(PetitionShowList list);
  void SetSelectedPetitionCursor(std::uint64_t item_guid,
                                 std::uint64_t container_guid,
                                 std::uint32_t source_slot);
  void ClearSelectedPetitionCursor();
  void OpenGuildRegistrar(std::uint64_t npc_guid,
                          const PetitionType& charter_offer);
  void MarkActivePetitionSignRequested();
  PetitionUiTransition ClosePetitionSignatureDisplay(
      std::uint64_t active_player_guid, const PacketSender& send_packet);
  void Clear();

 private:
  openwow::data::DBCacheRuntime& db_cache_runtime_;

  enum class SignerStorageMode {
    kRelease,
    kEnsureBootstrapCapacity,
  };

  void ResetActivePetitionState(SignerStorageMode storage_mode);
  void ResetActivePetitionRuntimeState();
  [[nodiscard]] bool ShouldDeclineActivePetition(
      std::uint64_t active_player_guid) const;
  void MaybeSendActivePetitionDecline(std::uint64_t active_player_guid,
                                      const PacketSender& send_packet) const;
  [[nodiscard]] PetitionUiTransition MaybeEmitPetitionShow();
  void RefreshActivePetitionQueryState(const PacketSender& send_packet);
  [[nodiscard]] bool IsPlayerNameResolved(std::uint64_t raw_guid,
                                          const QueryCache& query_cache,
                                          const ObjectManager& objects) const;
  [[nodiscard]] std::uint32_t RefreshPendingSignerNameQueries(
      QueryCache& query_cache, const ObjectManager& objects,
      const PacketSender* send_packet);
  [[nodiscard]] bool AppendDisplayedSigner(std::uint64_t raw_guid,
                                           std::uint32_t unk);

  TrainerBuyResult last_trainer_buy_{};
  PetitionShowList last_petition_list_{};
  GuildRegistrarState guild_registrar_{};
  PetitionShowSignatures last_signatures_{};
  PetitionSignResult last_sign_result_{};
  PetitionQueryResponse last_petition_query_{};
  TurnInPetitionResult last_turn_in_{};
  std::uint32_t guild_emblem_result_{0};
  bool tabard_save_pending_{false};
  std::uint64_t petition_vendor_guid_{0};
  std::uint64_t tabard_vendor_guid_{0};
  std::uint64_t petition_decline_guid_{0};
  PetitionCursorSelection selected_petition_cursor_{};
  std::optional<PetitionRenameResult> last_petition_rename_;
  std::uint64_t petition_error_guid_{0};
  std::uint64_t active_petition_guid_{0};
  std::uint32_t active_petition_id_{0};
  std::uint32_t pending_signer_name_queries_{0};
  bool last_petition_query_was_update_{false};
  bool active_petition_sign_requested_{false};
  bool petition_query_ready_{false};
  mutable std::unordered_map<std::uint32_t, PetitionQueryResponse> petition_queries_;
  AsyncQueryChannel pending_petition_queries_;
};

}
