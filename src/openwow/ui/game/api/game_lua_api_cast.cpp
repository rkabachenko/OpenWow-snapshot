#include "openwow/ui/game/api/game_lua_api_cast.h"

#include "openwow/game/actions/held_cursor/held_cursor.h"
#include "openwow/game/inventory/adapters/ui/item_target_cursor_presenter.h"
#include "openwow/game/localization.h"
#include "openwow/game/spell_cast_execution.h"
#include "openwow/game/spell_action.h"
#include "openwow/game/spell_cost_and_range.h"
#include "openwow/game/spell_target_validation.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"

#include <utility>
#include <vector>

namespace openwow::ui::lua {

template <ConversionPolicy Policy>
bool LuaConverter<openwow::ui::game::detail::CastLuaString, Policy>::Valid(
    lua_State*, int) noexcept {
  return true;
}

template <ConversionPolicy Policy>
auto LuaConverter<openwow::ui::game::detail::CastLuaString, Policy>::Read(
    lua_State* state, const int index) -> Storage {
  if (index > lua_gettop(state) || lua_isstring(state, index) == 0) {
    return {};
  }
  std::size_t size = 0;
  const char* value = lua_tolstring(state, index, &size);
  return {{std::string(value, size)}};
}

template <ConversionPolicy Policy>
auto LuaConverter<openwow::ui::game::detail::CastLuaString, Policy>::Argument(
    Storage value) noexcept -> Storage {
  return value;
}

template <ConversionPolicy Policy>
bool LuaConverter<openwow::ui::game::detail::CastLuaNumber, Policy>::Valid(
    lua_State*, int) noexcept {
  return true;
}

template <ConversionPolicy Policy>
auto LuaConverter<openwow::ui::game::detail::CastLuaNumber, Policy>::Read(
    lua_State* state, const int index) noexcept -> Storage {
  return index <= lua_gettop(state) && lua_isnumber(state, index) != 0
             ? Storage{{static_cast<double>(lua_tonumber(state, index))}}
             : Storage{};
}

template <ConversionPolicy Policy>
auto LuaConverter<openwow::ui::game::detail::CastLuaNumber, Policy>::Argument(
    Storage value) noexcept -> Storage {
  return value;
}

template <ConversionPolicy Policy>
bool LuaConverter<openwow::ui::game::detail::CastLuaBoolean, Policy>::Valid(
    lua_State*, int) noexcept {
  return true;
}

template <ConversionPolicy Policy>
auto LuaConverter<openwow::ui::game::detail::CastLuaBoolean, Policy>::Read(
    lua_State* state, const int index) noexcept -> Storage {
  return {openwow::ui::game::detail::ReadClientBoolArgOrDefault(state, index,
                                                                 false)};
}

template <ConversionPolicy Policy>
auto LuaConverter<openwow::ui::game::detail::CastLuaBoolean, Policy>::Argument(
    Storage value) noexcept -> Storage {
  return value;
}

template <ConversionPolicy Policy>
bool LuaConverter<openwow::ui::game::detail::CastLuaValue, Policy>::Valid(
    lua_State*, int) noexcept {
  return true;
}

template <ConversionPolicy Policy>
auto LuaConverter<openwow::ui::game::detail::CastLuaValue, Policy>::Read(
    lua_State* state, const int index) -> Storage {
  Storage result;
  if (index <= lua_gettop(state) && lua_isnumber(state, index) != 0) {
    result.number = static_cast<double>(lua_tonumber(state, index));
  }
  if (index <= lua_gettop(state) && lua_isstring(state, index) != 0) {
    std::size_t size = 0;
    const char* value = lua_tolstring(state, index, &size);
    result.string = std::string(value, size);
  }
  return result;
}

template <ConversionPolicy Policy>
auto LuaConverter<openwow::ui::game::detail::CastLuaValue, Policy>::Argument(
    Storage value) noexcept -> Storage {
  return value;
}

template struct LuaConverter<openwow::ui::game::detail::CastLuaString,
                             openwow::ui::game::detail::kCastLuaCoercion>;
template struct LuaConverter<openwow::ui::game::detail::CastLuaNumber,
                             openwow::ui::game::detail::kCastLuaCoercion>;
template struct LuaConverter<openwow::ui::game::detail::CastLuaBoolean,
                             openwow::ui::game::detail::kCastLuaCoercion>;
template struct LuaConverter<openwow::ui::game::detail::CastLuaValue,
                             openwow::ui::game::detail::kCastLuaCoercion>;

}

