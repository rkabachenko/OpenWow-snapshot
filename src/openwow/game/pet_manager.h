
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "openwow/game/async_query_channel.h"
#include "openwow/game/object_guid.h"
#include "openwow/game/packet_reader.h"
#include "openwow/net/wotlk/protocol/packet_sender.h"
#include "openwow/network/protocol/wotlk/world_packet.h"

namespace openwow::data {
class DBCacheRuntime;
}

namespace openwow::game {

class InteractionSender;
class WorldSession;

enum class PetReactState : std::uint8_t {
  kPassive = 0,
  kDefensive = 1,
  kAggressive = 2,
};

enum class PetCommandState : std::uint8_t {
  kStay = 0,
  kFollow = 1,
  kAttack = 2,
  kAbandon = 3,
};

inline constexpr std::uint32_t kPetActionRequireActiveControlMask = 0x08000000u;

struct PetActionButton {
  static constexpr std::uint32_t kActionIdMask = 0x00FFFFFFu;
  static constexpr std::uint32_t kAutocastEnabledMask = 0x40000000u;
  static constexpr std::uint32_t kAutocastAllowedMask = 0x80000000u;
  static constexpr std::uint32_t kAutocastIdentityMask = 0x3FFFFFFFu;

  std::uint32_t raw{0};

  std::uint32_t ActionId() const {
    return raw & kActionIdMask;
  }
  std::uint8_t ActionType() const {
    return static_cast<std::uint8_t>(raw >> 24);
  }
  std::uint8_t ActionKind() const {
    return static_cast<std::uint8_t>((raw >> 24) & 0x3Fu);
  }
  bool IsAutocastAllowed() const {
    return (raw & kAutocastAllowedMask) != 0;
  }
  bool IsAutocastEnabled() const {
    return (raw & kAutocastEnabledMask) != 0;
  }

  void SetAutocastEnabled(const bool enabled) {
    if (enabled) {
      raw |= kAutocastEnabledMask;
      return;
    }

    raw &= ~kAutocastEnabledMask;
  }

  [[nodiscard]] bool MatchesAutocastIdentity(const PetActionButton &other) const {
    return (raw & kAutocastIdentityMask) == (other.raw & kAutocastIdentityMask);
  }
};

[[nodiscard]] inline constexpr bool IsPetSpellActionKind(
    const std::uint8_t action_kind) noexcept {
  return action_kind == 1 || (action_kind >= 8 && action_kind <= 0x11);
}

struct PetCooldown {
  std::uint32_t spell_id{0};
  std::uint16_t category{0};
  std::uint32_t cooldown_ms{0};
  std::uint32_t category_cooldown_ms{0};
  std::uint32_t gcd_category{0};
  std::uint32_t gcd_duration_ms{0};
  double start_time_s{0.0};
  bool enabled{true};
};

struct PetBarState {
  ObjectGuid guid;
  std::uint16_t creature_family{0};
  std::uint32_t duration_ms{0};
  std::uint32_t timed_pet_deadline_tick{0};
  std::uint32_t mode_packed{0};
  PetReactState react{PetReactState::kPassive};
  PetCommandState command{PetCommandState::kFollow};
  std::uint16_t flags{0};

  PetActionButton action_bar[10]{};
  std::vector<PetActionButton> spells;

  std::vector<std::uint32_t> spellbook_spells;
  std::vector<PetCooldown> cooldowns;

  std::uint32_t possess_spell_id{0};

  bool active{false};
  bool generated_bar_active{false};

  [[nodiscard]] bool RequiresActiveControl() const {
    return (mode_packed & kPetActionRequireActiveControlMask) != 0;
  }

  [[nodiscard]] PetReactState EffectiveReactState() const {
    if (RequiresActiveControl()) {
      return PetReactState::kPassive;
    }

    return react;
  }
};

enum class PetFeedback : std::uint8_t {
  kNone = 0,
  kDisplaySystemMessage358 = 1,
  kClearCooldownDisplay174Or637 = 2,
  kClearCooldownDisplay175 = 3,
  kDisplaySystemMessage359 = 4,
};

struct PetActionFeedbackResult {
  PetFeedback feedback{PetFeedback::kNone};
  std::uint32_t spell_id{0};

