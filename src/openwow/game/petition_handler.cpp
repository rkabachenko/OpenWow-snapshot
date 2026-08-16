
#include "openwow/game/petition_handler.h"

#include "openwow/data/db_cache_instances.h"
#include "openwow/data/wdb_cache.h"
#include "openwow/data/wdb_persistence.h"
#include "openwow/game/object_guid.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/query_cache.h"

#include "openwow/net/wotlk/protocol/packet_sender.h"

namespace openwow::game {

namespace {

constexpr std::size_t kBootstrapPetitionSignerCapacity = 10;
constexpr std::size_t kMaxPetitionVendorOfferCount = 10;
constexpr std::size_t kPetitionQueryNameMax = 0x100;
constexpr std::size_t kPetitionQueryBodyTextMax = 0x1000;
constexpr std::size_t kPetitionQueryExtraStringMax = 0x40;
constexpr int kPetitionSignedSystemMessageId = 341;
constexpr int kPetitionSignerAddedSystemMessageId = 342;

std::optional<int> ResolvePetitionSignErrorSystemMessage(
    const std::uint32_t error) {
  switch (error) {
    case 0:
      return kPetitionSignedSystemMessageId;
    case 1:
      return 344;
    case 2:
      return 347;
    case 3:
      return 348;
    case 5:
      return 350;
    case 8:
      return 351;
    case 10:
      return 346;
    case 11:
      return 345;
    default:
      return std::nullopt;
  }
}

std::uint32_t NormalizePetitionCacheEntryId(
    const std::int32_t raw_petition_id) {
  if (raw_petition_id < 0) {
    return static_cast<std::uint32_t>(-static_cast<std::int64_t>(raw_petition_id));
  }

  return static_cast<std::uint32_t>(raw_petition_id);
}

bool ReadBoundedCString(PacketReader& reader, std::string& out,
                        const std::size_t max_bytes) {
  out.clear();

  for (std::size_t index = 0; index < max_bytes; ++index) {
    std::uint8_t byte = 0;
    if (!reader.ReadU8(byte)) {
      out.clear();
      return false;
    }
    if (byte == 0) {
      return true;
    }
    out.push_back(static_cast<char>(byte));
  }

  out.clear();
  reader.Skip(reader.Remaining());
  return false;
}

std::string ResolvePlayerName(const std::uint64_t raw_guid,
                              const QueryCache& query_cache,
                              const ObjectManager& objects) {
  if (raw_guid == 0) {
    return {};
  }

  if (const auto* cached_name = query_cache.GetPlayerName(raw_guid)) {
    return cached_name->name;
  }

  return objects.GetPlayerName(ObjectGuid(raw_guid));
}

bool ResolveOrQueuePlayerName(const std::uint64_t raw_guid, QueryCache& query_cache,
                              const ObjectManager& objects,
                              const PetitionHandler::PacketSender& send_packet,
                              std::string* out_name) {
  if (out_name == nullptr) {
    return false;
  }

  *out_name = ResolvePlayerName(raw_guid, query_cache, objects);
  if (!out_name->empty()) {
    return true;
  }

  if (query_cache.RequestNameQuery(raw_guid) &&
      !query_cache.HasNameQueryDispatcher() &&
      static_cast<bool>(send_packet)) {
    (void)send_packet(QueryCache::BuildNameQuery(raw_guid));
  }

  out_name->clear();
  return false;
}

void AppendU16LE(std::vector<std::uint8_t>& bytes, const std::uint16_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value & 0xFF));
  bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

void AppendU32LE(std::vector<std::uint8_t>& bytes, const std::uint32_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value & 0xFF));
  bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
  bytes.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
  bytes.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
}

void AppendU64LE(std::vector<std::uint8_t>& bytes, const std::uint64_t value) {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    bytes.push_back(static_cast<std::uint8_t>((value >> (index * 8)) & 0xFF));
  }
}

void AppendClampedCString(std::vector<std::uint8_t>& bytes,
                          const std::string& value,
                          const std::size_t max_bytes) {
  const auto clamped_size =
      std::min(value.size(), max_bytes == 0 ? std::size_t{0} : max_bytes - 1);
  bytes.insert(bytes.end(), value.begin(), value.begin() + clamped_size);
  bytes.push_back(0);
}

