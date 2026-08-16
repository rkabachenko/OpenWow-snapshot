
#include "openwow/ui/game/slash_command_handler.h"

#include "openwow/game/chat_sender.h"
#include "openwow/game/chat_system.h"
#include "openwow/game/emote_manager.h"
#include "openwow/game/group_system.h"
#include "openwow/game/interaction_sender.h"
#include "openwow/game/actions/macros/application/macro_catalog.h"
#include "openwow/game/object_guid.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/slash_commands.h"
#include "openwow/game/unit_query_bridge.h"
#include "openwow/game/world_session.h"
#include "openwow/net/client_services.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/lua_base_overrides.h"
#include "openwow/ui/lua_call_helpers.h"
#include "openwow/foundation/diagnostics/logging.h"

extern "C" {
#include <lua.hpp>
}

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace openwow::ui::game {

using openwow::game::ChatSystem;
using openwow::game::SlashCommands;
using openwow::game::WorldSession;

namespace {

std::string TrimLeft(const std::string& s) {
  auto it = std::find_if(s.begin(), s.end(),
                         [](unsigned char c) { return !std::isspace(c); });
  return std::string(it, s.end());
}

std::string TrimRight(const std::string& s) {
  auto it = std::find_if(s.rbegin(), s.rend(),
                         [](unsigned char c) { return !std::isspace(c); });
  return std::string(s.begin(), it.base());
}

std::string Trim(const std::string& s) { return TrimLeft(TrimRight(s)); }

std::pair<std::string, std::string> SplitFirst(const std::string& s) {
  auto trimmed = TrimLeft(s);
  auto space = trimmed.find(' ');
  if (space == std::string::npos) return {trimmed, ""};
  return {trimmed.substr(0, space), TrimLeft(trimmed.substr(space + 1))};
}

std::vector<std::string> SplitCommaList(const std::string& args) {
  std::vector<std::string> values;
  std::size_t begin = 0;
  while (begin <= args.size()) {
    const std::size_t comma = args.find(',', begin);
    std::string value = Trim(args.substr(begin, comma == std::string::npos ? std::string::npos
                                                                           : comma - begin));
    if (!value.empty()) {
      values.push_back(std::move(value));
    }
    if (comma == std::string::npos) {
      break;
    }
    begin = comma + 1;
  }
  return values;
}

bool TryParseInt(const std::string& text, int* value) {
  if (value == nullptr) {
    return false;
  }
  char* end = nullptr;
  const long parsed = std::strtol(text.c_str(), &end, 10);
  if (end == text.c_str() || (end != nullptr && *end != '\0')) {
    return false;
  }
  *value = static_cast<int>(parsed);
  return true;
}

bool RunSlashLuaSource(lua_State* state, std::string_view source, const char* chunk_name) {
  if (state == nullptr || source.empty()) {
    return false;
  }

  const int stack_base = lua_gettop(state);
  if (openwow::ui::LoadClientLuaChunk(state, source, chunk_name) != 0) {
    openwow::ui::LogLuaCallError(state, chunk_name);
    lua_settop(state, stack_base);
    return false;
  }
  if (lua_pcall(state, 0, 0, 0) != 0) {
    openwow::ui::LogLuaCallError(state, chunk_name);
    lua_settop(state, stack_base);
    return false;
  }

  lua_settop(state, stack_base);
  return true;
}

bool AddChatMessage(lua_State* state, std::string_view message) {
  return openwow::ui::CallLuaGlobalMethodIfFunction(
      state, "DEFAULT_CHAT_FRAME", "AddMessage", message);
}

bool AddCurrentDateChatMessage(lua_State* state) {
  if (state == nullptr) {
    return false;
  }
  const int stack_base = lua_gettop(state);
  if (!openwow::ui::PushLuaGlobalCallResult(state, "date", 1)) {
    lua_settop(state, stack_base);
    return false;
  }
  const char* date_text = lua_tostring(state, -1);
  const bool called = AddChatMessage(state, date_text != nullptr ? std::string_view(date_text)
                                                                 : std::string_view{});
  lua_settop(state, stack_base);
  return called;
}

bool ClickLuaGlobal(lua_State* state, const std::string& frame_name) {
  return openwow::ui::CallLuaGlobalMethodIfFunction(state, frame_name.c_str(), "Click");
}

template <typename... Args>
bool CallLua(lua_State* state, const char* function_name, Args&&... args) {
  return openwow::ui::CallLuaGlobalIfFunction(state, function_name, std::forward<Args>(args)...);
}

openwow::game::ObjectGuid ResolveNameOrUnitId(WorldSession* session,
                                               const std::string& name) {
  if (!session) return {};
  return openwow::game::UnitQueryBridge::Get().ResolveToGuid(session, name);
}

std::string GetChannelNameByIndex(std::size_t index) {
  auto& cs = ChatSystem::Get();
  if (index == 0 || index > cs.GetChannelSlotCount()) return "";
  const auto* ch = cs.GetLuaChannelBySlot(index - 1);
  return ch ? ch->name : "";
}

void RegisterChannelShortcuts(SlashCommands& cmds, WorldSession* session) {
  for (int i = 1; i <= 20; ++i) {
    cmds.RegisterCommand(
        std::to_string(i),
        [session, i](const std::string& args) -> bool {
          if (args.empty() || !session) return true;
          std::string channel =
              GetChannelNameByIndex(static_cast<std::size_t>(i));
          if (!channel.empty())
            session->chat_sender().SendChannel(channel, args);
          else
            openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                               "SlashCmd: channel " + std::to_string(i) +
                                   " not joined");
          return true;
        });
  }
}

}