  [[nodiscard]] bool HasSpellId() const {
    return spell_id != 0;
  }
};

struct PetCastFailedResult {
  std::uint8_t cast_count{0};
  std::uint32_t spell_id{0};
  std::uint8_t result{0};
  std::optional<std::uint32_t> extra1;
  std::optional<std::uint32_t> extra2;
};

struct PetNameInfo {
  std::uint32_t pet_number{0};
  std::string name;
  std::uint32_t name_timestamp{0};
  bool found{false};
  bool has_declined{false};
  std::string declined_names[5];
};

struct StablePetEntry {
  std::uint32_t pet_number{0};
  std::uint32_t creature_id{0};
  std::uint32_t level{0};
  std::string name;
  std::uint8_t flags{0};
};

struct StableListInfo {
  ObjectGuid npc_guid;
  std::uint8_t max_slots{0};
  std::vector<StablePetEntry> pets;
};

class PetManager {
public:
  explicit PetManager(openwow::data::DBCacheRuntime& db_cache_runtime)
      : db_cache_runtime_(db_cache_runtime) {}

  bool HandlePetSpells(const std::uint8_t *data, std::size_t len);
  bool HandlePetMode(const std::uint8_t *data, std::size_t len);
  bool HandlePetActionFeedback(const std::uint8_t *data, std::size_t len,
                               PetActionFeedbackResult *result = nullptr);
  bool HandlePetCastFailed(const std::uint8_t *data, std::size_t len,
                           PetCastFailedResult *result = nullptr);
  bool HandlePetNameQueryResponse(const std::uint8_t *data, std::size_t len);
  bool HandleStabledPets(const std::uint8_t *data, std::size_t len);
  bool HandlePetGuids(const std::uint8_t *data, std::size_t len);

  static net::wotlk::WorldPacket BuildPetNameQuery(std::uint32_t pet_number,
                                                   const ObjectGuid &guid);
  static net::wotlk::WorldPacket
  BuildPetAction(const ObjectGuid &pet_guid, std::uint32_t action_data, const ObjectGuid &target);
  static net::wotlk::WorldPacket BuildPetCancelAura(const ObjectGuid &pet_guid,
                                                    std::uint32_t spell_id);
  static net::wotlk::WorldPacket
  BuildPetSetAction(const ObjectGuid &pet_guid,
                    std::optional<net::wotlk::PetSetActionSlotState> secondary_slot,
                    net::wotlk::PetSetActionSlotState target_slot);

  const PetBarState &pet_bar() const {
    return pet_bar_;
  }
  PetBarState &mutable_pet_bar() {
    return pet_bar_;
  }
  [[nodiscard]] std::size_t GetSpellbookSpellCount() const;
  [[nodiscard]] std::uint32_t GetSpellbookSpellId(std::size_t slot) const;
  [[nodiscard]] bool HasSpellbookSpellId(std::uint32_t spell_id) const;

  [[nodiscard]] bool HasActionBarSpellId(std::uint32_t spell_id) const;

  [[nodiscard]] const PetActionButton *FindSpellEntryBySpellId(std::uint32_t spell_id) const;
  [[nodiscard]] const PetActionButton *GetSpellbookSpellEntry(std::size_t slot) const;
  [[nodiscard]] std::optional<bool>
  SetSpellAutocastStateBySpellId(std::uint32_t spell_id, std::optional<bool> requested_enabled);

  [[nodiscard]] std::optional<std::uint32_t>
  SetActionBarAutocastState(std::size_t slot, std::optional<bool> requested_enabled);
  PetFeedback last_feedback() const {
    return last_feedback_;
  }
  [[nodiscard]] std::uint32_t last_feedback_spell_id() const {
    return last_feedback_spell_id_;
  }
  [[nodiscard]] bool attack_command_active() const {
    return attack_command_active_;
  }
  [[nodiscard]] bool ClearSpellCooldown(std::uint32_t spell_id);

