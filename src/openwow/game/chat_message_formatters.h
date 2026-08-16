
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace openwow::game {

class ObjectManager;
class WorldSession;

struct ServerFirstInfo;
void FormatServerFirstAchievement(const ObjectManager& objects,
                                  const ServerFirstInfo& info);

void FormatTargetIconSet(const WorldSession& session, std::uint64_t setter_guid,
                         std::uint64_t target_guid, int icon_index);

void FormatXPGainFirstPerson(const ObjectManager& objects,
                             std::uint64_t source_guid, int xp_amount,
                             float group_rate);

void FormatXPGainDetailed(const ObjectManager& objects,
                          std::uint64_t source_guid, int xp_total, int xp_base,
                          int quest_flag, float group_rate,
                          bool is_refer_a_friend);

void FormatXPLoss(const ObjectManager& objects, int xp_amount);

void FormatHonorGain(WorldSession& session, std::uint64_t victim_guid,
                     int rank, int honor_points);

void FormatFeedPetLog(WorldSession& session, std::uint64_t caster_guid,
                      int item_entry);

void FormatSpellDismissPet(const WorldSession& session,
                           std::uint64_t caster_guid,
                           std::uint64_t pet_guid);

void FormatTradeskillLog(WorldSession& session,
                         std::uint64_t crafter_guid, int item_entry);

void FormatDurabilityDamageDeath(const ObjectManager& objects);

void FormatResetFailedNotify(const ObjectManager& objects);

void FormatInstanceSaveCreated(const ObjectManager& objects,
                               std::uint32_t flag);

void FormatVoiceChatParentalError(const ObjectManager& objects, int error_code);

void HandleZoneUnderAttack(const void* packet_data);

void HandleTitleEarned(const void* packet_data);

void HandleXPGainPacket(const WorldSession& session, const void* packet_data,
                        std::size_t packet_size);

void FormatOpenLockMessage(const WorldSession& session,
                           std::uint64_t caster_guid,
                           const std::string& skill_name,
                           int required_skill);

void HandleOpenLockEvent(WorldSession& session, std::uint64_t caster_guid,
                         std::uint64_t target_guid, int spell_index);

class QueryCache;
[[nodiscard]] std::optional<std::string>
BuildRandomRollResultText(const QueryCache& cache,
                          std::uint64_t roller_guid,
                          std::uint32_t min_val,
                          std::uint32_t max_val,
                          std::uint32_t result);
void FormatRandomRollResult(const ObjectManager& objects,
                            const QueryCache& cache,
                            std::uint64_t roller_guid,
                            std::uint32_t min_val,
                            std::uint32_t max_val,
                            std::uint32_t result);

}