void InitializeSlashCommandHandlers(lua_State* L, WorldSession* session) {
  auto& cmds = SlashCommands::Get();
  cmds.Reset();

  cmds.RegisterCommand("say", [session](const std::string& args) -> bool {
    if (!args.empty() && session)
      session->chat_sender().SendSay(args);
    return true;
  });
  cmds.RegisterAlias("s", "say");

  cmds.RegisterCommand("yell", [session](const std::string& args) -> bool {
    if (!args.empty() && session)
      session->chat_sender().SendYell(args);
    return true;
  });
  cmds.RegisterAlias("y", "yell");
  cmds.RegisterAlias("shout", "yell");

  cmds.RegisterCommand("whisper", [session](const std::string& args) -> bool {
    if (args.empty() || !session) return true;
    auto [target, msg] = SplitFirst(args);
    if (!msg.empty())
      session->chat_sender().SendWhisper(target, msg);
    return true;
  });
  cmds.RegisterAlias("w", "whisper");
  cmds.RegisterAlias("tell", "whisper");

  cmds.RegisterCommand("party", [session](const std::string& args) -> bool {
    if (!args.empty() && session)
      session->chat_sender().SendParty(args);
    return true;
  });
  cmds.RegisterAlias("p", "party");

  cmds.RegisterCommand("raid", [session](const std::string& args) -> bool {
    if (!args.empty() && session)
      session->chat_sender().SendRaid(args);
    return true;
  });
  cmds.RegisterAlias("ra", "raid");

  cmds.RegisterCommand("instance", [session](const std::string& args) -> bool {
    if (!args.empty() && session)
      session->chat_sender().SendBattleground(args);
    return true;
  });
  cmds.RegisterAlias("i", "instance");
  cmds.RegisterAlias("bg", "instance");
  cmds.RegisterAlias("battleground", "instance");

  cmds.RegisterCommand("guild", [session](const std::string& args) -> bool {
    if (!args.empty() && session)
      session->chat_sender().SendGuild(args);
    return true;
  });
  cmds.RegisterAlias("g", "guild");

  cmds.RegisterCommand("officer", [session](const std::string& args) -> bool {
    if (!args.empty() && session)
      session->chat_sender().SendOfficer(args);
    return true;
  });
  cmds.RegisterAlias("o", "officer");

  cmds.RegisterCommand("emote", [session](const std::string& args) -> bool {
    if (!args.empty() && session)
      session->chat_sender().SendEmote(args);
    return true;
  });
  cmds.RegisterAlias("e", "emote");
  cmds.RegisterAlias("em", "emote");
  cmds.RegisterAlias("me", "emote");

  cmds.RegisterCommand("reply", [session](const std::string& args) -> bool {
    if (args.empty() || !session) return true;
    const auto& target = ChatSystem::Get().GetLastWhisperTarget();
    if (!target.empty())
      session->chat_sender().SendWhisper(target, args);
    else
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                         "SlashCmd: /reply — no whisper target");
    return true;
  });
  cmds.RegisterAlias("r", "reply");

  cmds.RegisterCommand("rw", [session](const std::string& args) -> bool {
    if (!args.empty() && session)
      session->chat_sender().SendRaidWarning(args);
    return true;
  });

  RegisterChannelShortcuts(cmds, session);

  cmds.RegisterCommand("afk", [session](const std::string& args) -> bool {
    if (session)
      session->chat_sender().SendAfk(args);
    return true;
  });

  cmds.RegisterCommand("dnd", [session](const std::string& args) -> bool {
    if (session)
      session->chat_sender().SendDnd(args);
    return true;
  });

  auto register_emote = [&cmds, L](const std::string& cmd,
                                    const std::string& token) {
    cmds.RegisterCommand(cmd, [L, token](const std::string& ) -> bool {
      CallLua(L, "DoEmote", token);
      return true;
    });
  };

  register_emote("dance", "DANCE");
  register_emote("cheer", "CHEER");
  register_emote("wave", "WAVE");
  register_emote("bow", "BOW");
  register_emote("cry", "CRY");
  register_emote("laugh", "LAUGH");
  register_emote("lol", "LAUGH");
  register_emote("rude", "RUDE");
  register_emote("chicken", "CHICKEN");
  register_emote("flirt", "FLIRT");
  register_emote("flex", "FLEX");
  register_emote("kiss", "KISS");
  register_emote("clap", "CLAP");
  register_emote("point", "POINT");
  register_emote("roar", "ROAR");
  register_emote("salute", "SALUTE");
  register_emote("shrug", "SHRUG");
  register_emote("shy", "SHY");
  register_emote("sleep", "SLEEP");

  register_emote("lay", "LAYDOWN");
  register_emote("laydown", "LAYDOWN");
  register_emote("lie", "LAYDOWN");
  register_emote("liedown", "LAYDOWN");
  register_emote("spit", "SPIT");
  register_emote("thank", "THANK");
  register_emote("thanks", "THANK");
  register_emote("welcome", "WELCOME");
  register_emote("kneel", "KNEEL");
  register_emote("beg", "BEG");
  register_emote("grovel", "BEG");
  register_emote("eat", "EAT");
  register_emote("drink", "DRINK");
  register_emote("train", "TRAIN");
  register_emote("charge", "CHARGE");
  register_emote("angry", "ANGRY");
  register_emote("cower", "COWER");
  register_emote("doom", "DOOM");
  register_emote("no", "NO");
  register_emote("nod", "NOD");
  register_emote("yes", "NOD");
  register_emote("tap", "TAP");
  register_emote("talk", "TALK");
  register_emote("apologize", "APOLOGIZE");
  register_emote("sorry", "APOLOGIZE");
  register_emote("applaud", "APPLAUD");
  register_emote("bonk", "BONK");
  register_emote("bored", "BORED");
  register_emote("bye", "BYE");
  register_emote("cackle", "CACKLE");
  register_emote("calm", "CALM");
  register_emote("cold", "COLD");
  register_emote("comfort", "COMFORT");
  register_emote("confused", "CONFUSED");
  register_emote("console", "COMFORT");
  register_emote("cough", "COUGH");
  register_emote("cringe", "CRINGE");
  register_emote("curious", "CURIOUS");
  register_emote("curtsey", "CURTSEY");
  register_emote("disappointed", "DISAPPOINTED");
  register_emote("doh", "DOH");
  register_emote("drool", "DROOL");
  register_emote("excited", "EXCITED");
  register_emote("eye", "EYE");
  register_emote("fart", "FART");
  register_emote("fidget", "FIDGET");
  register_emote("flap", "FLAP");
  register_emote("followme", "FOLLOWME");
  register_emote("gasp", "GASP");
  register_emote("gaze", "GAZE");
  register_emote("giggle", "GIGGLE");
  register_emote("glare", "GLARE");
  register_emote("gloat", "GLOAT");
  register_emote("golfclap", "GOLFCLAP");
  register_emote("greet", "GREET");
  register_emote("hello", "GREET");
  register_emote("hi", "GREET");
  register_emote("grin", "GRIN");
  register_emote("groan", "GROAN");
  register_emote("growl", "GROWL");
  register_emote("guffaw", "GUFFAW");
  register_emote("happy", "HAPPY");
  register_emote("helpme", "HELPME");
  register_emote("hug", "HUG");
  register_emote("hungry", "HUNGRY");
  register_emote("impatient", "IMPATIENT");
  register_emote("insult", "INSULT");
  register_emote("introduce", "INTRODUCE");
  register_emote("jk", "JK");
  register_emote("lick", "LICK");
  register_emote("listen", "LISTEN");
  register_emote("lost", "LOST");
  register_emote("map", "LOST");
  register_emote("massage", "MASSAGE");
  register_emote("moan", "MOAN");
  register_emote("mock", "MOCK");
  register_emote("mourn", "MOURN");
  register_emote("oom", "OOM");
  register_emote("openfire", "OPENFIRE");
  register_emote("panic", "PANIC");
  register_emote("pat", "PAT");
  register_emote("peek", "PEEK");
  register_emote("peon", "PEON");
  register_emote("plead", "PLEAD");
  register_emote("poke", "POKE");
  register_emote("pray", "PRAY");
  register_emote("puzzle", "PUZZLE");
  register_emote("rasp", "RASP");
  register_emote("ready", "READY");
  register_emote("scared", "SCARED");
  register_emote("scratch", "SCRATCH");
  register_emote("sexy", "SEXY");
  register_emote("shimmy", "SHIMMY");
  register_emote("shiver", "SHIVER");
  register_emote("shoo", "SHOO");
  register_emote("sigh", "SIGH");
  register_emote("slap", "SLAP");
  register_emote("smirk", "SMIRK");
  register_emote("snicker", "SNICKER");
  register_emote("sniff", "SNIFF");
  register_emote("sob", "CRY");
  register_emote("stare", "STARE");
  register_emote("stink", "STINK");
  register_emote("surprised", "SURPRISED");
  register_emote("surrender", "SURRENDER");
  register_emote("tease", "TEASE");
  register_emote("thirsty", "THIRSTY");
  register_emote("tickle", "TICKLE");
  register_emote("tired", "TIRED");
  register_emote("victory", "VICTORY");
  register_emote("wait", "WAIT");
  register_emote("wink", "WINK");
  register_emote("work", "WORK");
  register_emote("yawn", "YAWN");

  cmds.RegisterCommand("sit", [L](const std::string& ) -> bool {
    CallLua(L, "DoEmote", "SIT");
    return true;
  });

  cmds.RegisterCommand("stand", [L](const std::string& ) -> bool {
    CallLua(L, "DoEmote", "STAND");
    return true;
  });

  cmds.RegisterCommand("duel", [L](const std::string& args) -> bool {
    if (args.empty()) {
      CallLua(L, "CastSpellByName", "Duel");
    } else {
      CallLua(L, "TargetUnit", Trim(args));
      CallLua(L, "CastSpellByName", "Duel");
    }
    return true;
  });

  cmds.RegisterCommand("cast", [L](const std::string& args) -> bool {
    if (!args.empty()) {
      CallLua(L, "CastSpellByName", args);
    }
    return true;
  });
  cmds.RegisterAlias("use", "cast");
  cmds.RegisterAlias("spell", "cast");

  cmds.RegisterCommand("castrandom", [L](const std::string& args) -> bool {
    const auto spells = SplitCommaList(args);
    if (!spells.empty()) {
      const auto index = static_cast<std::size_t>(std::rand()) % spells.size();
      CallLua(L, "CastSpellByName", spells[index]);
    }
    return true;
  });

  cmds.RegisterCommand("castsequence", [L](const std::string& args) -> bool {
    if (args.empty()) return true;
    auto comma = args.find(',');
    std::string first_spell =
        (comma != std::string::npos) ? args.substr(0, comma) : args;

    if (first_spell.find("reset=") == 0) {
      auto space = first_spell.find(' ');
      if (space != std::string::npos)
        first_spell = first_spell.substr(space + 1);
      else
        return true;
    }
    first_spell = TrimLeft(first_spell);
    if (!first_spell.empty()) {
      CallLua(L, "CastSpellByName", first_spell);
    }
    return true;
  });

  cmds.RegisterCommand("userandom", [L](const std::string& args) -> bool {
    const auto entries = SplitCommaList(args);
    if (!entries.empty()) {
      const auto index = static_cast<std::size_t>(std::rand()) % entries.size();
      CallLua(L, "CastSpellByName", entries[index]);
    }
    return true;
  });

  cmds.RegisterCommand("equip", [L](const std::string& args) -> bool {
    if (!args.empty()) {
      CallLua(L, "EquipItemByName", args);
    }
    return true;
  });

  cmds.RegisterCommand("equipslot", [L](const std::string& args) -> bool {
    if (args.empty()) return true;
    auto [slot, item] = SplitFirst(args);
    if (item.empty()) return true;
    int slot_id = 0;
    if (TryParseInt(slot, &slot_id)) {
      CallLua(L, "EquipItemByName", item, slot_id);
    }
    return true;
  });

  cmds.RegisterCommand("target", [L](const std::string& args) -> bool {
    if (!args.empty()) {
      CallLua(L, "TargetUnit", args);
    }
    return true;
  });
  cmds.RegisterAlias("tar", "target");

  cmds.RegisterCommand("focus", [L](const std::string& args) -> bool {
    if (args.empty()) {
      CallLua(L, "FocusUnit", "target");
    } else {
      CallLua(L, "FocusUnit", args);
    }
    return true;
  });

  cmds.RegisterCommand("clearfocus",
                        [L](const std::string& ) -> bool {
                          CallLua(L, "ClearFocus");
                          return true;
                        });

  cmds.RegisterCommand("assist", [L](const std::string& args) -> bool {
    if (args.empty()) {
      CallLua(L, "AssistUnit", "target");
    } else {
      CallLua(L, "AssistUnit", args);
    }
    return true;
  });

  cmds.RegisterCommand("cleartarget",
                        [L](const std::string& ) -> bool {
                          CallLua(L, "ClearTarget");
                          return true;
                        });

  cmds.RegisterCommand("targetenemy",
                        [L](const std::string& ) -> bool {
                          CallLua(L, "TargetNearestEnemy");
                          return true;
                        });

  cmds.RegisterCommand("targetfriend",
                        [L](const std::string& ) -> bool {
                          CallLua(L, "TargetNearestFriend");
                          return true;
                        });

  cmds.RegisterCommand("targetenemyplayer",
                        [L](const std::string& ) -> bool {
                          CallLua(L, "TargetNearestEnemyPlayer");
                          return true;
                        });

  cmds.RegisterCommand("targetfriendplayer",
                        [L](const std::string& ) -> bool {
                          CallLua(L, "TargetNearestFriendPlayer");
                          return true;
                        });

  cmds.RegisterCommand("targetparty",
                        [L](const std::string& ) -> bool {
                          CallLua(L, "TargetNearestPartyMember");
                          return true;
                        });

  cmds.RegisterCommand("targetraid",
                        [L](const std::string& ) -> bool {
                          CallLua(L, "TargetNearestRaidMember");
                          return true;
                        });

  cmds.RegisterCommand("who", [session](const std::string& args) -> bool {
    if (session)
      session->interaction().SendWho(args);
    return true;
  });

  cmds.RegisterCommand("invite", [session](const std::string& args) -> bool {
    if (!args.empty() && session)
      session->interaction().SendGroupInvite(Trim(args));
    return true;
  });
  cmds.RegisterAlias("inv", "invite");

  cmds.RegisterCommand("uninvite", [session](const std::string& args) -> bool {
    if (!args.empty() && session)
      session->interaction().SendGroupUninvite(Trim(args));
    return true;
  });
  cmds.RegisterAlias("kick", "uninvite");
  cmds.RegisterAlias("votekick", "uninvite");

  cmds.RegisterCommand("promote", [session](const std::string& args) -> bool {
    if (!args.empty() && session) {
      const auto target_guid =
          detail::ResolveGroupPlayerTargetGuid(session, Trim(args), false);
      if (!target_guid.IsEmpty()) {
        session->interaction().SendGroupSetLeader(target_guid.GetRawValue());
      }
    }
    return true;
  });

  cmds.RegisterCommand("maintank", [L](const std::string& args) -> bool {
    CallLua(L, "SetPartyAssignment", "MAINTANK", args.empty() ? "target" : Trim(args));
    return true;
  });

  cmds.RegisterCommand("mainassist", [L](const std::string& args) -> bool {
    CallLua(L, "SetPartyAssignment", "MAINASSIST", args.empty() ? "target" : Trim(args));
    return true;
  });

  cmds.RegisterCommand("leave", [session](const std::string& ) -> bool {
    if (session)
      session->interaction().SendGroupDisband();
    return true;
  });

  cmds.RegisterCommand("follow", [L](const std::string& args) -> bool {
    if (args.empty()) {
      CallLua(L, "FollowUnit", "target");
    } else {
      CallLua(L, "FollowUnit", args);
    }
    return true;
  });
  cmds.RegisterAlias("f", "follow");

  cmds.RegisterCommand("friend", [session](const std::string& args) -> bool {
    if (args.empty() || !session) return true;
    auto [name, note] = SplitFirst(args);
    session->interaction().SendAddFriend(Trim(name), note);
    return true;
  });
  cmds.RegisterAlias("friends", "friend");

  cmds.RegisterCommand("removefriend",
                        [session](const std::string& args) -> bool {
                          if (args.empty() || !session) return true;
                          auto guid =
                              ResolveNameOrUnitId(session, Trim(args));
                          if (!guid.IsEmpty())
                            session->interaction().SendDelFriend(
                                guid.GetRawValue());
                          else
                            openwow::diagnostics::Log(
                                openwow::diagnostics::LogLevel::kWarn,
                                "SlashCmd: /removefriend — cannot resolve '" +
                                    Trim(args) + "'");
                          return true;
                        });
  cmds.RegisterAlias("unfriend", "removefriend");

  cmds.RegisterCommand("ignore", [session](const std::string& args) -> bool {
    if (!args.empty() && session)
      session->interaction().SendAddIgnore(Trim(args));
    return true;
  });

  cmds.RegisterCommand("unignore",
                        [session](const std::string& args) -> bool {
                          if (args.empty() || !session) return true;
                          auto guid =
                              ResolveNameOrUnitId(session, Trim(args));
                          if (!guid.IsEmpty())
                            session->interaction().SendDelIgnore(
                                guid.GetRawValue());
                          else
                            openwow::diagnostics::Log(
                                openwow::diagnostics::LogLevel::kWarn,
                                "SlashCmd: /unignore — cannot resolve '" +
                                    Trim(args) + "'");
                          return true;
                        });

  cmds.RegisterCommand("ginvite", [session](const std::string& args) -> bool {
    if (!args.empty() && session)
      session->interaction().SendGuildInvite(Trim(args));
    return true;
  });
  cmds.RegisterAlias("guildinvite", "ginvite");

  cmds.RegisterCommand("gquit",
                        [session](const std::string& ) -> bool {
                          if (session) session->interaction().SendGuildLeave();
                          return true;
                        });
  cmds.RegisterAlias("guildquit", "gquit");

  cmds.RegisterCommand("gkick", [session](const std::string& args) -> bool {
    if (!args.empty() && session)
      session->interaction().SendGuildRemove(Trim(args));
    return true;
  });
  cmds.RegisterAlias("guildkick", "gkick");

  cmds.RegisterCommand("gpromote", [session](const std::string& args) -> bool {
    if (!args.empty() && session)
      session->interaction().SendGuildPromote(Trim(args));
    return true;
  });
  cmds.RegisterAlias("guildpromote", "gpromote");

  cmds.RegisterCommand("gdemote", [session](const std::string& args) -> bool {
    if (!args.empty() && session)
      session->interaction().SendGuildDemote(Trim(args));
    return true;
  });
  cmds.RegisterAlias("guilddemote", "gdemote");

  cmds.RegisterCommand("gmotd", [session](const std::string& args) -> bool {
    if (session)
      session->interaction().SendGuildSetMOTD(args);
    return true;
  });
  cmds.RegisterAlias("guildmotd", "gmotd");

  cmds.RegisterCommand("ginfo",
                        [session](const std::string& ) -> bool {
                          if (session) session->interaction().SendGuildInfo();
                          return true;
                        });
  cmds.RegisterAlias("guildinfo", "ginfo");

  cmds.RegisterCommand("groster",
                        [session](const std::string& ) -> bool {
                          if (session) session->interaction().SendGuildRoster();
                          return true;
                        });
  cmds.RegisterAlias("guildroster", "groster");

  cmds.RegisterCommand("gleader", [session](const std::string& args) -> bool {
    if (!args.empty() && session)
      session->interaction().SendGuildSetLeader(Trim(args));
    return true;
  });
  cmds.RegisterAlias("guildleader", "gleader");

  cmds.RegisterCommand("gdisband",
                        [session](const std::string& ) -> bool {
                          if (session)
                            session->interaction().SendGuildDisband();
                          return true;
                        });

  cmds.RegisterCommand("join", [session](const std::string& args) -> bool {
    if (args.empty() || !session) return true;
    auto [channel, password] = SplitFirst(args);
    const auto trimmed_channel = Trim(channel);
    if (!ChatSystem::Get().QueuePendingNumberedChannel(trimmed_channel).has_value()) {
      return true;
    }
    session->interaction().SendJoinChannel(0, trimmed_channel, password);
    return true;
  });
  cmds.RegisterAlias("channel", "join");
  cmds.RegisterAlias("chan", "join");

  cmds.RegisterCommand("leavechannel",
                        [L](const std::string& args) -> bool {
                          if (!args.empty()) {
                            CallLua(L, "LeaveChannelByName", Trim(args));
                          }
                          return true;
                        });
  cmds.RegisterAlias("chatleave", "leavechannel");

  cmds.RegisterCommand("trade", [L](const std::string& ) -> bool {
    CallLua(L, "InitiateTrade", "target");
    return true;
  });

  cmds.RegisterCommand("logout",
                        [](const std::string& ) -> bool {
                          openwow::net::ClientServices::Instance().RequestLogout();
                          return true;
                        });
  cmds.RegisterAlias("camp", "logout");

  cmds.RegisterCommand("quit",
                        [](const std::string& ) -> bool {
                          openwow::net::ClientServices::Instance().RequestQuit();
                          return true;
                        });
  cmds.RegisterAlias("exit", "quit");

  cmds.RegisterCommand("played",
                        [L](const std::string& ) -> bool {
                          CallLua(L, "RequestTimePlayed");
                          return true;
                        });

  cmds.RegisterCommand("time", [L](const std::string& ) -> bool {
    AddCurrentDateChatMessage(L);
    return true;
  });

  cmds.RegisterCommand("random", [session](const std::string& args) -> bool {
    if (!session) return true;
    std::uint32_t min_val = 1;
    std::uint32_t max_val = 100;
    if (!args.empty()) {
      auto dash = args.find('-');
      if (dash != std::string::npos) {
        min_val = static_cast<std::uint32_t>(
            std::atoi(args.substr(0, dash).c_str()));
        max_val = static_cast<std::uint32_t>(
            std::atoi(args.substr(dash + 1).c_str()));
      } else {
        max_val =
            static_cast<std::uint32_t>(std::atoi(args.c_str()));
        if (max_val == 0) max_val = 100;
      }
    }
    session->interaction().SendRandomRoll(min_val, max_val);
    return true;
  });
  cmds.RegisterAlias("roll", "random");
  cmds.RegisterAlias("rnd", "random");

  cmds.RegisterCommand("readycheck",
                        [session](const std::string& ) -> bool {
                          if (session) {
                            openwow::game::GroupSystem::Get().DoReadyCheck(
                                [session]() { session->interaction().SendReadyCheck(); },
                                [](const int message_id) { DisplaySystemMessage(message_id); });
                          }
                          return true;
                        });

  cmds.RegisterCommand("reload", [L](const std::string& ) -> bool {
    CallLua(L, "ReloadUI");
    return true;
  });
  cmds.RegisterAlias("reloadui", "reload");

  cmds.RegisterCommand("help", [L](const std::string& ) -> bool {
    AddChatMessage(
        L,
        "|cff00ff00--- Slash Commands ---|r\n"
        "/say, /yell, /whisper, /party, /raid, /guild, /officer - Chat\n"
        "/1 .. /20 - Channel message\n"
        "/join, /leave - Channel management\n"
        "/invite, /kick, /who, /friend, /ignore - Social\n"
        "/ginvite, /gquit, /gkick, /gpromote, /gdemote, /gmotd, /ginfo - Guild\n"
        "/cast, /use, /equip - Spells & items\n"
        "/target, /focus, /assist, /follow - Targeting\n"
        "/dance, /sit, /stand, /duel, /emote - Actions\n"
        "/logout, /quit, /camp - Session\n"
        "/played, /time, /roll, /readycheck - Misc\n"
        "/script, /run, /dump, /console, /reload - Debug");
    return true;
  });

  cmds.RegisterCommand("script", [L](const std::string& args) -> bool {
    if (!args.empty()) {
      RunSlashLuaSource(L, args, "=(slash command)");
    }
    return true;
  });
  cmds.RegisterAlias("run", "script");

  cmds.RegisterCommand("dump", [L](const std::string& args) -> bool {
    if (!args.empty()) {
      const std::string source = "DEFAULT_CHAT_FRAME:AddMessage(tostring(" + args + "))";
      RunSlashLuaSource(L, source, "=(slash dump)");
    }
    return true;
  });

  cmds.RegisterCommand("console", [L](const std::string& args) -> bool {
    if (!args.empty()) {
      CallLua(L, "ConsoleExec", args);
    }
    return true;
  });

  cmds.RegisterCommand("click", [L](const std::string& args) -> bool {
    if (!args.empty()) {
      ClickLuaGlobal(L, Trim(args));
    }
    return true;
  });

  cmds.RegisterCommand("macro", [L](const std::string& ) -> bool {
    CallLua(L, "ShowMacroFrame");
    return true;
  });

  cmds.RegisterCommand("stopmacro", [L](const std::string& ) -> bool {
    CallLua(L, "StopMacro");
    return true;
  });

  cmds.RegisterCommand("cancelaura", [L](const std::string& args) -> bool {
    if (!args.empty()) {
      CallLua(L, "CancelUnitBuff", "player", args);
    }
    return true;
  });

  cmds.RegisterCommand("cancelform",
                        [L](const std::string& ) -> bool {
                          CallLua(L, "CancelShapeshiftForm");
                          return true;
                        });

  cmds.RegisterCommand("dismount",
                        [session](const std::string& ) -> bool {
                          if (session)
                            session->interaction().SendCancelMountAura();
                          return true;
                        });

  cmds.RegisterCommand("stopattack",
                        [session](const std::string& ) -> bool {
                          if (session)
                            session->interaction().SendAttackStop();
                          return true;
                        });

  cmds.RegisterCommand("stopcasting",
                        [L](const std::string& ) -> bool {
                          CallLua(L, "SpellStopCasting");
                          return true;
                        });

  cmds.RegisterCommand("startattack",
                        [L](const std::string& ) -> bool {
                          CallLua(L, "StartAttack");
                          return true;
                        });

  cmds.RegisterCommand("petattack",
                        [L](const std::string& ) -> bool {
                          CallLua(L, "PetAttack");
                          return true;
                        });
  cmds.RegisterCommand("petfollow",
                        [L](const std::string& ) -> bool {
                          CallLua(L, "PetFollow");
                          return true;
                        });
  cmds.RegisterCommand("petstay",
                        [L](const std::string& ) -> bool {
                          CallLua(L, "PetWait");
                          return true;
                        });
  cmds.RegisterCommand("petpassive",
                        [L](const std::string& ) -> bool {
                          CallLua(L, "PetPassiveMode");
                          return true;
                        });
  cmds.RegisterCommand("petdefensive",
                        [L](const std::string& ) -> bool {
                          CallLua(L, "PetDefensiveMode");
                          return true;
                        });
  cmds.RegisterCommand("petaggressive",
                        [L](const std::string& ) -> bool {
                          CallLua(L, "PetAggressiveMode");
                          return true;
                        });
  cmds.RegisterCommand("petautocaston",
                        [L](const std::string& args) -> bool {
                          if (!args.empty()) {
                            CallLua(L, "EnableSpellAutocast", args);
                          }
                          return true;
                        });
  cmds.RegisterCommand("petautocastoff",
                        [L](const std::string& args) -> bool {
                          if (!args.empty()) {
                            CallLua(L, "DisableSpellAutocast", args);
                          }
                          return true;
                        });

  cmds.RegisterCommand("equipset", [L](const std::string& args) -> bool {
    if (!args.empty()) {
      CallLua(L, "UseEquipmentSet", args);
    }
    return true;
  });

  cmds.RegisterCommand("leavevehicle",
                        [L](const std::string& ) -> bool {
                          CallLua(L, "VehicleExit");
                          return true;
                        });

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "SlashCommandHandler: registered all slash commands "
                     "(chat, social, guild, channel, emote, spell, target, "
                     "session, debug, pet)");
}

}