bool ReadPetitionQueryResponseRecord(PacketReader& reader,
                                     PetitionQueryResponse* out_response,
                                     std::int32_t* out_raw_petition_id) {
  if (out_response == nullptr) {
    return false;
  }

  PetitionQueryResponse response{};
  std::int32_t raw_petition_id = 0;
  if (!reader.ReadI32(raw_petition_id)) {
    return false;
  }
  response.petition_id = NormalizePetitionCacheEntryId(raw_petition_id);
  if (!reader.ReadU64(response.owner_guid)) {
    return false;
  }
  if (!ReadBoundedCString(reader, response.name, kPetitionQueryNameMax)) {
    return false;
  }
  if (!ReadBoundedCString(reader, response.body_text, kPetitionQueryBodyTextMax)) {
    return false;
  }
  if (!reader.ReadU32(response.min_signatures)) {
    return false;
  }
  if (!reader.ReadU32(response.max_signatures)) {
    return false;
  }
  for (auto& value : response.unknown_header_u32s) {
    if (!reader.ReadU32(value)) {
      return false;
    }
  }
  if (!reader.ReadU16(response.unknown_header_u16) ||
      !reader.ReadU32(response.allowed_min_level) ||
      !reader.ReadU32(response.allowed_max_level) ||
      !reader.ReadU32(response.unknown_pre_name_u32)) {
    return false;
  }
  for (auto& value : response.extra_strings) {
    if (!ReadBoundedCString(reader, value, kPetitionQueryExtraStringMax)) {
      return false;
    }
  }
  if (!reader.ReadU32(response.unknown_post_name_u32)) {
    return false;
  }
  if (!reader.ReadU32(response.petition_type)) {
    return false;
  }

  *out_response = std::move(response);
  if (out_raw_petition_id != nullptr) {
    *out_raw_petition_id = raw_petition_id;
  }
  return true;
}

std::vector<std::uint8_t> SerializePetitionQueryWdbRecord(
    const PetitionQueryResponse& response) {
  std::vector<std::uint8_t> bytes;
  bytes.reserve(256);
  AppendU32LE(bytes, response.petition_id);
  AppendU64LE(bytes, response.owner_guid);
  AppendClampedCString(bytes, response.name, kPetitionQueryNameMax);
  AppendClampedCString(bytes, response.body_text, kPetitionQueryBodyTextMax);
  AppendU32LE(bytes, response.min_signatures);
  AppendU32LE(bytes, response.max_signatures);
  for (const auto value : response.unknown_header_u32s) {
    AppendU32LE(bytes, value);
  }
  AppendU16LE(bytes, response.unknown_header_u16);
  AppendU32LE(bytes, response.allowed_min_level);
  AppendU32LE(bytes, response.allowed_max_level);
  AppendU32LE(bytes, response.unknown_pre_name_u32);
  for (const auto& value : response.extra_strings) {
    AppendClampedCString(bytes, value, kPetitionQueryExtraStringMax);
  }
  AppendU32LE(bytes, response.unknown_post_name_u32);
  AppendU32LE(bytes, response.petition_type);
  return bytes;
}

std::optional<PetitionQueryResponse> DeserializePetitionQueryWdbRecord(
    const std::uint32_t expected_petition_id,
    const std::vector<std::uint8_t>& data) {
  PacketReader reader(data.data(), data.size());
  PetitionQueryResponse response{};
  std::int32_t raw_petition_id = 0;
  if (!ReadPetitionQueryResponseRecord(reader, &response, &raw_petition_id)) {
    return std::nullopt;
  }
  if (raw_petition_id <= 0 || response.petition_id != expected_petition_id) {
    return std::nullopt;
  }
  return response;
}

}

void PetitionHandler::ClearPetitionVendorInteraction() {
  petition_vendor_guid_ = 0;
  last_petition_list_.npc_guid = 0;
  last_petition_list_.types.clear();
  ClearSelectedPetitionCursor();
}

void PetitionHandler::ClearTabardSavePending() {
  tabard_save_pending_ = false;
}

void PetitionHandler::ClearTabardVendorInteraction() {
  tabard_vendor_guid_ = 0;
}

void PetitionHandler::ClearGuildRegistrarGuid() {
  guild_registrar_.npc_guid = 0;
}

