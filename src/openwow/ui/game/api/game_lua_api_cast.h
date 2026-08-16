#pragma once

#include "openwow/ui/runtime/lua/lua_binding.h"

#include <optional>
#include <string>
#include <variant>

namespace openwow::game {
class SpellbookSystem;
class SpellTargeting;
class WorldSession;
}

namespace openwow::ui::game::detail {

struct CastLuaString final {
  std::optional<std::string> value;
};

struct CastLuaNumber final {
  std::optional<double> value;
};

struct CastLuaBoolean final {
  bool value{false};
};

struct CastLuaValue final {
  std::optional<double> number;
  std::optional<std::string> string;
};

using CastValue = std::variant<std::string, double, bool>;
using CastInfoReturns = openwow::ui::lua::LuaVariableReturns<CastValue>;
using CastTruthyReturns =
    openwow::ui::lua::LuaVariableReturns<openwow::ui::lua::LuaTruthy>;
using CastVoidResult = std::variant<openwow::ui::lua::NoLuaResults,
                                    openwow::ui::lua::LuaUsageError>;
using CastInfoResult =
    std::variant<CastInfoReturns, openwow::ui::lua::LuaUsageError>;
using CastTruthyResult =
    std::variant<CastTruthyReturns, openwow::ui::lua::LuaUsageError>;

void BindCastLuaContext(lua_State& state, openwow::game::WorldSession* session,
                        openwow::game::SpellTargeting* targeting,
                        openwow::game::SpellbookSystem* spellbook);

CastInfoResult UnitCastingInfo(openwow::game::WorldSession* session,
                               CastLuaString unit);
CastInfoResult UnitChannelInfo(openwow::game::WorldSession* session,
                               CastLuaString unit);
CastTruthyResult SpellCanTargetUnit(openwow::game::WorldSession* session,
                                    openwow::game::SpellTargeting* targeting,
                                    CastLuaString unit);
CastVoidResult SpellTargetUnit(openwow::game::WorldSession* session,
                               openwow::game::SpellTargeting* targeting,
                               CastLuaString unit);
CastTruthyResult IsCurrentSpell(openwow::game::WorldSession* session,
                                openwow::game::SpellbookSystem* spellbook,
                                CastLuaValue spell,
                                std::optional<CastLuaValue> book);
CastTruthyResult IsAutoRepeatSpell(openwow::game::WorldSession* session,
                                   openwow::game::SpellbookSystem* spellbook,
                                   CastLuaValue spell,
                                   std::optional<CastLuaValue> book);
CastTruthyResult IsAttackSpell(openwow::game::WorldSession* session,
                               openwow::game::SpellbookSystem* spellbook,
                               CastLuaValue spell,
                               std::optional<CastLuaValue> book);
CastVoidResult CastSpell(openwow::game::WorldSession* session,
                         openwow::game::SpellbookSystem* spellbook,
                         CastLuaNumber slot, CastLuaString book,
                         CastLuaBoolean on_self);
CastVoidResult CastSpellByName(openwow::game::WorldSession* session,
                               CastLuaString name, CastLuaString target);
CastVoidResult CastSpellByID(openwow::game::WorldSession* session,
                             CastLuaNumber spell_id, CastLuaString target);
openwow::ui::lua::LuaTruthy SpellStopCasting(
    openwow::game::WorldSession* session);
openwow::ui::lua::LuaTruthy SpellIsTargeting(
    openwow::game::WorldSession* session,
    openwow::game::SpellTargeting* targeting);
openwow::ui::lua::LuaTruthy SpellStopTargeting(
    openwow::game::WorldSession* session,
    openwow::game::SpellTargeting* targeting);
openwow::ui::lua::LuaTruthy SpellCanTargetItem(
    openwow::game::WorldSession* session,
    openwow::game::SpellTargeting* targeting);
openwow::ui::lua::LuaTruthy SpellCanTargetGlyph(
    openwow::game::WorldSession* session,
    openwow::game::SpellTargeting* targeting);

}

namespace openwow::ui::lua {

template <>
struct LuaRegistryContext<openwow::game::WorldSession> {
  static constexpr std::string_view key = "openwow.world_session";
};

template <>
struct LuaRegistryContext<openwow::game::SpellTargeting> {
  static constexpr std::string_view key = "openwow.cast_targeting";
};

template <>
struct LuaRegistryContext<openwow::game::SpellbookSystem> {
  static constexpr std::string_view key = "openwow.cast_spellbook";
};

template <ConversionPolicy Policy>
struct LuaConverter<openwow::ui::game::detail::CastLuaString, Policy> {
  using Storage = openwow::ui::game::detail::CastLuaString;
  static bool Valid(lua_State*, int) noexcept;
  static Storage Read(lua_State* state, int index);
  static Storage Argument(Storage value) noexcept;
};

template <ConversionPolicy Policy>
struct LuaConverter<openwow::ui::game::detail::CastLuaNumber, Policy> {
  using Storage = openwow::ui::game::detail::CastLuaNumber;
  static bool Valid(lua_State*, int) noexcept;
  static Storage Read(lua_State* state, int index) noexcept;
  static Storage Argument(Storage value) noexcept;
};

template <ConversionPolicy Policy>
struct LuaConverter<openwow::ui::game::detail::CastLuaBoolean, Policy> {
  using Storage = openwow::ui::game::detail::CastLuaBoolean;
  static bool Valid(lua_State*, int) noexcept;
  static Storage Read(lua_State* state, int index) noexcept;
  static Storage Argument(Storage value) noexcept;
};

template <ConversionPolicy Policy>
struct LuaConverter<openwow::ui::game::detail::CastLuaValue, Policy> {
  using Storage = openwow::ui::game::detail::CastLuaValue;
  static bool Valid(lua_State*, int) noexcept;
  static Storage Read(lua_State* state, int index);
  static Storage Argument(Storage value) noexcept;
};

}

