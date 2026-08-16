#pragma once

#include <memory>

struct lua_State;

namespace openwow::game {
class BindingProfiles;
class CursorSurface;
class SpellbookSystem;
class SpellCastRuntime;
class WorldSession;
namespace simple_script {
struct SimpleScript;
}
}

namespace openwow::ui::lua {
class FrameScriptRuntime;
class LuaVm;
struct NativeBindingCatalog;
}

namespace openwow::ui {
class AddOnsData;
}

namespace openwow::ui::game {
class CVarSystem;
class GameUIManager;
class ScriptEventDispatch;
class SecureExecution;
namespace detail {
struct SoundLuaContext;
}

namespace runtime {

class WorldUiRuntimeContext;

class WorldLuaRuntime final {
 public:
  explicit WorldLuaRuntime(GameUIManager& owner) noexcept;
  ~WorldLuaRuntime();

  WorldLuaRuntime(const WorldLuaRuntime&) = delete;
  WorldLuaRuntime& operator=(const WorldLuaRuntime&) = delete;

  [[nodiscard]] bool Create(openwow::game::WorldSession* session,
                            openwow::game::CursorSurface& cursor);
  void CompleteFrameXmlLoad();
  void Destroy();

  [[nodiscard]] lua_State* state() const noexcept { return lua_; }

  [[nodiscard]] static openwow::ui::lua::NativeBindingCatalog
  FrameNativeBindingCatalog();
  static void InstallFrameRuntimeCallbacks(lua_State* state);
  static void UninstallFrameRuntimeCallbacks(lua_State* state);

 private:
  struct TypedContexts final {
    openwow::ui::AddOnsData* addons{nullptr};
    WorldUiRuntimeContext* runtime_context{nullptr};
    openwow::game::BindingProfiles* binding_profiles{nullptr};
    openwow::game::WorldSession* session{nullptr};
    openwow::game::SpellCastRuntime* spell_cast_runtime{nullptr};
    openwow::game::SpellbookSystem* spellbook{nullptr};
    CVarSystem* cvars{nullptr};
    SecureExecution* security{nullptr};
    ScriptEventDispatch* events{nullptr};
    detail::SoundLuaContext* sound{nullptr};
  };

  static bool InitializeLuaVm(openwow::ui::lua::LuaVm& vm);
  static void BindTypedContexts(lua_State& state,
                                const TypedContexts& contexts);
  static thread_local const TypedContexts* creating_typed_contexts_;

  GameUIManager& owner_;
  TypedContexts typed_contexts_;
  lua_State* lua_{nullptr};
  openwow::game::simple_script::SimpleScript* simple_script_{nullptr};
  std::unique_ptr<detail::SoundLuaContext> sound_lua_context_;
  std::unique_ptr<openwow::ui::lua::FrameScriptRuntime> frame_script_runtime_;
};

void LuaWatchdogNoteFrame();

}
}