void PetitionHandler::ClearGuildRegistrarInteraction() {
  guild_registrar_ = {};
}

void PetitionHandler::ResetGuildRegistrarCharterOffer() {
  guild_registrar_.charter_offer = {};
}

void PetitionHandler::MarkTabardSavePending() {
  tabard_save_pending_ = true;
}

void PetitionHandler::SetPetitionShowList(PetitionShowList list) {
  if (list.types.size() > kMaxPetitionVendorOfferCount) {
    list.types.resize(kMaxPetitionVendorOfferCount);
  }
  petition_vendor_guid_ = list.npc_guid;
  ClearSelectedPetitionCursor();
  last_petition_list_ = std::move(list);
}

std::size_t PetitionHandler::GetPetitionVendorOfferCount() const {
  return last_petition_list_.types.size();
}

void PetitionHandler::ResetPetitionVendorOfferCache() {

  last_petition_list_.npc_guid = petition_vendor_guid_;
  last_petition_list_.types.clear();
  ClearSelectedPetitionCursor();
}

void PetitionHandler::SetSelectedPetitionCursor(const std::uint64_t item_guid,
                                                const std::uint64_t container_guid,
                                                const std::uint32_t source_slot) {
  selected_petition_cursor_.item_guid = item_guid;
  selected_petition_cursor_.container_guid = container_guid;
  selected_petition_cursor_.source_slot = source_slot;
}

void PetitionHandler::ClearSelectedPetitionCursor() {
  selected_petition_cursor_ = {};
}

void PetitionHandler::OpenGuildRegistrar(const std::uint64_t npc_guid,
                                         const PetitionType& charter_offer) {
  guild_registrar_.npc_guid = npc_guid;
  guild_registrar_.charter_offer = charter_offer;
}

const PetitionType* PetitionHandler::GetPetitionVendorOffer(
    const std::size_t index) const {
  if (index >= last_petition_list_.types.size()) {
    return nullptr;
  }

  return &last_petition_list_.types[index];
}

void PetitionHandler::ResetActivePetitionState() {
  ResetActivePetitionState(SignerStorageMode::kEnsureBootstrapCapacity);
}

void PetitionHandler::ResetActivePetitionRuntimeState() {
  last_sign_result_ = {};
  last_petition_query_ = {};
  active_petition_guid_ = 0;
  active_petition_id_ = 0;
  pending_signer_name_queries_ = 0;
  last_petition_query_was_update_ = false;
  active_petition_sign_requested_ = false;
  petition_query_ready_ = false;
}

void PetitionHandler::ResetActivePetitionState(
    const SignerStorageMode storage_mode) {
  last_signatures_.petition_guid = 0;
  last_signatures_.owner_guid = 0;
  last_signatures_.petition_id = 0;
  last_signatures_.signatures.clear();
  if (storage_mode == SignerStorageMode::kRelease) {
    std::vector<PetitionSignature>().swap(last_signatures_.signatures);
  } else if (last_signatures_.signatures.capacity() <
             kBootstrapPetitionSignerCapacity) {
    last_signatures_.signatures.reserve(kBootstrapPetitionSignerCapacity);
  }
  ResetActivePetitionRuntimeState();
}

bool PetitionHandler::HandleTrainerBuySucceeded(const std::uint8_t* data,
                                                 std::size_t len) {
  PacketReader r(data, len);
  TrainerBuyResult result{};
  if (!r.ReadU64(result.trainer_guid)) return false;
  std::uint32_t spell_u32 = 0;
  if (!r.ReadU32(spell_u32)) return false;
  result.spell_id = static_cast<std::int32_t>(spell_u32);
  result.succeeded = true;
  last_trainer_buy_ = result;
  return true;
}

bool PetitionHandler::HandleTrainerBuyFailed(const std::uint8_t* data,
                                              std::size_t len) {
  PacketReader r(data, len);
  TrainerBuyResult result{};
  if (!r.ReadU64(result.trainer_guid)) return false;
  std::uint32_t spell_u32 = 0;
  if (!r.ReadU32(spell_u32)) return false;
  result.spell_id = static_cast<std::int32_t>(spell_u32);
  std::uint32_t reason_u32 = 0;
  if (!r.ReadU32(reason_u32)) return false;
  result.fail_reason = static_cast<std::int32_t>(reason_u32);
  result.succeeded = false;
  last_trainer_buy_ = result;
  return true;
}