namespace openwow::ui::lua::detail {

template <>
struct IsOptional<openwow::ui::game::detail::CastLuaString> : std::true_type {};
template <>
struct IsOptional<openwow::ui::game::detail::CastLuaNumber> : std::true_type {};
template <>
struct IsOptional<openwow::ui::game::detail::CastLuaBoolean> : std::true_type {};
template <>
struct IsOptional<openwow::ui::game::detail::CastLuaValue> : std::true_type {};

}

namespace openwow::ui::game::detail {

inline constexpr openwow::ui::lua::ConversionPolicy kCastLuaCoercion{
    openwow::ui::lua::IntegralConversion::kTruncate,
    true, false, true, true, true};

inline constexpr auto kUnitCastingInfo =
    openwow::ui::lua::bind<&UnitCastingInfo, kCastLuaCoercion>(
        "UnitCastingInfo");
inline constexpr auto kUnitChannelInfo =
    openwow::ui::lua::bind<&UnitChannelInfo, kCastLuaCoercion>(
        "UnitChannelInfo");
inline constexpr auto kSpellCanTargetUnit =
    openwow::ui::lua::bind<&SpellCanTargetUnit, kCastLuaCoercion>(
        "SpellCanTargetUnit");
inline constexpr auto kSpellTargetUnit =
    openwow::ui::lua::bind<&SpellTargetUnit, kCastLuaCoercion>(
        "SpellTargetUnit");
inline constexpr auto kIsCurrentSpell =
    openwow::ui::lua::bind<&IsCurrentSpell, kCastLuaCoercion>("IsCurrentSpell");
inline constexpr auto kIsAutoRepeatSpell =
    openwow::ui::lua::bind<&IsAutoRepeatSpell, kCastLuaCoercion>(
        "IsAutoRepeatSpell");
inline constexpr auto kIsAttackSpell =
    openwow::ui::lua::bind<&IsAttackSpell, kCastLuaCoercion>("IsAttackSpell");
inline constexpr auto kCastSpell =
    openwow::ui::lua::bind<&CastSpell, kCastLuaCoercion>("CastSpell");
inline constexpr auto kCastSpellByName =
    openwow::ui::lua::bind<&CastSpellByName, kCastLuaCoercion>(
        "CastSpellByName");
inline constexpr auto kCastSpellByID =
    openwow::ui::lua::bind<&CastSpellByID, kCastLuaCoercion>("CastSpellByID");
inline constexpr auto kSpellStopCasting =
    openwow::ui::lua::bind<&SpellStopCasting, kCastLuaCoercion>(
        "SpellStopCasting");
inline constexpr auto kSpellIsTargeting =
    openwow::ui::lua::bind<&SpellIsTargeting, kCastLuaCoercion>(
        "SpellIsTargeting");
inline constexpr auto kSpellStopTargeting =
    openwow::ui::lua::bind<&SpellStopTargeting, kCastLuaCoercion>(
        "SpellStopTargeting");
inline constexpr auto kSpellCanTargetItem =
    openwow::ui::lua::bind<&SpellCanTargetItem, kCastLuaCoercion>(
        "SpellCanTargetItem");
inline constexpr auto kSpellCanTargetGlyph =
    openwow::ui::lua::bind<&SpellCanTargetGlyph, kCastLuaCoercion>(
        "SpellCanTargetGlyph");

inline constexpr lua_CFunction LuaUnitCastingInfo = kUnitCastingInfo.handler;
inline constexpr lua_CFunction LuaUnitChannelInfo = kUnitChannelInfo.handler;
inline constexpr lua_CFunction LuaSpellCanTargetUnit = kSpellCanTargetUnit.handler;
inline constexpr lua_CFunction LuaSpellTargetUnit = kSpellTargetUnit.handler;
inline constexpr lua_CFunction LuaIsCurrentSpell = kIsCurrentSpell.handler;
inline constexpr lua_CFunction LuaIsAutoRepeatSpell = kIsAutoRepeatSpell.handler;
inline constexpr lua_CFunction LuaIsAttackSpell = kIsAttackSpell.handler;
inline constexpr lua_CFunction LuaCastSpell = kCastSpell.handler;
inline constexpr lua_CFunction LuaCastSpellByName = kCastSpellByName.handler;
inline constexpr lua_CFunction LuaCastSpellByID = kCastSpellByID.handler;
inline constexpr lua_CFunction LuaSpellStopCasting = kSpellStopCasting.handler;
inline constexpr lua_CFunction LuaSpellIsTargeting = kSpellIsTargeting.handler;
inline constexpr lua_CFunction LuaSpellStopTargeting = kSpellStopTargeting.handler;
inline constexpr lua_CFunction LuaSpellCanTargetItem = kSpellCanTargetItem.handler;
inline constexpr lua_CFunction LuaSpellCanTargetGlyph = kSpellCanTargetGlyph.handler;

}
