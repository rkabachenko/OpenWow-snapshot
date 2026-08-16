
#include "openwow/game/tabard_frame.h"

#include "openwow/runtime/time/game_clock.h"
#include "openwow/foundation/hashing/retail_adler_seed.h"
#include "openwow/game/guild_manager.h"
#include "openwow/game/player_npc_interaction.h"
#include "openwow/game/world_session.h"
#include "openwow/network/protocol/wotlk/world_packet.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/game/script_event_dispatch.h"

#include <algorithm>
#include <array>
#include <bit>

namespace openwow::game {

namespace {

constexpr std::uint32_t kGuildEmblemResultSuccessMessage = 317;
constexpr std::uint32_t kGuildEmblemResultInvalidColorsMessage = 318;
constexpr std::uint32_t kGuildEmblemResultNoGuildMessage = 319;
constexpr std::uint32_t kGuildEmblemResultNotGuildMasterMessage = 320;
constexpr std::uint32_t kGuildEmblemResultNotEnoughMoneyMessage = 321;
constexpr std::uint32_t kGuildEmblemSameMessage = 472;
constexpr std::uint32_t kGuildEmblemNoDisplayMessage = 730;
constexpr std::uint32_t kNotEnoughMoneyMessage = 40;
constexpr std::uint32_t kGuildEmblemResultSuccess = 0;

constexpr std::array<std::uint32_t, 6> kGuildEmblemResultMessages{
    kGuildEmblemResultSuccessMessage,
    kGuildEmblemResultInvalidColorsMessage,
    kGuildEmblemResultNoGuildMessage,
    kGuildEmblemResultNotGuildMasterMessage,
    kGuildEmblemResultNotEnoughMoneyMessage,
    kGuildEmblemNoDisplayMessage,
};

[[nodiscard]] bool AreTabardDesignValuesValid(
    const std::array<std::uint32_t, kTabardNumAxes>& design_values) {
  for (std::size_t index = 0; index < design_values.size(); ++index) {
    if (design_values[index] >= kTabardVariationLimits[index]) {
      return false;
    }
  }

  return true;
}

[[nodiscard]] bool TryGetCachedGuildEmblem(const WorldSession& session,
                                           GuildEmblem* emblem) {
  const auto* player = session.objects().GetActivePlayer();
  if (player == nullptr) {
    return false;
  }

  const auto guild_id = player->GetGuildID();
  if (guild_id == 0) {
    return false;
  }

  const auto* guild_info = session.guild().FindCachedGuildInfo(guild_id);
  if (guild_info == nullptr) {
    return false;
  }

  if (emblem != nullptr) {
    *emblem = guild_info->emblem;
  }

  return true;
}

[[nodiscard]] bool MatchesGuildEmblem(
    const std::array<std::uint32_t, kTabardNumAxes>& design_values,
    const GuildEmblem& emblem) {
  return design_values[0] == emblem.style &&
         design_values[1] == emblem.color &&
         design_values[2] == emblem.border_style &&
         design_values[3] == emblem.border_color &&
         design_values[4] == emblem.background_color;
}

[[nodiscard]] std::int32_t BitCastSigned(std::uint32_t bits) {
  return std::bit_cast<std::int32_t>(bits);
}

[[nodiscard]] std::uint32_t BitCastUnsigned(std::int32_t value) {
  return std::bit_cast<std::uint32_t>(value);
}

[[nodiscard]] std::int32_t TabardCycleAbsoluteValue(std::int32_t delta) {
  const auto sign_mask = std::uint32_t{0} - static_cast<std::uint32_t>(delta < 0);
  const auto magnitude_bits = (BitCastUnsigned(delta) ^ sign_mask) - sign_mask;
  return BitCastSigned(magnitude_bits);
}

[[nodiscard]] std::uint32_t TabardCycleWrappedValue(std::uint32_t current_bits,
                                                    std::int32_t delta,
                                                    std::int32_t limit) {
  const auto delta_plus_limit_bits =
      BitCastUnsigned(delta) + static_cast<std::uint32_t>(limit);
  const auto dividend_bits = current_bits + delta_plus_limit_bits;
  const auto dividend = BitCastSigned(dividend_bits);
  return BitCastUnsigned(dividend % limit);
}

}

void TabardFrame_RefreshActivePlayerPreview(WorldSession *session) {
    if (session == nullptr || session->objects().GetActivePlayer() == nullptr) {
        return;
    }

    const std::uint64_t active_player_guid =
        session->objects().GetActivePlayerGuid().GetRawValue();
    if (active_player_guid == 0) {
        return;
    }

    auto &dispatch = openwow::ui::game::ScriptEventDispatch::Get();
    dispatch.FireUnitPortrait(active_player_guid);
    dispatch.FireUnitModel(active_player_guid);
}

bool TabardFrame_CycleVariation(uint32_t* designValues,
                                 uint32_t axisIndex, int32_t delta) {
    if (axisIndex >= kTabardNumAxes)
        return false;

    const auto limit = static_cast<std::int32_t>(kTabardVariationLimits[axisIndex]);
    if (TabardCycleAbsoluteValue(delta) >= limit)
        return false;

    designValues[axisIndex] =
        TabardCycleWrappedValue(designValues[axisIndex], delta, limit);
    return true;
}

bool TabardFrame_Save(WorldSession& session,
                      const std::array<std::uint32_t, kTabardNumAxes>& design_values,
                      const std::uint64_t vendor_guid) {
    const auto* player = session.objects().GetActivePlayer();
    if (player == nullptr) {
        return false;
    }

    if (!AreTabardDesignValuesValid(design_values)) {
        openwow::ui::game::DisplaySystemMessage(
            static_cast<int>(kGuildEmblemResultInvalidColorsMessage));
        return false;
    }

    GuildEmblem emblem{};
    if (!TryGetCachedGuildEmblem(session, &emblem)) {
        openwow::ui::game::DisplaySystemMessage(
            static_cast<int>(kGuildEmblemResultNoGuildMessage));
        return false;
    }

    if (MatchesGuildEmblem(design_values, emblem)) {
        openwow::ui::game::DisplaySystemMessage(
            static_cast<int>(kGuildEmblemSameMessage));
        return false;
    }

    if (player->GetGuildRank() != 0) {
        openwow::ui::game::DisplaySystemMessage(
            static_cast<int>(kGuildEmblemResultNotGuildMasterMessage));
        return false;
    }

    if (player->GetMoney() < kTabardCreationCostCopper) {
        openwow::ui::game::DisplaySystemMessage(
            static_cast<int>(kNotEnoughMoneyMessage));
        return false;
    }

    openwow::net::wotlk::WorldPacket packet(
        openwow::net::wotlk::Opcode::MSG_SAVE_GUILD_EMBLEM);
    packet.AppendU64(vendor_guid);
    packet.AppendU32(design_values[0]);
    packet.AppendU32(design_values[1]);
    packet.AppendU32(design_values[2]);
    packet.AppendU32(design_values[3]);
    packet.AppendU32(design_values[4]);
    session.interaction().SendRawPacket(packet);
    session.petition().MarkTabardSavePending();
    openwow::ui::game::ScriptEventDispatch::Get().FireEvent(
        openwow::ui::game::events::TABARD_SAVE_PENDING);
    return true;
}

void TabardFrame_HandleSaveResult(WorldSession& session,
                                  const std::uint32_t result_code) {
    if (result_code >= kGuildEmblemResultMessages.size()) {
        return;
    }

    const auto message_id = kGuildEmblemResultMessages[result_code];
    if (message_id != kGuildEmblemNoDisplayMessage) {
        openwow::ui::game::DisplaySystemMessage(static_cast<int>(message_id));
    }

    session.petition().ClearTabardSavePending();

    if (result_code != kGuildEmblemResultSuccess) {
        openwow::ui::game::ScriptEventDispatch::Get().FireEvent(
            openwow::ui::game::events::TABARD_SAVE_PENDING);
        return;
    }

    const auto* player = session.objects().GetActivePlayer();
    if (player != nullptr) {
        session.guild().InvalidateCachedGuildInfo(player->GetGuildID());
    }

    openwow::ui::game::ScriptEventDispatch::Get().FireEvent(
        openwow::ui::game::events::GUILDTABARD_UPDATE);
}

bool TabardFrame_InitializeColors(WorldSession* session,
                                  uint32_t* design_values) {
    if (session == nullptr || design_values == nullptr) {
        return false;
    }

    const auto* player = session->objects().GetActivePlayer();
    if (player == nullptr) {
        return false;
    }

    GuildEmblem emblem{};
    if (TryGetCachedGuildEmblem(*session, &emblem) && HasResolvedGuildEmblem(emblem)) {
        design_values[0] = emblem.style;
        design_values[1] = emblem.color;
        design_values[2] = emblem.border_style;
        design_values[3] = emblem.border_color;
        design_values[4] = emblem.background_color;
        return true;
    }

    TabardFrame_InitializeRandomDesign(
        design_values, openwow::core::GameClock::GetTickCount32());
    return true;
}

void TabardFrame_InitializeRandomDesign(uint32_t* design_values,
                                        uint32_t tick_count32) {
    if (!design_values) {
        return;
    }

    openwow::foundation::hashing::AdlerSeedState state =
        openwow::foundation::hashing::MakeAdlerSeedState(tick_count32);
    for (uint32_t i = 0; i < kTabardNumAxes; ++i) {
        design_values[i] =
            openwow::foundation::hashing::AdlerSeedNextBoundedValue(
                kTabardVariationLimits[i], state);
    }
}

}
