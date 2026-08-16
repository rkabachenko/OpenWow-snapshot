#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::game {

class WorldSession;
struct TrainerSpell;

struct TrainerSkillStepEffectView {
  std::string_view skill_name;
  std::uint16_t skill_line_id = 0;
  std::uint32_t required_skill_rank = 0;
};

struct TrainerSkillStepIncreaseView {
  std::string_view skill_name;
  std::uint32_t rank_increase = 0;
};

struct TrainerServiceInfoView {
  std::string_view name;
  std::string_view subtext;
  std::string_view availability;
  bool is_header = false;
  bool show_numeric_flag = false;
};

enum class TrainerServiceTooltipTargetKind : std::uint8_t {
  Spell,
  Item,
};

struct TrainerServiceTooltipTarget {
  TrainerServiceTooltipTargetKind kind = TrainerServiceTooltipTargetKind::Spell;
  std::uint32_t id = 0;
};

std::uint32_t Trainer_ParseFilterString(const char *filter_str);

void Trainer_ResetFrameState();
void Trainer_UpdateGreetingText(const WorldSession &session);
const std::string &Trainer_GetGreetingText();

std::uint32_t Trainer_GetVisibleServiceCount(const WorldSession &session,
                                             const openwow::data::dbc::DbcLoader *dbc);

bool Trainer_GetServiceInfo(const WorldSession &session, const openwow::data::dbc::DbcLoader *dbc,
                            std::uint32_t one_based_index, TrainerServiceInfoView *out_view);

std::optional<std::size_t>
Trainer_ResolveVisibleSpellIndex(const WorldSession &session,
                                 const openwow::data::dbc::DbcLoader *dbc,
                                 std::uint32_t one_based_index);

bool Trainer_IsVisibleServiceAvailable(const WorldSession &session,
                                       const openwow::data::dbc::DbcLoader *dbc,
                                       std::uint32_t one_based_index);

std::optional<std::uint32_t>
Trainer_ResolveVisibleServiceCreatedItemId(const WorldSession &session,
                                           const openwow::data::dbc::DbcLoader *dbc,
                                           std::uint32_t one_based_index);

std::optional<TrainerServiceTooltipTarget>
Trainer_ResolveVisibleServiceTooltipTarget(const WorldSession &session,
                                           const openwow::data::dbc::DbcLoader *dbc,
                                           std::uint32_t one_based_index);

std::optional<std::string_view>
Trainer_GetVisibleServiceSkillLineName(const WorldSession &session,
                                       const openwow::data::dbc::DbcLoader *dbc,
                                       std::uint32_t one_based_index);

std::optional<TrainerSkillStepEffectView>
Trainer_ResolveSkillStepEffect(const openwow::data::dbc::DbcLoader &dbc,
                               std::int32_t step_spell_id, std::uint32_t player_level);

bool Trainer_HasSkillStepEffect(const openwow::data::dbc::DbcLoader &dbc,
                                std::int32_t spell_id);

std::optional<TrainerSkillStepIncreaseView>
Trainer_ResolveServiceStepIncrease(const openwow::data::dbc::DbcLoader &dbc,
                                   const TrainerSpell &trainer_spell,
                                   std::uint32_t player_level);

std::uint32_t Trainer_GetSkillLineCount(const WorldSession &session,
                                        const openwow::data::dbc::DbcLoader *dbc);

std::optional<std::string_view> Trainer_GetSkillLineName(const WorldSession &session,
                                                         const openwow::data::dbc::DbcLoader *dbc,
                                                         std::uint32_t one_based_index);

bool Trainer_GetSkillLineFilterEnabled(const WorldSession &session,
                                       const openwow::data::dbc::DbcLoader *dbc,
                                       std::int32_t one_based_index);

bool Trainer_SetSkillLineFilter(const WorldSession &session,
                                const openwow::data::dbc::DbcLoader *dbc,
                                std::int32_t one_based_index, bool enabled, bool exclusive);

bool Trainer_CollapseSkillLine(const WorldSession &session,
                               const openwow::data::dbc::DbcLoader *dbc,
                               std::int32_t one_based_service_index);

bool Trainer_CollapseSkillLine(std::int32_t one_based_service_index);

bool Trainer_ExpandSkillLine(const WorldSession &session, const openwow::data::dbc::DbcLoader *dbc,
                             std::int32_t one_based_service_index);

std::optional<std::uint32_t> Trainer_GetSelectionIndex(const WorldSession &session,
                                                       const openwow::data::dbc::DbcLoader *dbc);

void Trainer_SelectVisibleService(const WorldSession &session,
                                  const openwow::data::dbc::DbcLoader *dbc,
                                  std::int32_t one_based_index);

bool Trainer_GetServiceTypeFilterEnabled(std::uint32_t filter_bucket);
void Trainer_SetServiceTypeFilter(std::uint32_t filter_bucket, bool enabled, bool exclusive);
void Trainer_ResetServiceTypeFilters();

}