bool PetitionHandler::ParsePetitionShowList(const std::uint8_t* data,
                                            const std::size_t len,
                                            PetitionShowList* out) const {
  if (out == nullptr) {
    return false;
  }

  PacketReader r(data, len);
  PetitionShowList list{};
  if (!r.ReadU64(list.npc_guid)) return false;
  std::uint8_t count = 0;
  if (!r.ReadU8(count)) return false;
  list.types.resize(count);
  for (auto& t : list.types) {
    if (!r.ReadU32(t.index)) return false;
    if (!r.ReadU32(t.charter_entry)) return false;
    if (!r.ReadU32(t.charter_display_id)) return false;
    if (!r.ReadU32(t.cost)) return false;
    if (!r.ReadU32(t.unk_value)) return false;
    if (!r.ReadU32(t.required_signatures)) return false;
  }
  *out = std::move(list);
  return true;
}

bool PetitionHandler::HandlePetitionShowList(const std::uint8_t* data,
                                             const std::size_t len) {
  PetitionShowList list{};
  if (!ParsePetitionShowList(data, len, &list)) return false;
  SetPetitionShowList(std::move(list));
  return true;
}

bool PetitionHandler::HandlePetitionShowSignatures(const std::uint8_t* data,
                                                    std::size_t len) {
  PacketReader r(data, len);
  PetitionShowSignatures sigs{};
  if (!r.ReadU64(sigs.petition_guid)) return false;
  if (!r.ReadU64(sigs.owner_guid)) return false;
  if (!r.ReadU32(sigs.petition_id)) return false;
  std::uint8_t count = 0;
  if (!r.ReadU8(count)) return false;
  sigs.signatures.resize(count);
  for (auto& s : sigs.signatures) {
    if (!r.ReadU64(s.signer_guid)) return false;
    if (!r.ReadU32(s.unk)) return false;
  }
  last_signatures_ = std::move(sigs);
  return true;
}

bool PetitionHandler::HandlePetitionSignResults(const std::uint8_t* data,
                                                 std::size_t len) {
  PacketReader r(data, len);
  PetitionSignResult result{};
  if (!r.ReadU64(result.petition_guid)) return false;
  if (!r.ReadU64(result.player_guid)) return false;
  if (!r.ReadU32(result.error)) return false;
  last_sign_result_ = result;
  return true;
}

bool PetitionHandler::HandlePetitionQueryResponse(const std::uint8_t* data,
                                                   std::size_t len) {
  PacketReader r(data, len);
  std::int32_t raw_petition_id = 0;
  PetitionQueryResponse resp{};
  if (!ReadPetitionQueryResponseRecord(r, &resp, &raw_petition_id)) {
    return false;
  }

  const auto cache_key = resp.petition_id;
  last_petition_query_was_update_ = raw_petition_id > 0;
  if (cache_key != 0) {
    (void)pending_petition_queries_.Resolve(cache_key);
  }
  if (raw_petition_id <= 0) {
    petition_queries_.erase(cache_key);
    if (db_cache_runtime_.cache().InvalidateEntry(
            openwow::data::WDBCacheType::Petition, cache_key)) {
      db_cache_runtime_.persistence().SetDirty(
          openwow::data::WDBCacheType::Petition);
    }
    return true;
  }

  db_cache_runtime_.cache().UpdateEntry(
      openwow::data::WDBCacheType::Petition, cache_key,
      SerializePetitionQueryWdbRecord(resp),
      openwow::data::wdb_format::kVersion_Petition);
  db_cache_runtime_.persistence().SetDirty(
      openwow::data::WDBCacheType::Petition);
  petition_queries_[cache_key] = resp;
  last_petition_query_ = std::move(resp);
  return true;
}

PetitionUiTransition PetitionHandler::BeginPetitionSignatureDisplay(
    QueryCache& query_cache, const ObjectManager& objects,
    const PacketSender& send_packet) {
  PetitionUiTransition transition{};
  transition.fire_closed = active_petition_guid_ != 0;

  MaybeSendActivePetitionDecline(objects.GetActivePlayerGuid().GetRawValue(),
                                 send_packet);
  ResetActivePetitionRuntimeState();

  active_petition_guid_ = last_signatures_.petition_guid;
  active_petition_id_ = last_signatures_.petition_id;
  RefreshActivePetitionQueryState(send_packet);
  pending_signer_name_queries_ =
      RefreshPendingSignerNameQueries(query_cache, objects, &send_packet);

  const auto show_transition = MaybeEmitPetitionShow();
  transition.fire_show = show_transition.fire_show;
  return transition;
}