  void InsertCooldown(const PetCooldown &cooldown);
  [[nodiscard]] std::optional<std::uint32_t> GetTimedPetRemainingMs() const;
  [[nodiscard]] ObjectGuid GetPrimaryPetGuid() const;

  [[nodiscard]] bool IsAttackActionSlot(std::size_t slot) const;
  void SetLocalReactState(PetReactState react);
  void SetLocalCommandState(PetCommandState command);
  void SetAttackCommandActive(bool active);
  bool StopAttackIfActive(InteractionSender &interaction);
  bool RefreshGeneratedBarState(const WorldSession &session);
  [[nodiscard]] bool HasActionBar(const WorldSession &session) const;

  const PetNameInfo *GetPetName(std::uint32_t pet_number) const;

  using PetNameQueryDispatchFn =
      std::function<void(std::uint32_t pet_number, std::uint64_t guid)>;
  using PetNameCallbackKey = AsyncQueryChannel::CallbackKey;
  using PetNameCallback = AsyncQueryChannel::Callback;
  using PetNameRequestOptions = AsyncQueryChannel::RequestOptions;

  const PetNameInfo *GetOrRequestPetName(std::uint32_t pet_number,
                                         std::uint64_t guid = 0);
  const PetNameInfo *GetOrRequestPetName(std::uint32_t pet_number,
                                         PetNameRequestOptions options);

  [[nodiscard]] bool IsPetNameQueryPending(std::uint32_t pet_number) const;
  void MarkPetNameQueryPending(std::uint32_t pet_number);

  void SetPetNameQueryDispatcher(PetNameQueryDispatchFn dispatcher);

  void PumpPetNameQueries(std::uint32_t current_tick_ms);
  void ClearPendingNameQueriesOnLogout();
  void ClearNameCacheForClientCacheVersion();

  void SetPetNameQueryMaxInFlight(std::uint32_t max_in_flight);

  void SetPetNameTickCountProvider(std::function<std::uint32_t()> provider);

  void CancelPetNameCallback(std::uint32_t pet_number, PetNameCallbackKey key);
  void CancelPetNameCallbacks(PetNameCallbackKey key);

  const StableListInfo &stable_list() const {
    return stable_list_;
  }
  const std::vector<std::uint64_t> &pet_guids() const {
    return pet_guids_;
  }
  void ResetStableListState();
  void ResetStablePetSelection() noexcept {
    selected_stable_pet_number_ = 0;
  }
  void SetSelectedStablePetNumber(std::int32_t pet_number) noexcept {
    selected_stable_pet_number_ = pet_number;
  }
  [[nodiscard]] std::int32_t selected_stable_pet_number() const noexcept {
    return selected_stable_pet_number_;
  }
  void CloseStableList() {
    stable_list_.npc_guid = ObjectGuid{};
  }
  void IncrementStableMaxSlots() {
    ++stable_list_.max_slots;
  }

  std::uint32_t last_pet_cast_spell() const {
    return last_pet_cast_spell_;
  }
  std::uint8_t last_pet_cast_result() const {
    return last_pet_cast_result_;
  }

  void Clear();

  void ResetPetBarForEnterWorld();

private:
  openwow::data::DBCacheRuntime& db_cache_runtime_;

  void SyncSpellEntryFromActionBar(const PetActionButton &action_button);

  PetBarState pet_bar_;
  PetFeedback last_feedback_{PetFeedback::kNone};
  std::uint32_t last_feedback_spell_id_{0};
  std::uint32_t last_pet_cast_spell_{0};
  std::uint8_t last_pet_cast_result_{0};

  bool attack_command_active_{false};

  mutable std::unordered_map<std::uint32_t, PetNameInfo> pet_names_;
  AsyncQueryChannel pet_name_queries_;
  std::function<std::uint32_t()> pet_name_tick_provider_;
  StableListInfo stable_list_;
  std::int32_t selected_stable_pet_number_{0};
  std::vector<std::uint64_t> pet_guids_;

  [[nodiscard]] PetActionButton *FindMutableSpellEntryBySpellId(std::uint32_t spell_id);
};

}