namespace openwow::ui::game::detail {
namespace {

using ::openwow::game::SpellbookSystem;
using ::openwow::game::SpellTargeting;
using ::openwow::game::WorldSession;
using ::openwow::ui::lua::LuaTruthy;
using ::openwow::ui::lua::LuaUsageError;
using ::openwow::ui::lua::NoLuaResults;

struct CastTarget {
  std::uint64_t guid = 0;
};

struct SpellDisplayData {
  std::string name;
  std::string rank;
  std::string texture;
  bool is_trade_skill = false;
  bool use_spell_name_for_player_text = false;
};

struct UnitCastInfo {
  std::string name;
  std::string sub_text;
  std::string text;
  std::string texture;
  double start_time_ms = 0.0;
  double end_time_ms = 0.0;
  bool is_trade_skill = false;
  std::optional<std::uint32_t> cast_id;
  bool not_interruptible = false;
};

struct CurrentSpellQuery {
  std::uint32_t spell_id = 0;
  bool from_pet_book = false;
};

constexpr std::uint32_t kTradeSkillSpellAttribute = 0x20u;
constexpr std::uint32_t kPlayerChannelTextUsesSpellNameAttributeEx3 =
    0x20000000u;
constexpr std::uint32_t kShootWandSpellId = 5019;

void BindRegistryContext(lua_State& state, const std::string_view key,
                         void* value) {
  if (value != nullptr) {
    lua_pushlightuserdata(&state, value);
  } else {
    lua_pushnil(&state);
  }
  lua_setfield(&state, LUA_REGISTRYINDEX, key.data());
}

SpellbookSystem& ResolveSpellbook(SpellbookSystem* spellbook) {
  return spellbook != nullptr ? *spellbook : SpellbookSystem::Get();
}

SpellTargeting* ResolveTargeting(WorldSession* session,
                                 SpellTargeting* targeting) {
  return targeting != nullptr
             ? targeting
             : session != nullptr ? &session->spells().GetTargeting() : nullptr;
}

double CurrentTimeMs() {

  return static_cast<double>(core::GameClock::GetTickCount32());
}

const openwow::data::dbc::SpellEntry* LookupSpellEntry(
    const WorldSession* session, const std::uint32_t spell_id) {
  const auto* dbc = session != nullptr ? session->GetDbcLoader() : nullptr;
  return dbc != nullptr && spell_id != 0 ? dbc->spell().LookupEntry(spell_id)
                                         : nullptr;
}

std::optional<SpellDisplayData> ResolveSpellDisplayData(
    const WorldSession& session, const std::uint32_t spell_id) {
  const auto* spell = LookupSpellEntry(&session, spell_id);
  if (spell == nullptr) {
    return std::nullopt;
  }
  SpellDisplayData result{
      .name = std::string(spell->spell_name),
      .rank = std::string(spell->rank),
      .is_trade_skill = (spell->attributes & kTradeSkillSpellAttribute) != 0u,
      .use_spell_name_for_player_text =
          (spell->attributes_ex3 &
           kPlayerChannelTextUsesSpellNameAttributeEx3) != 0u,
  };
  const auto* dbc = session.GetDbcLoader();
  if (dbc != nullptr && spell->spell_icon_id != 0) {
    const auto* icon = dbc->spell_icon().LookupEntry(spell->spell_icon_id);
    if (icon != nullptr && !std::string_view(icon->icon_path).empty()) {
      result.texture = std::string(icon->icon_path);
    }
  }
  return result;
}

std::string ResolveChannelText(const std::string_view unit_id,
                               const std::string_view spell_name,
                               const bool use_spell_name_for_player_text) {
  if (spell_name.empty()) {
    return {};
  }
  if (!EqualsIgnoreCaseAscii(unit_id, "player") ||
      use_spell_name_for_player_text) {
    return std::string(spell_name);
  }
  return ::openwow::game::Localization::Get().GetString(
      "CHANNELING", std::string(spell_name));
}

std::optional<UnitCastInfo> ResolveUnitCastInfo(
    const WorldSession& session, const ::openwow::game::CastInfo& cast,
    const std::string_view unit_id, const bool channel) {
  if (cast.spell_id == 0 || cast.is_channel != channel ||
      static_cast<double>(cast.end_time) <= CurrentTimeMs()) {
    return std::nullopt;
  }
  const auto display = ResolveSpellDisplayData(session, cast.spell_id);
  if (!display) {
    return std::nullopt;
  }
  UnitCastInfo result;
  result.name = display->name;
  result.sub_text = display->rank;
  result.text = channel
                    ? ResolveChannelText(
                          unit_id, result.name,
                          display->use_spell_name_for_player_text)
                    : !cast.spell_text.empty() ? cast.spell_text : result.name;
  result.texture = display->texture;
  result.start_time_ms = static_cast<double>(cast.start_time);
  result.end_time_ms = static_cast<double>(cast.end_time);
  result.is_trade_skill = display->is_trade_skill;
  if (!channel) {
    result.cast_id = cast.cast_id;
  }
  result.not_interruptible = cast.not_interruptible;
  return result;
}

const ::openwow::game::CGUnit_C* ResolveCastUnit(
    WorldSession& session, const std::string_view unit_id) {
  return ResolveUnitObject(ResolveUnit(&session, std::string(unit_id)));
}

CastInfoReturns EmitUnitCastInfo(const UnitCastInfo& info) {
  std::vector<CastValue> values;
  values.reserve(info.cast_id ? 9 : 8);
  values.emplace_back(info.name);
  values.emplace_back(info.sub_text);
  values.emplace_back(info.text);
  values.emplace_back(info.texture);
  values.emplace_back(info.start_time_ms);
  values.emplace_back(info.end_time_ms);
  values.emplace_back(info.is_trade_skill);
  if (info.cast_id) {
    values.emplace_back(static_cast<double>(*info.cast_id));
  }
  values.emplace_back(info.not_interruptible);
  return CastInfoReturns(std::move(values));
}

std::optional<CastTarget> ResolveCastTarget(WorldSession& session,
                                            const CastLuaString target) {
  if (!target.value || target.value->empty()) {
    return CastTarget{session.objects().GetTargetGuid().GetRawValue()};
  }
  if (::openwow::game::ParseUnitId(*target.value).kind ==
      ::openwow::game::UnitIdKind::kUnknown) {
    return std::nullopt;
  }
  return CastTarget{ResolveUnitId(&session, *target.value).GetRawValue()};
}

std::uint32_t ResolveSpellBookSpellId(WorldSession* session,
                                      SpellbookSystem& spellbook,
                                      const std::uint32_t slot,
                                      const std::string_view book) {
  if (openwow::text::EqualsIgnoreCaseAscii(book, "pet")) {
    return session != nullptr ? session->pet().GetSpellbookSpellId(slot) : 0;
  }
  const auto* spell = spellbook.GetPlayerSpellBookSlot(slot);
  return spell != nullptr ? spell->spell_id : 0;
}

std::optional<std::uint32_t> FindSpellBookSlot(
    WorldSession* session, SpellbookSystem& spellbook,
    const std::uint32_t spell_id, const bool from_pet_book) {
  if (spell_id == 0) {
    return std::nullopt;
  }
  if (from_pet_book) {
    if (session == nullptr) {
      return std::nullopt;
    }
    const auto& spells = session->pet().pet_bar().spellbook_spells;
    for (std::size_t slot = spells.size(); slot > 0; --slot) {
      if (spells[slot - 1] == spell_id) {
        return static_cast<std::uint32_t>(slot);
      }
    }
    return std::nullopt;
  }
  for (std::size_t slot = spellbook.GetPlayerSpellBookSlotCount(); slot > 0;
       --slot) {
    const auto* spell =
        spellbook.GetPlayerSpellBookSlot(static_cast<std::uint32_t>(slot));
    if (spell != nullptr && spell->spell_id == spell_id) {
      return static_cast<std::uint32_t>(slot);
    }
  }
  return std::nullopt;
}

std::optional<CurrentSpellQuery> ResolveSpellName(std::string name) {
  if (!name.empty() && name.front() == '!') {
    name.erase(name.begin());
  }
  if (name.empty()) {
    return std::nullopt;
  }
  if (const auto match =
          ::openwow::game::SpellBookFrame::ResolveSpellByName(name, {})) {
    return CurrentSpellQuery{match->spell_id, match->from_pet_spellbook};
  }
  std::string qualifier;
  if (const auto open = name.find('('); open != std::string::npos) {
    qualifier = name.substr(open + 1);
    name.resize(open);
    if (const auto close = qualifier.find(')'); close != std::string::npos) {
      qualifier.resize(close);
    }
  }
  const auto match =
      ::openwow::game::SpellBookFrame::ResolveSpellByName(name, qualifier);
  return match ? std::optional<CurrentSpellQuery>(CurrentSpellQuery{
                     match->spell_id, match->from_pet_spellbook})
               : std::nullopt;
}

std::variant<std::optional<CurrentSpellQuery>, LuaUsageError>
ResolveCurrentSpellQuery(WorldSession* session, SpellbookSystem& spellbook,
                         const CastLuaValue spell,
                         const std::optional<CastLuaValue> book,
                         const std::string_view api_name) {
  if (spell.number) {
    const auto slot = static_cast<lua_Integer>(*spell.number);
    if (!book && api_name == "IsCurrentSpell") {
      return slot > 0 ? std::optional<CurrentSpellQuery>(CurrentSpellQuery{
                            static_cast<std::uint32_t>(slot), false})
                      : std::optional<CurrentSpellQuery>{};
    }
    const auto* selector = book && book->string ? &*book->string : nullptr;
    if (slot < 1 || slot > 1024 || selector == nullptr ||
        !IsSpellBookSelector(*selector)) {
      return LuaUsageError{std::string(api_name) +
                           "(): Invalid spell slot"};
    }
    return std::optional<CurrentSpellQuery>(CurrentSpellQuery{
        ResolveSpellBookSpellId(session, spellbook,
                                static_cast<std::uint32_t>(slot), *selector),
        openwow::text::EqualsIgnoreCaseAscii(*selector, "pet")});
  }
  const auto* name = spell.string ? &*spell.string : nullptr;
  if (name == nullptr) {
    return LuaUsageError{std::string(api_name) + "(): Invalid spell slot"};
  }
  std::optional<CurrentSpellQuery> resolved;
  if (book) {
    const auto* qualifier = book->string ? &*book->string : nullptr;
    if (qualifier != nullptr &&
        ::openwow::game::ParseUnitId(*qualifier).kind ==
            ::openwow::game::UnitIdKind::kUnknown) {
      const auto match = ::openwow::game::SpellBookFrame::ResolveSpellByName(
          *name, *qualifier);
      if (!match) {
        return std::optional<CurrentSpellQuery>{};
      }
      resolved = CurrentSpellQuery{match->spell_id,
                                   match->from_pet_spellbook};
    }
  }
  if (!resolved) {
    resolved = ResolveSpellName(*name);
  }
  if (!resolved) {
    return std::optional<CurrentSpellQuery>{};
  }
  return FindSpellBookSlot(session, spellbook, resolved->spell_id,
                           resolved->from_pet_book)
             ? std::optional<CurrentSpellQuery>(*resolved)
             : std::optional<CurrentSpellQuery>{};
}

CastTruthyResult CurrentSpellResult(
    const std::variant<std::optional<CurrentSpellQuery>, LuaUsageError>& query,
    const bool value) {
  if (const auto* error = std::get_if<LuaUsageError>(&query)) {
    return *error;
  }
  const auto& resolved = std::get<std::optional<CurrentSpellQuery>>(query);
  return CastTruthyReturns(resolved ? std::vector<LuaTruthy>{{value}}
                                    : std::vector<LuaTruthy>{});
}

void DispatchPlayerSpell(WorldSession& session, const std::uint32_t spell_id,
                         std::uint64_t target_guid) {
  if (spell_id == 0) {
    return;
  }
  if (::openwow::game::SpellHasAttackActionEffect(spell_id,
                                                  session.GetDbcLoader())) {
    if (target_guid == 0) {
      target_guid = session.objects().GetTargetGuid().GetRawValue();
    }
    if (target_guid == 0) {
      return;
    }
    if (auto* targeting = session.targeting_system(); targeting != nullptr) {
      targeting->StartAttack(target_guid, false, false, spell_id);
    } else {
      session.interaction().SendAttackSwing(target_guid);
    }
    return;
  }
  if (spell_id == kShootWandSpellId) {
    auto& casts = session.spells();
    if (::openwow::game::SpellAction_ValidateAndInitiateCast(
            session, spell_id, target_guid, -1, 0)) {
      casts.StartAutoRepeat(spell_id);
    }
    return;
  }
  (void)::openwow::game::SpellAction_ValidateAndInitiateCast(
      session, spell_id, target_guid, -1, 0);
}

void DispatchSpell(WorldSession& session, const CurrentSpellQuery query,
                   const CastTarget target) {
  if (query.from_pet_book) {
    (void)DispatchResolvedPetSpell(session, query.spell_id, target.guid);
  } else {
    DispatchPlayerSpell(session, query.spell_id, target.guid);
  }
}

}

void BindCastLuaContext(lua_State& state, WorldSession* session,
                        SpellTargeting* targeting,
                        SpellbookSystem* spellbook) {
  BindRegistryContext(
      state, openwow::ui::lua::LuaRegistryContext<WorldSession>::key, session);
  BindRegistryContext(
      state, openwow::ui::lua::LuaRegistryContext<SpellTargeting>::key,
      targeting);
  BindRegistryContext(
      state, openwow::ui::lua::LuaRegistryContext<SpellbookSystem>::key,
      spellbook);
}

CastInfoResult UnitCastingInfo(WorldSession* session, const CastLuaString unit) {
  if (!unit.value) {
    return LuaUsageError{"Usage: UnitCastingInfo(\"unit\")"};
  }
  if (session == nullptr) {
    return CastInfoReturns({});
  }
  const auto* object = ResolveCastUnit(*session, *unit.value);
  if (object == nullptr) {
    return CastInfoReturns({});
  }
  const auto info = ResolveUnitCastInfo(
      *session, object->Casts().GetCurrentCast(), *unit.value, false);
  return info ? EmitUnitCastInfo(*info) : CastInfoReturns({});
}

CastInfoResult UnitChannelInfo(WorldSession* session, const CastLuaString unit) {
  if (!unit.value) {
    return LuaUsageError{"Usage: UnitChannelInfo(\"unit\")"};
  }
  if (session == nullptr) {
    return CastInfoReturns({});
  }
  const auto* object = ResolveCastUnit(*session, *unit.value);
  if (object == nullptr) {
    return CastInfoReturns({});
  }
  const auto info = ResolveUnitCastInfo(
      *session, object->Casts().GetChannelCast(), *unit.value, true);
  return info ? EmitUnitCastInfo(*info) : CastInfoReturns({});
}

CastTruthyResult SpellCanTargetUnit(WorldSession* session,
                                    SpellTargeting* targeting,
                                    const CastLuaString unit) {
  if (!unit.value) {
    return LuaUsageError{"Usage: SpellCanTargetUnit(\"unit\")"};
  }
  if (session == nullptr) {
    return CastTruthyReturns({{false}});
  }
  const auto* object = ResolveCastUnit(*session, *unit.value);
  targeting = ResolveTargeting(session, targeting);
  const auto* caster = session->objects().GetActivePlayer();
  const auto* dbc = session->GetDbcLoader();
  if (object == nullptr || targeting == nullptr || dbc == nullptr ||
      targeting->GetSpellId() == 0 || caster == nullptr) {
    return CastTruthyReturns({{false}});
  }
  const auto result = ::openwow::game::SpellTargetValidator::ResolveUnitTarget(
      *session, *dbc, targeting->GetSpellId(), *caster, *object,
      targeting->GetTargetMask(), true);
  return CastTruthyReturns({{result.IsValid()}});
}

CastVoidResult SpellTargetUnit(WorldSession* session, SpellTargeting* targeting,
                               const CastLuaString unit) {
  if (!unit.value) {
    return LuaUsageError{"Usage: SpellTargetUnit(\"unit\")"};
  }
  if (!GameUI_CanPerformProtectedAction(protected_action_kind::kSpellCast) || session == nullptr) {
    return NoLuaResults{};
  }
  targeting = ResolveTargeting(session, targeting);
  if (targeting == nullptr || targeting->GetSpellId() == 0) {
    return NoLuaResults{};
  }
  const auto* object = ResolveUnit(session, *unit.value);
  const auto* dbc = session->GetDbcLoader();
  const auto* spell =
      dbc != nullptr ? dbc->spell().LookupEntry(targeting->GetSpellId()) : nullptr;
  if (object == nullptr || spell == nullptr) {
    return NoLuaResults{};
  }
  if (!object->IsUnit()) {
    ::openwow::game::HandleCastFailure(
        *session, reinterpret_cast<std::uintptr_t>(spell),
        reinterpret_cast<std::uintptr_t>(spell),
        static_cast<std::uint8_t>(
            ::openwow::game::SpellCastResult::kOutOfRange),
        -1, -1, false);
    return NoLuaResults{};
  }
  const auto* target = session->objects().GetUnit(object->GetGuid());
  if (target != nullptr) {
    const auto guid = target->GetGuid();
    (void)::openwow::game::TryAssignTarget(
        *session, reinterpret_cast<std::uintptr_t>(&guid),
        reinterpret_cast<std::uintptr_t>(spell));
  }
  return NoLuaResults{};
}

CastTruthyResult IsCurrentSpell(WorldSession* session,
                                SpellbookSystem* spellbook,
                                const CastLuaValue spell,
                                const std::optional<CastLuaValue> book) {
  auto query = ResolveCurrentSpellQuery(session, ResolveSpellbook(spellbook),
                                        spell, book, "IsCurrentSpell");
  const auto* resolved = std::get_if<std::optional<CurrentSpellQuery>>(&query);
  const bool active = resolved != nullptr && *resolved && session != nullptr &&
                      LookupSpellEntry(session, (**resolved).spell_id) != nullptr &&
                      ::openwow::game::IsCurrentSpellId(
                          *session, (**resolved).spell_id);
  return CurrentSpellResult(query, active);
}

CastTruthyResult IsAutoRepeatSpell(WorldSession* session,
                                   SpellbookSystem* spellbook,
                                   const CastLuaValue spell,
                                   const std::optional<CastLuaValue> book) {
  auto query = ResolveCurrentSpellQuery(session, ResolveSpellbook(spellbook),
                                        spell, book, "IsAutoRepeatSpell");
  const auto* resolved = std::get_if<std::optional<CurrentSpellQuery>>(&query);
  const bool active = resolved != nullptr && *resolved && session != nullptr &&
                      !(**resolved).from_pet_book &&
                      (session->spells().GetAutoRepeatSpellId() ==
                           (**resolved).spell_id ||
                       session->spells().GetAutoAttackSpellId() ==
                           (**resolved).spell_id);
  return CurrentSpellResult(query, active);
}

CastTruthyResult IsAttackSpell(WorldSession* session,
                               SpellbookSystem* spellbook,
                               const CastLuaValue spell,
                               const std::optional<CastLuaValue> book) {
  auto query = ResolveCurrentSpellQuery(session, ResolveSpellbook(spellbook),
                                        spell, book, "IsAttackSpell");
  const auto* resolved = std::get_if<std::optional<CurrentSpellQuery>>(&query);
  const auto* entry = resolved != nullptr && *resolved
                          ? LookupSpellEntry(session, (**resolved).spell_id)
                          : nullptr;
  return CurrentSpellResult(query, entry != nullptr && entry->effect[0] == 78);
}

CastVoidResult CastSpell(WorldSession* session, SpellbookSystem* spellbook,
                         const CastLuaNumber slot, const CastLuaString book,
                         const CastLuaBoolean on_self) {
  if (!slot.value || !book.value) {
    return LuaUsageError{"CastSpell(): Invalid spell slot"};
  }
  const auto ordinal = static_cast<lua_Integer>(*slot.value);
  if (ordinal < 1 || ordinal > 1024 || !IsSpellBookSelector(*book.value)) {
    return LuaUsageError{"CastSpell(): Invalid spell slot"};
  }
  const CurrentSpellQuery query{
      ResolveSpellBookSpellId(session, ResolveSpellbook(spellbook),
                              static_cast<std::uint32_t>(ordinal), *book.value),
      openwow::text::EqualsIgnoreCaseAscii(*book.value, "pet")};

  if (query.spell_id == 0u) {
    return NoLuaResults{};
  }

  auto* cursor = session != nullptr ? session->held_cursor() : nullptr;
  if (cursor == nullptr) {
    return NoLuaResults{};
  }
  if (cursor->kind() == ::openwow::game::actions::held_cursor::Kind::Spell) {
    if (GameUI_CanPerformProtectedAction(protected_action_kind::kActionSlotMutation)) {
      cursor->Clear();
    }
    return NoLuaResults{};
  }
  cursor->Clear();
  const auto target_guid = DefaultActionTargetGuid(session, on_self.value);
  if (query.from_pet_book) {
    (void)DispatchResolvedPetSpell(*session, query.spell_id, target_guid);
  } else {
    (void)::openwow::game::SpellAction_ValidateAndInitiateCast(
        *session, query.spell_id, target_guid, -1, 0);
  }
  return NoLuaResults{};
}

CastVoidResult CastSpellByName(WorldSession* session, const CastLuaString name,
                               const CastLuaString target) {
  if (!name.value) {
    return LuaUsageError{"Usage: CastSpellByName(name[, target])"};
  }
  if (session == nullptr || session->objects().GetLocalPlayer() == nullptr) {
    return NoLuaResults{};
  }
  const auto resolved = ResolveSpellName(*name.value);
  const auto cast_target = ResolveCastTarget(*session, target);
  if (resolved && cast_target) {
    DispatchSpell(*session, *resolved, *cast_target);
  }
  return NoLuaResults{};
}

CastVoidResult CastSpellByID(WorldSession* session,
                             const CastLuaNumber spell_id,
                             const CastLuaString target) {
  if (!spell_id.value) {
    return LuaUsageError{"Usage: CastSpellByID(spellID[, target])"};
  }
  if (session == nullptr || session->objects().GetLocalPlayer() == nullptr) {
    return NoLuaResults{};
  }
  const auto id = static_cast<std::uint32_t>(*spell_id.value);
  const auto cast_target = ResolveCastTarget(*session, target);
  if (id != 0 && cast_target) {
    (void)::openwow::game::SpellAction_ValidateAndInitiateCast(
        *session, id, cast_target->guid, -1, 0);
  }
  return NoLuaResults{};
}

LuaTruthy SpellStopCasting(WorldSession* session) {
  if (!GameUI_CanPerformProtectedAction(protected_action_kind::kSpellCast) || session == nullptr) {
    return {false};
  }
  auto& spells = session->spells();
  if (spells.IsCasting()) {
    spells.CancelSpell(*session, ::openwow::game::SpellSlotType::kCurrent);
  } else if (spells.IsChanneling()) {
    spells.CancelSpell(*session, ::openwow::game::SpellSlotType::kChannel);
  } else if (spells.GetAutoRepeatSpellId() != 0u) {
    spells.CancelSpell(*session, ::openwow::game::SpellSlotType::kAutoRepeat);
  } else {
    return {false};
  }
  return {true};
}

LuaTruthy SpellIsTargeting(WorldSession* session, SpellTargeting* targeting) {
  targeting = ResolveTargeting(session, targeting);
  return {targeting != nullptr && targeting->GetSpellId() != 0};
}

LuaTruthy SpellStopTargeting(WorldSession* session, SpellTargeting* targeting) {
  targeting = ResolveTargeting(session, targeting);
  if (!GameUI_CanPerformProtectedAction(protected_action_kind::kSpellCast) || targeting == nullptr ||
      targeting->GetSpellId() == 0) {
    return {false};
  }
  ::openwow::game::inventory::ui::ClearItemTargetCursor(*targeting);
  targeting->CancelTargeting();
  return {true};
}

LuaTruthy SpellCanTargetItem(WorldSession* session, SpellTargeting* targeting) {
  targeting = ResolveTargeting(session, targeting);
  const auto targets = ::openwow::game::SpellTargetFlag::kItem |
                       ::openwow::game::SpellTargetFlag::kGoItem;
  return {targeting != nullptr && targeting->GetSpellId() != 0 &&
          ::openwow::game::HasFlag(
              static_cast<::openwow::game::SpellTargetFlag>(
                  targeting->GetTargetMask()),
              targets)};
}

LuaTruthy SpellCanTargetGlyph(WorldSession* session, SpellTargeting* targeting) {
  targeting = ResolveTargeting(session, targeting);
  return {targeting != nullptr && targeting->GetSpellId() != 0 &&
          ::openwow::game::HasFlag(
              static_cast<::openwow::game::SpellTargetFlag>(
                  targeting->GetTargetMask()),
              ::openwow::game::SpellTargetFlag::kGlyphSlot)};
}

}