void PetitionHandler::MarkActivePetitionSignRequested() {
  if (active_petition_guid_ == 0) {
    return;
  }

  active_petition_sign_requested_ = true;
}

PetitionUiTransition PetitionHandler::ClosePetitionSignatureDisplay(
    const std::uint64_t active_player_guid, const PacketSender& send_packet) {
  const bool had_active_petition = active_petition_guid_ != 0;
  MaybeSendActivePetitionDecline(active_player_guid, send_packet);
  ResetActivePetitionState();
  return {.fire_closed = had_active_petition, .fire_show = false};
}

PetitionUiTransition PetitionHandler::OnPetitionQueryResponseUpdated() {
  if (active_petition_id_ == 0 ||
      last_petition_query_.petition_id != active_petition_id_) {
    return {};
  }

  petition_query_ready_ = true;
  return MaybeEmitPetitionShow();
}

PetitionUiTransition PetitionHandler::OnPlayerNameResolved(
    const std::uint64_t raw_guid, QueryCache& query_cache,
    const ObjectManager& objects) {
  if (raw_guid == 0 || pending_signer_name_queries_ == 0) {
    return {};
  }

  bool tracked_signer = false;
  for (const auto& signature : last_signatures_.signatures) {
    if (signature.signer_guid == raw_guid) {
      tracked_signer = true;
      break;
    }
  }

  if (!tracked_signer) {
    return {};
  }

  pending_signer_name_queries_ =
      RefreshPendingSignerNameQueries(query_cache, objects, nullptr);
  return MaybeEmitPetitionShow();
}

PetitionUiTransition PetitionHandler::ApplyPetitionSignResult(
    const std::uint64_t active_player_guid, QueryCache& query_cache,
    const ObjectManager& objects, const PacketSender& send_packet) {
  PetitionUiTransition transition{};

  if (last_sign_result_.player_guid == active_player_guid) {
    if (const auto message_id =
            ResolvePetitionSignErrorSystemMessage(last_sign_result_.error);
        message_id.has_value()) {
      transition.system_message_id = *message_id;
      transition.fire_closed = last_sign_result_.error == 0;
    } else {
      transition.console_line = "Petition error";
    }
    return transition;
  }

  if (!AppendDisplayedSigner(last_sign_result_.player_guid, 0)) {
    return transition;
  }

  const std::string signer_name =
      ResolvePlayerName(last_sign_result_.player_guid, query_cache, objects);
  if (!signer_name.empty()) {
    transition.system_message_id = kPetitionSignerAddedSystemMessageId;
    transition.system_message_arg = signer_name;
  }

  pending_signer_name_queries_ =
      RefreshPendingSignerNameQueries(query_cache, objects, &send_packet);
  const auto show_transition = MaybeEmitPetitionShow();
  transition.fire_show = show_transition.fire_show;
  return transition;
}

bool PetitionHandler::HasActivePetitionQuery() const {
  return active_petition_id_ != 0 &&
         last_petition_query_.petition_id == active_petition_id_;
}

std::size_t PetitionHandler::GetDisplayedPetitionSignatureCount() const {
  return last_signatures_.signatures.size();
}

bool PetitionHandler::TryGetDisplayedPetitionSignerName(
    const std::size_t index, const QueryCache& query_cache,
    const ObjectManager& objects, std::string* out_name) const {
  if (out_name == nullptr || index >= last_signatures_.signatures.size()) {
    return false;
  }

  *out_name = ResolvePlayerName(last_signatures_.signatures[index].signer_guid,
                                query_cache, objects);
  return !out_name->empty();
}

bool PetitionHandler::TryGetDisplayedPetitionOwnerName(
    const QueryCache& query_cache, const ObjectManager& objects,
    std::string* out_name) const {
  if (out_name == nullptr || !HasActivePetitionQuery()) {
    return false;
  }

  *out_name =
      ResolvePlayerName(last_petition_query_.owner_guid, query_cache, objects);
  return !out_name->empty();
}

