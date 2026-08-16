#include "openwow/game/trainer_frame.h"

#include "openwow/core/storm_string.h"
#include "openwow/data/formats/dbc/dbc_entries_gameplay.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/game/gossip_manager.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/skill_line_ability_lookup.h"
#include "openwow/game/spell_text_formatter.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/script_event_dispatch.h"

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace openwow::game {
namespace {

constexpr std::uint32_t kTrainerTypeTradeskill = 2;
constexpr std::uint32_t kTrainerAvailabilityMaskAll = 0x7u;
constexpr char kTrainerServiceTypeFilterCVarName[] = "serviceTypeFilter";
constexpr std::uint32_t kHiddenSkillLineFlag = 0x80000000u;
constexpr std::uint32_t kTradeskillTrainerCategorySkillStep = 1u;
constexpr std::uint32_t kTradeskillTrainerCategoryDefault = 2u;
constexpr std::uint32_t kTradeskillTrainerCategoryAttribute40 = 3u;
constexpr std::uint32_t kTradeskillTrainerCategoryAttribute40Mask = 0x40u;
constexpr std::uint32_t kSpellEffectLearnSpell = 36u;
constexpr std::uint32_t kSpellEffectTooltipProxy = 57u;
constexpr std::uint32_t kSpellEffectSkillStep = 44u;
constexpr std::uint32_t kInvalidSpellId = std::numeric_limits<std::uint32_t>::max();

constexpr std::uint32_t kTrainerAllSkillLinesCollapsedMask = 0u;

struct TrainerSkillCategoryView {
  std::uint32_t category_id = 0;
  std::array<std::uint32_t, 3> count_by_state = {};
  bool has_point_costs = false;
  bool passes_skill_line_filter = true;
  bool collapsed = false;
  std::string name;
};

struct TrainerServiceViewState {
  std::optional<std::size_t> raw_spell_index;
  std::uint32_t spell_id = kInvalidSpellId;
  std::uint32_t category_id = 0;
  std::uint32_t required_level = 0;
  std::uint32_t required_skill_value = 0;
  std::uint8_t availability_bucket = 1;
  bool visible = true;
  bool is_header = false;
  std::string name;
  std::string subtext;
  std::string skill_line_name;
};

std::uint32_t HashCombine(std::uint32_t seed, std::uint32_t value) {
  return seed ^ (value + 0x9e3779b9u + (seed << 6) + (seed >> 2));
}

std::uint8_t ResolveTrainerAvailabilityBucket(const TrainerSpellState state) {
  return static_cast<std::uint8_t>(state == TrainerSpellState::Available ? 0
                                   : state == TrainerSpellState::Known    ? 2
                                                                          : 1);
}

int CompareTrainerServiceNames(const TrainerServiceViewState &left,
                               const TrainerServiceViewState &right) {
  if (left.name.empty() || right.name.empty()) {
    return 0;
  }

  return openwow::core::SStrCmpNoCaseCollate(left.name.c_str(), right.name.c_str(), 0x7FFFFFFFu);
}

template <typename CompareWithinCategory, typename CategoryOrder>
bool CompareTrainerServices(const TrainerServiceViewState &left,
                            const TrainerServiceViewState &right,
                            const CompareWithinCategory &compare_within_category,
                            const CategoryOrder &category_order) {
  if (left.visible != right.visible) {
    return left.visible && !right.visible;
  }

  const auto left_category_order = category_order(left.category_id);
  const auto right_category_order = category_order(right.category_id);
  if (left_category_order != right_category_order) {
    return left_category_order < right_category_order;
  }

  if (left.is_header != right.is_header) {
    return left.is_header;
  }
  if (left.is_header && right.is_header) {
    return false;
  }

  return compare_within_category(left, right);
}

bool CompareClassTrainerServices(const TrainerServiceViewState &left,
                                 const TrainerServiceViewState &right) {
  if (left.required_level != right.required_level) {
    return left.required_level < right.required_level;
  }
  if (left.required_skill_value != right.required_skill_value) {
    return left.required_skill_value < right.required_skill_value;
  }

  const auto name_compare = CompareTrainerServiceNames(left, right);
  if (name_compare != 0) {
    return name_compare < 0;
  }
  if (left.name.empty() || right.name.empty()) {
    return false;
  }

  if (!left.subtext.empty() && !right.subtext.empty()) {
    return openwow::core::SStrCmpNoCaseCollate(left.subtext.c_str(), right.subtext.c_str(),
                                               0x7FFFFFFFu) < 0;
  }

  return false;
}

bool CompareTradeskillTrainerServices(const TrainerServiceViewState &left,
                                      const TrainerServiceViewState &right) {
  if (left.required_skill_value != right.required_skill_value) {
    return left.required_skill_value < right.required_skill_value;
  }

  const auto name_compare = CompareTrainerServiceNames(left, right);
  if (name_compare != 0) {
    return name_compare < 0;
  }

  return false;
}

TrainerSpellState ResolveTrainerSpellState(const TrainerSpell &spell,
                                           const TrainerUnlearnSpellCache &unlearn_spell_cache) {
  if (unlearn_spell_cache.ContainsRawSpellId(spell.spell_id)) {
    return TrainerSpellState::Known;
  }

  return spell.state;
}

bool IsTrainerTooltipItemEffect(const std::uint32_t effect_id) {
  return effect_id == 24u || effect_id == 59u || effect_id == 157u;
}

const TrainerSpell *ResolveVisibleTrainerSpell(const WorldSession &session,
                                               const openwow::data::dbc::DbcLoader *dbc,
                                               const std::uint32_t one_based_index) {
  const auto spell_index = Trainer_ResolveVisibleSpellIndex(session, dbc, one_based_index);
  if (!spell_index.has_value() || !session.gossip().has_trainer()) {
    return nullptr;
  }

  const auto &trainer = session.gossip().trainer();
  if (*spell_index >= trainer.spells.size()) {
    return nullptr;
  }
  return &trainer.spells[*spell_index];
}

const openwow::data::dbc::SpellEntry *
LookupTrainerSpellEntry(const openwow::data::dbc::DbcLoader *dbc, const std::int32_t spell_id) {
  if (dbc == nullptr || spell_id <= 0) {
    return nullptr;
  }

  return dbc->spell().LookupEntry(static_cast<std::uint32_t>(spell_id));
}

std::int32_t ComputeTrainerSpellEffectMaxValue(const openwow::data::dbc::SpellEntry &spell,
                                               const std::size_t effect_index,
                                               const std::uint32_t player_level) {
  const auto level_delta = player_level > spell.base_level ? player_level - spell.base_level : 0u;
  const float scaled_max =
      static_cast<float>(spell.effect_base_points[effect_index] +
                         spell.effect_die_sides[effect_index]) +
      spell.effect_real_points_per_lvl[effect_index] * static_cast<float>(level_delta);
  const auto max_value = static_cast<std::int32_t>(scaled_max);
  return max_value > 0 ? max_value : 0;
}

std::optional<std::uint32_t>
ResolveTrainerCreatedItemId(const openwow::data::dbc::SpellEntry &spell) {
  for (std::size_t effect_index = 0; effect_index < spell.effect.size(); ++effect_index) {
    if (!IsTrainerTooltipItemEffect(spell.effect[effect_index])) {
      continue;
    }

    const auto item_id = spell.effect_item_type[effect_index];
    if (item_id != 0) {
      return item_id;
    }
  }

  return std::nullopt;
}

const openwow::data::dbc::SpellEntry *
ResolveTrainerTooltipSpell(const openwow::data::dbc::DbcLoader &dbc,
                           const openwow::data::dbc::SpellEntry &spell) {
  for (std::size_t effect_index = 0; effect_index < spell.effect.size(); ++effect_index) {
    const auto effect_id = spell.effect[effect_index];
    if (effect_id != kSpellEffectLearnSpell && effect_id != kSpellEffectTooltipProxy) {
      continue;
    }

    const auto trigger_spell_id = spell.effect_trigger_spell[effect_index];
    if (trigger_spell_id == 0) {
      continue;
    }

    const auto *trigger_spell = dbc.spell().LookupEntry(trigger_spell_id);
    if (trigger_spell != nullptr) {
      return trigger_spell;
    }
  }

  return nullptr;
}

void EnsureTrainerServiceTypeFilterCVar() {
  auto &cvars = openwow::ui::game::CVarSystem::Instance();
  if (!cvars.Exists(kTrainerServiceTypeFilterCVarName)) {
    cvars.RegisterCVar(kTrainerServiceTypeFilterCVarName, "3",
                       openwow::ui::game::CVarFlags::Archive, "Which trainer services to show");
  }
}

std::uint32_t GetTrainerServiceTypeFilterMask() {
  EnsureTrainerServiceTypeFilterCVar();
  return static_cast<std::uint32_t>(
      openwow::ui::game::CVarSystem::Instance().GetCVarInt(kTrainerServiceTypeFilterCVarName));
}

void StoreTrainerServiceTypeFilterMask(const std::uint32_t filter_mask) {
  EnsureTrainerServiceTypeFilterCVar();
  openwow::ui::game::CVarSystem::Instance().SetCVar(kTrainerServiceTypeFilterCVarName,
                                                    std::to_string(filter_mask));
}

class TrainerFrameCache {
public:
  void Reset() {
    signature_ = 0;
    dbc_ = nullptr;
    trainer_type_ = 0;
    availability_filter_mask_ = GetTrainerServiceTypeFilterMask();
    skill_line_filter_mask_ = std::numeric_limits<std::uint32_t>::max();
    collapse_mask_ = std::numeric_limits<std::uint32_t>::max();
    selected_spell_id_ = 0;
    greeting_text_.clear();
    categories_.clear();
    services_.clear();
    visible_service_count_ = 0;
  }

  void Sync(const WorldSession &session, const openwow::data::dbc::DbcLoader *dbc) {
    const auto stored_filter_mask = GetTrainerServiceTypeFilterMask();
    const bool filter_mask_changed = stored_filter_mask != availability_filter_mask_;
    availability_filter_mask_ = stored_filter_mask;

    const auto *active_player = session.objects().GetActivePlayer();
    const auto *trainer = session.gossip().has_trainer() ? &session.gossip().trainer() : nullptr;
    const auto next_signature =
        ComputeSignature(trainer, active_player, session.trainer_unlearn_spell_cache());

    if (next_signature == signature_ && dbc == dbc_) {
      if (filter_mask_changed) {
        RebuildVisibleServices();
      }
      return;
    }

    signature_ = next_signature;
    dbc_ = dbc;

    if (trainer == nullptr) {
      trainer_type_ = 0;
      categories_.clear();
      services_.clear();
      visible_service_count_ = 0;
      selected_spell_id_ = 0;
      return;
    }

    skill_line_filter_mask_ = std::numeric_limits<std::uint32_t>::max();
    collapse_mask_ = std::numeric_limits<std::uint32_t>::max();
    Build(*trainer, dbc, active_player, session.trainer_unlearn_spell_cache());
  }

  std::uint32_t visible_service_count() const {
    return visible_service_count_;
  }

  const TrainerServiceViewState *GetVisibleService(std::uint32_t one_based_index) const {
    if (one_based_index == 0 || one_based_index > visible_service_count_) {
      return nullptr;
    }
    return &services_[one_based_index - 1];
  }

  const TrainerServiceViewState *GetService(std::uint32_t one_based_index) const {
    if (one_based_index == 0 || one_based_index > services_.size()) {
      return nullptr;
    }
    return &services_[one_based_index - 1];
  }

  const TrainerSkillCategoryView *GetSkillLine(std::uint32_t one_based_index) const {
    if (one_based_index == 0 || one_based_index > categories_.size()) {
      return nullptr;
    }
    return &categories_[one_based_index - 1];
  }

  std::uint32_t skill_line_count() const {
    return static_cast<std::uint32_t>(categories_.size());
  }

  bool IsSkillLineExpanded(const std::uint32_t skill_line_id) const {
    const auto *category = FindCategory(skill_line_id);
    return category != nullptr && !category->collapsed;
  }

  bool GetSkillLineFilterEnabled(std::int32_t one_based_index) const {
    if (one_based_index <= 0) {
      for (std::uint32_t index = 0; index < categories_.size(); ++index) {
        if ((skill_line_filter_mask_ & (std::uint32_t{1} << index)) == 0) {
          return false;
        }
      }
      return true;
    }

    const auto zero_based_index = static_cast<std::uint32_t>(one_based_index - 1);
    if (zero_based_index >= categories_.size()) {
      return false;
    }
    return (skill_line_filter_mask_ & (std::uint32_t{1} << zero_based_index)) != 0;
  }

  bool SetSkillLineFilter(std::int32_t one_based_index, bool enabled, bool exclusive) {
    if (one_based_index <= 0) {
      skill_line_filter_mask_ = std::numeric_limits<std::uint32_t>::max();
      RebuildVisibleServices();
      return true;
    }

    const auto zero_based_index = static_cast<std::uint32_t>(one_based_index - 1);
    if (zero_based_index >= categories_.size()) {
      return false;
    }

    if (!enabled) {
      skill_line_filter_mask_ &= ~(std::uint32_t{1} << zero_based_index);
    } else if (exclusive) {
      skill_line_filter_mask_ = std::uint32_t{1} << zero_based_index;
    } else {
      skill_line_filter_mask_ |= std::uint32_t{1} << zero_based_index;
    }

    RebuildVisibleServices();
    return true;
  }

  bool CollapseSkillLine(std::int32_t one_based_service_index) {
    if (one_based_service_index <= 0) {
      collapse_mask_ = kTrainerAllSkillLinesCollapsedMask;
      RebuildVisibleServices();
      return true;
    }

    const auto bit_index = ResolveSkillLineBitIndexForServiceRow(one_based_service_index);
    if (!bit_index.has_value()) {
      return false;
    }

    collapse_mask_ &= ~(std::uint32_t{1} << *bit_index);
    RebuildVisibleServices();
    return true;
  }

  bool ExpandSkillLine(std::int32_t one_based_service_index) {
    if (one_based_service_index <= 0) {
      collapse_mask_ = std::numeric_limits<std::uint32_t>::max();
      RebuildVisibleServices();
      return true;
    }

    const auto bit_index = ResolveSkillLineBitIndexForServiceRow(one_based_service_index);
    if (!bit_index.has_value()) {
      return false;
    }

    collapse_mask_ |= std::uint32_t{1} << *bit_index;
    RebuildVisibleServices();
    return true;
  }

  std::optional<std::uint32_t> GetSelectionIndex() const {
    if (selected_spell_id_ == 0) {
      return std::nullopt;
    }

    for (std::uint32_t index = 0; index < services_.size(); ++index) {
      if (services_[index].spell_id == selected_spell_id_) {
        return index + 1;
      }
    }

    return std::nullopt;
  }

  void SelectVisibleService(const std::int32_t one_based_index) {
    if (one_based_index <= 0) {
      selected_spell_id_ = 0;
      return;
    }

    const auto *service = GetVisibleService(static_cast<std::uint32_t>(one_based_index));
    if (service == nullptr || service->is_header || service->spell_id == kInvalidSpellId) {
      selected_spell_id_ = 0;
      return;
    }
    selected_spell_id_ = service->spell_id;
  }

  bool GetServiceTypeFilterEnabled(std::uint32_t filter_bucket) const {
    if (filter_bucket >= 3) {
      return false;
    }
    const auto filter_mask = GetTrainerServiceTypeFilterMask();
    return (filter_mask & (std::uint32_t{1} << filter_bucket)) != 0;
  }

  void SetServiceTypeFilterMask(const std::uint32_t filter_mask) {
    availability_filter_mask_ = filter_mask;
    RebuildVisibleServices();
  }

  void SetGreetingText(std::string text) {
    greeting_text_ = std::move(text);
  }

  const std::string &greeting_text() const {
    return greeting_text_;
  }

private:
  static std::uint32_t ComputeSignature(const TrainerList *trainer, const CGPlayer_C *active_player,
                                        const TrainerUnlearnSpellCache &unlearn_spell_cache) {
    if (trainer == nullptr) {
      return 0;
    }

    std::uint32_t signature = static_cast<std::uint32_t>(trainer->trainer_guid.GetRawValue());
    signature = HashCombine(signature, static_cast<std::uint32_t>(trainer->trainer_type));
    if (active_player != nullptr) {
      signature = HashCombine(signature, active_player->State().GetRace());
      signature = HashCombine(signature, active_player->State().GetClass());
    }

    for (const auto &spell : trainer->spells) {
      signature = HashCombine(signature, static_cast<std::uint32_t>(spell.spell_id));
      signature = HashCombine(signature, static_cast<std::uint32_t>(spell.state));
      signature = HashCombine(signature, static_cast<std::uint32_t>(spell.req_level));
      signature = HashCombine(signature, static_cast<std::uint32_t>(spell.req_skill_rank));
      signature = HashCombine(signature, static_cast<std::uint32_t>(spell.point_cost_0));
      signature = HashCombine(signature, static_cast<std::uint32_t>(spell.point_cost_1));
      signature = HashCombine(signature, static_cast<std::uint32_t>(spell.req_abilities[0]));
      signature = HashCombine(signature, static_cast<std::uint32_t>(spell.req_abilities[1]));
      signature = HashCombine(signature, static_cast<std::uint32_t>(spell.req_abilities[2]));
    }

    signature = HashCombine(signature, unlearn_spell_cache.Signature());
    return signature;
  }

  void Build(const TrainerList &trainer, const openwow::data::dbc::DbcLoader *dbc,
             const CGPlayer_C *active_player, const TrainerUnlearnSpellCache &unlearn_spell_cache) {
    trainer_type_ = trainer.trainer_type;
    categories_.clear();
    services_.clear();
    visible_service_count_ = 0;

    const bool has_trainer_spell_metadata =
        dbc != nullptr &&
        std::any_of(trainer.spells.begin(), trainer.spells.end(),
                    [dbc](const TrainerSpell &spell) {
                      return spell.spell_id > 0 &&
                             dbc->spell().LookupEntry(
                                 static_cast<std::uint32_t>(spell.spell_id)) != nullptr;
                    });
    if (!has_trainer_spell_metadata) {
      BuildFlatServices(trainer, dbc, active_player, unlearn_spell_cache);
      return;
    }

    if (trainer.trainer_type == kTrainerTypeTradeskill) {
      BuildTradeskillServices(trainer, *dbc, active_player, unlearn_spell_cache);
      return;
    }

    if (active_player == nullptr) {
      BuildFlatServices(trainer, dbc, active_player, unlearn_spell_cache);
      return;
    }

    BuildClassServices(trainer, *dbc, *active_player, unlearn_spell_cache);
    if (services_.empty()) {
      BuildFlatServices(trainer, dbc, active_player, unlearn_spell_cache);
    }
  }

  void PopulateServiceDisplay(const openwow::data::dbc::DbcLoader *dbc,
                              const CGPlayer_C *active_player, const TrainerSpell &spell,
                              const TrainerUnlearnSpellCache &unlearn_spell_cache,
                              TrainerServiceViewState *service) const {
    service->spell_id = static_cast<std::uint32_t>(spell.spell_id);
    service->required_level = spell.req_level;
    service->required_skill_value =
        spell.req_skill_rank < 0 ? 0 : static_cast<std::uint32_t>(spell.req_skill_rank);
    service->availability_bucket =
        ResolveTrainerAvailabilityBucket(ResolveTrainerSpellState(spell, unlearn_spell_cache));
    if (dbc == nullptr) {
      return;
    }

    PopulateSpellDisplay(*dbc, spell.spell_id, &service->name, &service->subtext);
    const auto player_race = active_player != nullptr ? active_player->State().GetRace() : std::uint8_t{0};
    const auto player_class =
        active_player != nullptr ? active_player->State().GetClass() : std::uint8_t{0};
    if (const auto skill_line_name = ResolveTrainerServiceSkillLineName(
            *dbc, player_race, player_class, active_player, spell.spell_id)) {
      service->skill_line_name = std::move(*skill_line_name);
    }
  }

  void AccumulateCategory(const openwow::data::dbc::DbcLoader &dbc, const std::uint32_t category_id,
                          const TrainerSpell &spell, const TrainerServiceViewState &service) {
    auto *category = FindCategory(category_id);
    if (category == nullptr) {
      TrainerSkillCategoryView new_category;
      new_category.category_id = category_id;
      new_category.name = LookupSkillLineName(dbc, category_id);
      categories_.push_back(std::move(new_category));
      category = &categories_.back();
    }

    if (service.availability_bucket < category->count_by_state.size()) {
      ++category->count_by_state[service.availability_bucket];
    }
    if (spell.point_cost_0 != 0 || spell.point_cost_1 != 0) {
      category->has_point_costs = true;
    }
  }

  void FinalizeCategorizedServices(const std::int32_t trainer_type) {
    if (trainer_type == kTrainerTypeTradeskill) {
      std::stable_sort(
          categories_.begin(), categories_.end(),
          [](const TrainerSkillCategoryView &left, const TrainerSkillCategoryView &right) {
            if (left.category_id == right.category_id) {
              return false;
            }
            return left.category_id < right.category_id;
          });
    } else {
      std::stable_sort(
          categories_.begin(), categories_.end(),
          [](const TrainerSkillCategoryView &left, const TrainerSkillCategoryView &right) {
            if (left.category_id == right.category_id) {
              return false;
            }
            if (left.has_point_costs != right.has_point_costs) {
              return !left.has_point_costs;
            }
            if (left.name.empty() || right.name.empty()) {
              return false;
            }
            return openwow::core::SStrCmpNoCaseCollate(left.name.c_str(), right.name.c_str(),
                                                       0x7FFFFFFFu) < 0;
          });
    }

    const auto spell_count = services_.size();
    services_.reserve(spell_count + categories_.size());
    for (const auto &category : categories_) {
      TrainerServiceViewState header;
      header.category_id = category.category_id;
      header.is_header = true;
      header.name = category.name;
      header.spell_id = kInvalidSpellId;
      services_.push_back(std::move(header));
    }

    RebuildVisibleServices();
  }

  std::uint32_t ResolveTradeskillTrainerCategory(const openwow::data::dbc::DbcLoader &dbc,
                                                 const std::int32_t trainer_spell_id) const {
    if (trainer_spell_id <= 0) {
      return kTradeskillTrainerCategoryDefault;
    }

    const auto *spell = dbc.spell().LookupEntry(static_cast<std::uint32_t>(trainer_spell_id));
    if (spell == nullptr) {
      return kTradeskillTrainerCategoryDefault;
    }

    if ((spell->attributes & kTradeskillTrainerCategoryAttribute40Mask) != 0) {
      return kTradeskillTrainerCategoryAttribute40;
    }

    for (const auto effect : spell->effect) {
      if (effect == kSpellEffectSkillStep) {
        return kTradeskillTrainerCategorySkillStep;
      }
    }

    return kTradeskillTrainerCategoryDefault;
  }

  void BuildClassServices(const TrainerList &trainer, const openwow::data::dbc::DbcLoader &dbc,
                          const CGPlayer_C &active_player,
                          const TrainerUnlearnSpellCache &unlearn_spell_cache) {
    const auto player_race = active_player.State().GetRace();
    const auto player_class = active_player.State().GetClass();

    for (std::size_t index = 0; index < trainer.spells.size(); ++index) {
      const auto &spell = trainer.spells[index];
      const auto category_id = ResolveClassTrainerCategory(
          dbc, player_race, player_class, active_player, spell.spell_id);
      if (category_id == 0) {
        continue;
      }

      TrainerServiceViewState service;
      service.raw_spell_index = index;
      service.category_id = category_id;
      PopulateServiceDisplay(&dbc, &active_player, spell, unlearn_spell_cache, &service);
      AccumulateCategory(dbc, category_id, spell, service);
      services_.push_back(std::move(service));
    }

    FinalizeCategorizedServices(trainer.trainer_type);
  }

  void BuildTradeskillServices(const TrainerList &trainer, const openwow::data::dbc::DbcLoader &dbc,
                               const CGPlayer_C *active_player,
                               const TrainerUnlearnSpellCache &unlearn_spell_cache) {
    for (std::size_t index = 0; index < trainer.spells.size(); ++index) {
      const auto &spell = trainer.spells[index];
      const auto category_id = ResolveTradeskillTrainerCategory(dbc, spell.spell_id);

      TrainerServiceViewState service;
      service.raw_spell_index = index;
      service.category_id = category_id;
      PopulateServiceDisplay(&dbc, active_player, spell, unlearn_spell_cache, &service);
      AccumulateCategory(dbc, category_id, spell, service);
      services_.push_back(std::move(service));
    }

    FinalizeCategorizedServices(trainer.trainer_type);
  }

  void BuildFlatServices(const TrainerList &trainer, const openwow::data::dbc::DbcLoader *dbc,
                         const CGPlayer_C *active_player,
                         const TrainerUnlearnSpellCache &unlearn_spell_cache) {
    for (std::size_t index = 0; index < trainer.spells.size(); ++index) {
      const auto &spell = trainer.spells[index];
      TrainerServiceViewState service;
      service.raw_spell_index = index;
      PopulateServiceDisplay(dbc, active_player, spell, unlearn_spell_cache, &service);
      services_.push_back(std::move(service));
    }

    RebuildVisibleServices();
  }

  TrainerSkillCategoryView *FindCategory(const std::uint32_t category_id) {
    for (auto &category : categories_) {
      if (category.category_id == category_id) {
        return &category;
      }
    }
    return nullptr;
  }

  const TrainerSkillCategoryView *FindCategory(const std::uint32_t category_id) const {
    for (const auto &category : categories_) {
      if (category.category_id == category_id) {
        return &category;
      }
    }
    return nullptr;
  }

  std::optional<std::uint32_t> FindCategoryBitIndex(const std::uint32_t category_id) const {
    for (std::uint32_t index = 0; index < categories_.size(); ++index) {
      if (categories_[index].category_id == category_id) {
        return index;
      }
    }
    return std::nullopt;
  }

  std::optional<std::uint32_t>
  ResolveSkillLineBitIndexForServiceRow(const std::int32_t one_based_service_index) const {
    if (one_based_service_index <= 0) {
      return std::nullopt;
    }

    const auto zero_based_index = static_cast<std::size_t>(one_based_service_index - 1);
    if (zero_based_index >= services_.size()) {
      return std::nullopt;
    }

    const auto &service = services_[zero_based_index];
    if (!service.is_header) {
      return std::nullopt;
    }

    return FindCategoryBitIndex(service.category_id);
  }

  std::uint32_t CategoryOrder(const std::uint32_t category_id) const {
    for (std::uint32_t index = 0; index < categories_.size(); ++index) {
      if (categories_[index].category_id == category_id) {
        return index;
      }
    }
    return 0;
  }

  void RebuildVisibleServices() {
    for (std::uint32_t index = 0; index < categories_.size(); ++index) {
      auto &category = categories_[index];
      category.passes_skill_line_filter =
          (skill_line_filter_mask_ & (std::uint32_t{1} << index)) != 0;

      bool passes_availability_filter = false;
      for (std::uint32_t bucket = 0; bucket < category.count_by_state.size(); ++bucket) {
        if ((availability_filter_mask_ & (std::uint32_t{1} << bucket)) != 0 &&
            category.count_by_state[bucket] != 0) {
          passes_availability_filter = true;
          break;
        }
      }

      category.passes_skill_line_filter =
          category.passes_skill_line_filter && passes_availability_filter;
      category.collapsed = (collapse_mask_ & (std::uint32_t{1} << index)) == 0;
    }

    for (auto &service : services_) {
      service.visible = true;
      if (!service.is_header &&
          (availability_filter_mask_ & (std::uint32_t{1} << service.availability_bucket)) == 0) {
        service.visible = false;
      }

      if (categories_.empty()) {
        continue;
      }

      const auto *category = FindCategory(service.category_id);
      if (category == nullptr || !category->passes_skill_line_filter) {
        service.visible = false;
        continue;
      }

      if (!service.is_header && category->collapsed) {
        service.visible = false;
      }
    }

    std::stable_sort(
        services_.begin(), services_.end(),
        [this](const TrainerServiceViewState &left, const TrainerServiceViewState &right) {
          const auto category_order = [this](const std::uint32_t category_id) {
            return CategoryOrder(category_id);
          };

          if (trainer_type_ == kTrainerTypeTradeskill) {
            return CompareTrainerServices(left, right, CompareTradeskillTrainerServices,
                                          category_order);
          }

          return CompareTrainerServices(left, right, CompareClassTrainerServices, category_order);
        });

    visible_service_count_ = 0;
    for (const auto &service : services_) {
      if (!service.visible) {
        break;
      }
      ++visible_service_count_;
    }

    selected_spell_id_ = 0;
    for (const auto &service : services_) {
      if (service.visible && !service.is_header && service.availability_bucket == 0 &&
          service.spell_id != kInvalidSpellId) {
        selected_spell_id_ = service.spell_id;
        return;
      }
    }

    if (services_.size() > 1 && services_[1].spell_id != kInvalidSpellId) {
      selected_spell_id_ = services_[1].spell_id;
    }
  }

  std::uint32_t ResolveClassTrainerCategory(const openwow::data::dbc::DbcLoader &dbc,
                                            const std::uint8_t player_race,
                                            const std::uint8_t player_class,
                                            const CGPlayer_C &active_player,
                                            const std::int32_t trainer_spell_id) const {
    if (trainer_spell_id <= 0) {
      return 0;
    }

    const auto *spell = dbc.spell().LookupEntry(static_cast<std::uint32_t>(trainer_spell_id));
    if (spell == nullptr) {
      return 0;
    }

    std::uint32_t candidate_spell_id = spell->id;
    for (std::size_t effect_index = 0; effect_index < spell->effect.size(); ++effect_index) {
      if (spell->effect[effect_index] == kSpellEffectLearnSpell &&
          spell->effect_implicit_target_a[effect_index] != 5 &&
          spell->effect_trigger_spell[effect_index] != 0) {
        candidate_spell_id = spell->effect_trigger_spell[effect_index];
        break;
      }
    }

    return ResolveSkillLineForSpell(dbc, player_race, player_class, &active_player,
                                    candidate_spell_id);
  }

  std::optional<std::string> ResolveTrainerServiceSkillLineName(
      const openwow::data::dbc::DbcLoader &dbc, const std::uint8_t player_race,
      const std::uint8_t player_class, const CGPlayer_C *const active_player,
      const std::int32_t trainer_spell_id) const {
    if (trainer_spell_id <= 0) {
      return std::nullopt;
    }

    const auto *spell = dbc.spell().LookupEntry(static_cast<std::uint32_t>(trainer_spell_id));
    if (spell == nullptr) {
      return std::nullopt;
    }

    for (std::size_t effect_index = 0; effect_index < spell->effect.size(); ++effect_index) {
      if (spell->effect[effect_index] == kSpellEffectSkillStep) {
        const auto skill_line_id =
            static_cast<std::uint32_t>(std::max(spell->effect_misc_value[effect_index], 0));
        if (skill_line_id == 0) {
          continue;
        }

        const auto name = LookupSkillLineName(dbc, skill_line_id);
        if (!name.empty()) {
          return name;
        }
      }
    }

    std::uint32_t candidate_spell_id = spell->id;
    for (std::size_t effect_index = 0; effect_index < spell->effect.size(); ++effect_index) {
      if (spell->effect[effect_index] == kSpellEffectLearnSpell &&
          spell->effect_implicit_target_a[effect_index] != 5 &&
          spell->effect_trigger_spell[effect_index] != 0) {
        candidate_spell_id = spell->effect_trigger_spell[effect_index];
        break;
      }
    }

    const auto skill_line_id =
        ResolveSkillLineForSpell(dbc, player_race, player_class, active_player,
                                 candidate_spell_id);
    if (skill_line_id == 0) {
      return std::nullopt;
    }

    const auto name = LookupSkillLineName(dbc, skill_line_id);
    if (!name.empty()) {
      return name;
    }

    return std::nullopt;
  }

  static void PopulateSpellDisplay(const openwow::data::dbc::DbcLoader &dbc,
                                   const std::int32_t spell_id, std::string *out_name,
                                   std::string *out_subtext) {
    if (spell_id <= 0) {
      out_name->clear();
      out_subtext->clear();
      return;
    }

    const auto *spell = dbc.spell().LookupEntry(static_cast<std::uint32_t>(spell_id));
    if (spell == nullptr) {
      out_name->clear();
      out_subtext->clear();
      return;
    }

    *out_name = std::string(spell->spell_name);
    *out_subtext = std::string(spell->rank);
  }

  std::uint32_t ResolveSkillLineForSpell(const openwow::data::dbc::DbcLoader &dbc,
                                         const std::uint8_t player_race,
                                         const std::uint8_t player_class,
                                         const CGPlayer_C *const active_player,
                                         const std::uint32_t spell_id) const {
    if (spell_id == 0 || player_race == 0 || player_class == 0) {
      return 0;
    }

    std::optional<SkillRaceClassIdentity> active_identity;
    if (active_player != nullptr) {
      active_identity = SkillRaceClassIdentity{active_player->State().GetRace(),
                                               active_player->State().GetClass()};
    }

    const auto *ability = FindSkillLineAbilityForRaceClassSpell(
        dbc.skill_line_ability().entries(), dbc.skill_race_class_info().entries(), player_race,
        player_class, spell_id, active_identity);
    if (ability == nullptr) {
      return 0;
    }

    const auto *skill_info = FindSkillRaceClassInfoBySkillId(
        dbc.skill_race_class_info().entries(), player_race, player_class, ability->skill_id);
    if (skill_info == nullptr || (skill_info->flags & kHiddenSkillLineFlag) != 0) {
      return 0;
    }

    return ability->skill_id;
  }

  static std::string LookupSkillLineName(const openwow::data::dbc::DbcLoader &dbc,
                                         const std::uint32_t skill_line_id) {
    const auto *skill_line = dbc.skill_line().LookupEntry(skill_line_id);
    if (skill_line == nullptr || skill_line->name.empty()) {
      return {};
    }
    return std::string(skill_line->name);
  }

  std::uint32_t signature_ = 0;
  const openwow::data::dbc::DbcLoader *dbc_ = nullptr;
  std::int32_t trainer_type_ = 0;
  std::uint32_t availability_filter_mask_ = kTrainerAvailabilityMaskAll;
  std::uint32_t skill_line_filter_mask_ = std::numeric_limits<std::uint32_t>::max();
  std::uint32_t collapse_mask_ = std::numeric_limits<std::uint32_t>::max();
  std::uint32_t selected_spell_id_ = 0;
  std::string greeting_text_;
  std::vector<TrainerSkillCategoryView> categories_;
  std::vector<TrainerServiceViewState> services_;
  std::uint32_t visible_service_count_ = 0;
};

TrainerFrameCache &GetTrainerFrameCache() {
  static TrainerFrameCache cache;
  return cache;
}

void ApplyTrainerServiceTypeFilterMask(const std::uint32_t filter_mask) {
  StoreTrainerServiceTypeFilterMask(filter_mask);
  GetTrainerFrameCache().SetServiceTypeFilterMask(filter_mask);
  openwow::ui::game::ScriptEventDispatch::Get().FireEvent(
      openwow::ui::game::events::TRAINER_UPDATE);
}

}

std::uint32_t Trainer_ParseFilterString(const char *filter_str) {
  if (filter_str == nullptr) {
    return 6;
  }
  if (openwow::core::SStrCmpNoCase(filter_str, "available", 0x7FFFFFFFu) == 0) {
    return 0;
  }
  if (openwow::core::SStrCmpNoCase(filter_str, "unavailable", 0x7FFFFFFFu) == 0) {
    return 1;
  }
  if (openwow::core::SStrCmpNoCase(filter_str, "used", 0x7FFFFFFFu) == 0) {
    return 2;
  }
  return 6;
}

void Trainer_ResetFrameState() {
  GetTrainerFrameCache().Reset();
}

void Trainer_UpdateGreetingText(const WorldSession &session) {
  const auto *trainer = session.gossip().has_trainer() ? &session.gossip().trainer() : nullptr;
  if (trainer == nullptr) {
    return;
  }

  std::array<char, 0x800> expanded{};
  BindSpellTextFormatterDbcLoader(session.GetDbcLoader());
  BindSpellTextFormatterWorldSession(&session);
  SpellTextFormatter::ExpandObjectTextVariables(
      trainer->greeting.c_str(), expanded.data(), static_cast<std::uint32_t>(expanded.size()),
      session.objects().GetActivePlayerGuid().GetRawValue(), nullptr, 0);
  GetTrainerFrameCache().SetGreetingText(expanded.data());
}

const std::string &Trainer_GetGreetingText() {
  return GetTrainerFrameCache().greeting_text();
}

std::uint32_t Trainer_GetVisibleServiceCount(const WorldSession &session,
                                             const openwow::data::dbc::DbcLoader *dbc) {
  auto &cache = GetTrainerFrameCache();
  cache.Sync(session, dbc);
  return cache.visible_service_count();
}

bool Trainer_GetServiceInfo(const WorldSession &session, const openwow::data::dbc::DbcLoader *dbc,
                            const std::uint32_t one_based_index,
                            TrainerServiceInfoView *out_view) {
  auto &cache = GetTrainerFrameCache();
  cache.Sync(session, dbc);

  const auto *service = cache.GetService(one_based_index);
  if (service == nullptr || out_view == nullptr) {
    return false;
  }

  out_view->name = service->name;
  out_view->subtext = service->subtext;
  out_view->availability = service->is_header                  ? std::string_view{"header"}
                           : service->availability_bucket == 0 ? std::string_view{"available"}
                           : service->availability_bucket == 2 ? std::string_view{"used"}
                                                               : std::string_view{"unavailable"};
  out_view->is_header = service->is_header;
  out_view->show_numeric_flag =
      !service->is_header || cache.IsSkillLineExpanded(service->category_id);
  return true;
}

std::optional<std::size_t>
Trainer_ResolveVisibleSpellIndex(const WorldSession &session,
                                 const openwow::data::dbc::DbcLoader *dbc,
                                 const std::uint32_t one_based_index) {
  auto &cache = GetTrainerFrameCache();
  cache.Sync(session, dbc);

  const auto *service = cache.GetVisibleService(one_based_index);
  if (service == nullptr || !service->raw_spell_index.has_value()) {
    return std::nullopt;
  }
  return service->raw_spell_index;
}

bool Trainer_IsVisibleServiceAvailable(
    const WorldSession &session, const openwow::data::dbc::DbcLoader *dbc,
    const std::uint32_t one_based_index) {
  auto &cache = GetTrainerFrameCache();
  cache.Sync(session, dbc);

  const auto *service = cache.GetVisibleService(one_based_index);
  return service != nullptr && !service->is_header &&
         service->raw_spell_index.has_value() &&
         service->availability_bucket == 0;
}

std::optional<std::uint32_t>
Trainer_ResolveVisibleServiceCreatedItemId(const WorldSession &session,
                                           const openwow::data::dbc::DbcLoader *dbc,
                                           const std::uint32_t one_based_index) {
  const auto *trainer_spell = ResolveVisibleTrainerSpell(session, dbc, one_based_index);
  if (trainer_spell == nullptr) {
    return std::nullopt;
  }

  const auto *spell = LookupTrainerSpellEntry(dbc, trainer_spell->spell_id);
  if (spell == nullptr || (spell->attributes & 0x20u) == 0) {
    return std::nullopt;
  }

  return ResolveTrainerCreatedItemId(*spell);
}

std::optional<TrainerServiceTooltipTarget>
Trainer_ResolveVisibleServiceTooltipTarget(const WorldSession &session,
                                           const openwow::data::dbc::DbcLoader *dbc,
                                           const std::uint32_t one_based_index) {
  const auto *trainer_spell = ResolveVisibleTrainerSpell(session, dbc, one_based_index);
  if (trainer_spell == nullptr) {
    return std::nullopt;
  }

  const auto *spell = LookupTrainerSpellEntry(dbc, trainer_spell->spell_id);
  if (spell == nullptr) {
    return std::nullopt;
  }

  if (const auto *tooltip_spell = ResolveTrainerTooltipSpell(*dbc, *spell);
      tooltip_spell != nullptr) {
    if ((tooltip_spell->attributes & 0x20u) != 0) {
      if (const auto item_id = ResolveTrainerCreatedItemId(*tooltip_spell); item_id.has_value()) {
        return TrainerServiceTooltipTarget{TrainerServiceTooltipTargetKind::Item, *item_id};
      }
    }

    return TrainerServiceTooltipTarget{TrainerServiceTooltipTargetKind::Spell, tooltip_spell->id};
  }

  return TrainerServiceTooltipTarget{TrainerServiceTooltipTargetKind::Spell, spell->id};
}

std::optional<std::string_view>
Trainer_GetVisibleServiceSkillLineName(const WorldSession &session,
                                       const openwow::data::dbc::DbcLoader *dbc,
                                       const std::uint32_t one_based_index) {
  auto &cache = GetTrainerFrameCache();
  cache.Sync(session, dbc);

  const auto *service = cache.GetVisibleService(one_based_index);
  if (service == nullptr || service->is_header || service->skill_line_name.empty()) {
    return std::nullopt;
  }
  return service->skill_line_name;
}

std::optional<TrainerSkillStepEffectView>
Trainer_ResolveSkillStepEffect(const openwow::data::dbc::DbcLoader &dbc,
                               const std::int32_t step_spell_id,
                               const std::uint32_t player_level) {
  const auto *spell = LookupTrainerSpellEntry(&dbc, step_spell_id);
  if (spell == nullptr) {
    return std::nullopt;
  }

  for (std::size_t effect_index = 0; effect_index < spell->effect.size(); ++effect_index) {
    if (spell->effect[effect_index] != kSpellEffectSkillStep ||
        spell->effect_misc_value[effect_index] <= 0) {
      continue;
    }

    const auto skill_line_id = static_cast<std::uint32_t>(spell->effect_misc_value[effect_index]);
    const auto *skill_line = dbc.skill_line().LookupEntry(skill_line_id);
    if (skill_line == nullptr || skill_line->name.empty()) {
      return std::nullopt;
    }

    return TrainerSkillStepEffectView{
        .skill_name = skill_line->name,
        .skill_line_id = static_cast<std::uint16_t>(skill_line_id),
        .required_skill_rank =
            static_cast<std::uint32_t>(
                ComputeTrainerSpellEffectMaxValue(*spell, effect_index, player_level)) *
            5u,
    };
  }

  return std::nullopt;
}

bool Trainer_HasSkillStepEffect(const openwow::data::dbc::DbcLoader &dbc,
                                const std::int32_t spell_id) {
  const auto *spell = LookupTrainerSpellEntry(&dbc, spell_id);
  if (spell == nullptr) {
    return false;
  }

  for (std::size_t effect_index = 0; effect_index < spell->effect.size() && effect_index < 3;
       ++effect_index) {
    if (spell->effect[effect_index] == kSpellEffectSkillStep) {
      return true;
    }
  }

  return false;
}

std::optional<TrainerSkillStepIncreaseView>
Trainer_ResolveServiceStepIncrease(const openwow::data::dbc::DbcLoader &dbc,
                                   const TrainerSpell &trainer_spell,
                                   const std::uint32_t player_level) {
  if (trainer_spell.state == TrainerSpellState::Known) {
    return std::nullopt;
  }

  const auto current_step =
      Trainer_ResolveSkillStepEffect(dbc, trainer_spell.spell_id, player_level);
  if (!current_step.has_value()) {
    return std::nullopt;
  }

  if (current_step->required_skill_rank == 0) {
    return std::nullopt;
  }

  return TrainerSkillStepIncreaseView{
      .skill_name = current_step->skill_name,
      .rank_increase = current_step->required_skill_rank,
  };
}

std::uint32_t Trainer_GetSkillLineCount(const WorldSession &session,
                                        const openwow::data::dbc::DbcLoader *dbc) {
  auto &cache = GetTrainerFrameCache();
  cache.Sync(session, dbc);
  return cache.skill_line_count();
}

std::optional<std::string_view> Trainer_GetSkillLineName(const WorldSession &session,
                                                         const openwow::data::dbc::DbcLoader *dbc,
                                                         const std::uint32_t one_based_index) {
  auto &cache = GetTrainerFrameCache();
  cache.Sync(session, dbc);

  const auto *category = cache.GetSkillLine(one_based_index);
  if (category == nullptr) {
    return std::nullopt;
  }
  return category->name;
}

bool Trainer_GetSkillLineFilterEnabled(const WorldSession &session,
                                       const openwow::data::dbc::DbcLoader *dbc,
                                       const std::int32_t one_based_index) {
  auto &cache = GetTrainerFrameCache();
  cache.Sync(session, dbc);
  return cache.GetSkillLineFilterEnabled(one_based_index);
}

bool Trainer_SetSkillLineFilter(const WorldSession &session,
                                const openwow::data::dbc::DbcLoader *dbc,
                                const std::int32_t one_based_index, const bool enabled,
                                const bool exclusive) {
  auto &cache = GetTrainerFrameCache();
  cache.Sync(session, dbc);
  const bool updated = cache.SetSkillLineFilter(one_based_index, enabled, exclusive);
  if (updated) {
    openwow::ui::game::ScriptEventDispatch::Get().FireEvent(
        openwow::ui::game::events::TRAINER_UPDATE);
  }
  return updated;
}

bool Trainer_CollapseSkillLine(const WorldSession &session,
                               const openwow::data::dbc::DbcLoader *dbc,
                               const std::int32_t one_based_service_index) {
  GetTrainerFrameCache().Sync(session, dbc);
  return Trainer_CollapseSkillLine(one_based_service_index);
}

bool Trainer_CollapseSkillLine(const std::int32_t one_based_service_index) {
  auto &cache = GetTrainerFrameCache();
  const bool updated = cache.CollapseSkillLine(one_based_service_index);
  if (updated) {
    openwow::ui::game::ScriptEventDispatch::Get().FireEvent(
        openwow::ui::game::events::TRAINER_UPDATE);
  }
  return updated;
}

bool Trainer_ExpandSkillLine(const WorldSession &session, const openwow::data::dbc::DbcLoader *dbc,
                             const std::int32_t one_based_service_index) {
  auto &cache = GetTrainerFrameCache();
  cache.Sync(session, dbc);
  const bool updated = cache.ExpandSkillLine(one_based_service_index);
  if (updated) {
    openwow::ui::game::ScriptEventDispatch::Get().FireEvent(
        openwow::ui::game::events::TRAINER_UPDATE);
  }
  return updated;
}

std::optional<std::uint32_t> Trainer_GetSelectionIndex(const WorldSession &session,
                                                       const openwow::data::dbc::DbcLoader *dbc) {
  auto &cache = GetTrainerFrameCache();
  cache.Sync(session, dbc);
  return cache.GetSelectionIndex();
}

void Trainer_SelectVisibleService(const WorldSession &session,
                                  const openwow::data::dbc::DbcLoader *dbc,
                                  const std::int32_t one_based_index) {
  auto &cache = GetTrainerFrameCache();
  cache.Sync(session, dbc);
  cache.SelectVisibleService(one_based_index);
}

bool Trainer_GetServiceTypeFilterEnabled(const std::uint32_t filter_bucket) {
  return GetTrainerFrameCache().GetServiceTypeFilterEnabled(filter_bucket);
}

void Trainer_SetServiceTypeFilter(const std::uint32_t filter_bucket, const bool enabled,
                                  const bool exclusive) {
  if (filter_bucket >= 3) {
    return;
  }

  std::uint32_t filter_mask = GetTrainerServiceTypeFilterMask();
  const auto filter_bit = std::uint32_t{1} << filter_bucket;
  if (!enabled) {
    filter_mask &= ~filter_bit;
  } else if (exclusive) {
    filter_mask = filter_bit;
  } else {
    filter_mask |= filter_bit;
  }

  ApplyTrainerServiceTypeFilterMask(filter_mask);
}

void Trainer_ResetServiceTypeFilters() {
  ApplyTrainerServiceTypeFilterMask(kTrainerAvailabilityMaskAll);
}

}