bool PetitionHandler::ResolveOrRequestDisplayedPetitionSignerName(
    const std::size_t index, QueryCache& query_cache,
    const ObjectManager& objects, const PacketSender& send_packet,
    std::string* out_name) const {
  if (index >= last_signatures_.signatures.size()) {
    return false;
  }

  return ResolveOrQueuePlayerName(last_signatures_.signatures[index].signer_guid,
                                  query_cache, objects, send_packet, out_name);
}

bool PetitionHandler::ResolveOrRequestDisplayedPetitionOwnerName(
    QueryCache& query_cache, const ObjectManager& objects,
    const PacketSender& send_packet, std::string* out_name) const {
  if (!HasActivePetitionQuery()) {
    return false;
  }

  return ResolveOrQueuePlayerName(last_petition_query_.owner_guid, query_cache,
                                  objects, send_packet, out_name);
}

bool PetitionHandler::CanActivePlayerSign(const std::uint64_t active_player_guid,
                                          const bool active_player_is_guilded) const {
  bool can_sign = true;

  if (HasActivePetitionQuery()) {
    if (last_petition_query_.petition_type <= 1) {
      if ((last_petition_query_.petition_type == 0 &&
           active_player_is_guilded) ||
          GetDisplayedPetitionSignatureCount() >=
              last_petition_query_.max_signatures) {
        can_sign = false;
      }
    }

    if (last_petition_query_.owner_guid == active_player_guid || !can_sign) {
      return false;
    }
  }

  for (const auto& signature : last_signatures_.signatures) {
    if (signature.signer_guid == active_player_guid) {
      return false;
    }
  }

  return true;
}

const PetitionQueryResponse* PetitionHandler::FindCachedPetitionQuery(
    const std::uint32_t petition_id) const {
  if (petition_id == 0) {
    return nullptr;
  }

  const auto it = petition_queries_.find(petition_id);
  if (it != petition_queries_.end()) {
    return &it->second;
  }

  const auto entry = db_cache_runtime_.cache().Get(
      openwow::data::WDBCacheType::Petition, petition_id);
  if (!entry.has_value()) {
    return nullptr;
  }

  auto parsed = DeserializePetitionQueryWdbRecord(petition_id, entry->data);
  if (!parsed.has_value()) {
    return nullptr;
  }

  const auto [cached_it, _] =
      petition_queries_.emplace(petition_id, std::move(*parsed));
  return &cached_it->second;
}

void PetitionHandler::HandleClientCacheVersionInvalidation() {
  petition_queries_.clear();
  pending_petition_queries_.Clear();
  last_petition_query_ = {};
  petition_query_ready_ = false;
  last_petition_query_was_update_ = false;
}

void PetitionHandler::ClearPendingQueriesOnLogout() {
  pending_petition_queries_.Clear();
}

bool PetitionHandler::ShouldDeclineActivePetition(
    const std::uint64_t active_player_guid) const {
  return active_petition_guid_ != 0 && !active_petition_sign_requested_ &&
         HasActivePetitionQuery() &&
         last_petition_query_.owner_guid != active_player_guid;
}

void PetitionHandler::MaybeSendActivePetitionDecline(
    const std::uint64_t active_player_guid,
    const PacketSender& send_packet) const {
  if (!ShouldDeclineActivePetition(active_player_guid) ||
      !static_cast<bool>(send_packet)) {
    return;
  }

  (void)send_packet(openwow::net::wotlk::PacketSender::BuildPetitionDecline(
      active_petition_guid_));
}

PetitionUiTransition PetitionHandler::MaybeEmitPetitionShow() {

  if (petition_query_ready_ && pending_signer_name_queries_ == 0) {
    return {.fire_closed = false, .fire_show = true};
  }

  return {};
}

void PetitionHandler::RefreshActivePetitionQueryState(
    const PacketSender& send_packet) {
  petition_query_ready_ = false;
  if (active_petition_id_ == 0) {
    return;
  }

  if (const auto* cached_query = FindCachedPetitionQuery(active_petition_id_)) {
    last_petition_query_ = *cached_query;
    petition_query_ready_ = true;
    (void)pending_petition_queries_.Resolve(active_petition_id_);
    return;
  }

  if (!static_cast<bool>(send_packet) ||
      pending_petition_queries_.IsPending(active_petition_id_)) {
    return;
  }

  pending_petition_queries_.MarkPending(active_petition_id_,
                                        0,
                                        active_petition_guid_);
  (void)send_packet(openwow::net::wotlk::PacketSender::BuildPetitionQuery(
      active_petition_id_, active_petition_guid_));
}

bool PetitionHandler::IsPlayerNameResolved(const std::uint64_t raw_guid,
                                           const QueryCache& query_cache,
                                           const ObjectManager& objects) const {
  return !ResolvePlayerName(raw_guid, query_cache, objects).empty();
}

std::uint32_t PetitionHandler::RefreshPendingSignerNameQueries(
    QueryCache& query_cache, const ObjectManager& objects,
    const PacketSender* send_packet) {
  std::uint32_t pending_queries = 0;

  for (const auto& signature : last_signatures_.signatures) {
    if (IsPlayerNameResolved(signature.signer_guid, query_cache, objects)) {
      continue;
    }

    ++pending_queries;

    if (send_packet != nullptr && static_cast<bool>(*send_packet) &&
        query_cache.RequestNameQuery(signature.signer_guid) &&
        !query_cache.HasNameQueryDispatcher()) {
      (void)(*send_packet)(QueryCache::BuildNameQuery(signature.signer_guid));
    }
  }

  return pending_queries;
}

bool PetitionHandler::AppendDisplayedSigner(const std::uint64_t raw_guid,
                                           const std::uint32_t unk) {
  if (raw_guid == 0) {
    return false;
  }

  for (const auto& signature : last_signatures_.signatures) {
    if (signature.signer_guid == raw_guid) {
      return false;
    }
  }

  PetitionSignature signature{};
  signature.signer_guid = raw_guid;
  signature.unk = unk;
  last_signatures_.signatures.push_back(signature);
  return true;
}

bool PetitionHandler::HandleTurnInPetitionResults(const std::uint8_t* data,
                                                   std::size_t len) {
  PacketReader r(data, len);
  TurnInPetitionResult result{};
  if (!r.ReadU32(result.result)) return false;
  last_turn_in_ = result;
  return true;
}

void PetitionHandler::Clear() {
  last_trainer_buy_ = {};
  last_petition_list_ = {};
  ResetActivePetitionState(SignerStorageMode::kRelease);
  last_turn_in_ = {};
  guild_emblem_result_ = 0;
  tabard_save_pending_ = false;
  ClearPetitionVendorInteraction();
  ClearTabardVendorInteraction();
  ClearGuildRegistrarInteraction();
  petition_decline_guid_ = 0;
  last_petition_rename_.reset();
  petition_error_guid_ = 0;
  petition_queries_.clear();
  pending_petition_queries_.Clear();
}

bool PetitionHandler::HandleSaveGuildEmblem(const std::uint8_t* data,
                                            std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU32(guild_emblem_result_)) return false;
  return true;
}

bool PetitionHandler::HandleTabardVendorActivate(const std::uint8_t* data,
                                                 std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU64(tabard_vendor_guid_)) return false;
  return true;
}

bool PetitionHandler::HandlePetitionDecline(const std::uint8_t* data,
                                            std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU64(petition_decline_guid_)) return false;
  return true;
}

bool PetitionHandler::HandlePetitionRename(const std::uint8_t* data,
                                           std::size_t len) {

  constexpr std::size_t kPetitionRenameNameBytesIncludingNul = 0x60;
  PacketReader r(data, len);
  PetitionRenameResult res;
  if (!r.ReadU64(res.guid)) return false;
  if (!r.ReadCString(res.name, kPetitionRenameNameBytesIncludingNul)) return false;
  last_petition_rename_ = std::move(res);
  return true;
}

bool PetitionHandler::UpdateCachedPetitionName(
    const std::uint32_t petition_id, const std::string& new_name) {
  if (petition_id == 0) {
    return false;
  }

  auto it = petition_queries_.find(petition_id);
  if (it != petition_queries_.end()) {
    it->second.name = new_name;
    return true;
  }

  return false;
}

bool PetitionHandler::HandleOfferPetitionError(const std::uint8_t* data,
                                               std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU64(petition_error_guid_)) return false;
  return true;
}

}
