#pragma once

#include <functional>
#include <string>

struct lua_State;

namespace openwow::game {
class MinimapPingSystem;
class WorldSession;
}
namespace openwow::render {
class TextureManager;
class WorldFrame;
}
namespace openwow::vfs {
class VirtualFileSystem;
}
namespace openwow::world {
class WorldCamera;
}
namespace openwow::audio { class SoundRuntime; }
namespace openwow::ui {
class MinimapSystem;
class WorldMapSystem;
}
namespace openwow::ui::game {
class AddonRuntimeLoader;
struct AddonRuntimeIdentity;
struct TooltipFrameStackSnapshot;
}

namespace openwow::ui::game::runtime {

class FrameEventRuntime;
class FrameInputRouter;
class FrameMaterializer;
class FrameStore;
class FrameTraversalIndex;
class FrameXmlRuntimeLoader;
class MovieFrameRuntime;
class MovieRecordingRuntime;
class RetainedLayout;
class WorldLuaRuntime;
class WorldUiRuntimeHost;

class WorldUiRuntimeContext final {
 public:
  struct Ports {
    FrameStore& frames;
    RetainedLayout& layout;
    FrameInputRouter& input;
    FrameMaterializer& materializer;
    FrameEventRuntime& events;
    MovieFrameRuntime& movies;
    MovieRecordingRuntime& movie_recording;
    FrameTraversalIndex& traversal;
    WorldLuaRuntime& lua;
    WorldUiRuntimeHost& runtime_host;
    FrameXmlRuntimeLoader& frame_xml_loader;
    openwow::ui::MinimapSystem& minimap;
    openwow::game::MinimapPingSystem& minimap_ping;
    openwow::ui::WorldMapSystem& world_map;
    openwow::render::TextureManager& textures;
    openwow::render::WorldFrame& world_frame;
    openwow::world::WorldCamera& world_camera;
    openwow::audio::SoundRuntime& sound;
    const openwow::vfs::VirtualFileSystem*& vfs;
    openwow::game::WorldSession*& session;
    std::function<void(const std::string&, bool)> notify_frame_mutation;
    std::function<bool(bool, TooltipFrameStackSnapshot*)> frame_stack_snapshot;
    std::function<void(float, bool)> set_root_scale;
    std::function<void()> request_world_ui_reload;
  };

  explicit WorldUiRuntimeContext(Ports ports) noexcept;

  [[nodiscard]] FrameStore& frame_store() const noexcept { return ports_.frames; }
  [[nodiscard]] RetainedLayout& retained_layout() const noexcept { return ports_.layout; }
  [[nodiscard]] FrameInputRouter& input_router() const noexcept { return ports_.input; }
  [[nodiscard]] FrameMaterializer& frame_materializer() const noexcept { return ports_.materializer; }
  [[nodiscard]] FrameEventRuntime& frame_events() const noexcept { return ports_.events; }
  [[nodiscard]] MovieFrameRuntime& movie_runtime() const noexcept { return ports_.movies; }
  [[nodiscard]] MovieRecordingRuntime& movie_recording_runtime() const noexcept {
    return ports_.movie_recording;
  }
  [[nodiscard]] FrameTraversalIndex& traversal() const noexcept { return ports_.traversal; }
  [[nodiscard]] openwow::ui::MinimapSystem& minimap_state() const noexcept { return ports_.minimap; }
  [[nodiscard]] openwow::game::MinimapPingSystem& minimap_ping() const noexcept { return ports_.minimap_ping; }
  [[nodiscard]] openwow::ui::WorldMapSystem& world_map() const noexcept { return ports_.world_map; }
  [[nodiscard]] openwow::render::TextureManager& texture_manager() const noexcept { return ports_.textures; }
  [[nodiscard]] openwow::render::WorldFrame& world_frame() const noexcept { return ports_.world_frame; }
  [[nodiscard]] openwow::world::WorldCamera& world_camera() const noexcept { return ports_.world_camera; }
  [[nodiscard]] openwow::audio::SoundRuntime& sound_runtime() const noexcept { return ports_.sound; }
  [[nodiscard]] const openwow::vfs::VirtualFileSystem* vfs() const noexcept { return ports_.vfs; }
  [[nodiscard]] openwow::game::WorldSession* world_session() const noexcept { return ports_.session; }
  [[nodiscard]] AddonRuntimeLoader* addon_runtime_loader() const noexcept;
  [[nodiscard]] const AddonRuntimeIdentity& addon_runtime_identity() const noexcept;
  [[nodiscard]] lua_State* lua_state() const noexcept;
  [[nodiscard]] bool is_loaded() const noexcept;
  [[nodiscard]] bool is_initialized() const noexcept;
  [[nodiscard]] float screen_width() const noexcept;
  [[nodiscard]] float screen_height() const noexcept;
  [[nodiscard]] float root_scale() const noexcept;
  void SetRootScale(float scale, bool force) const;
  void RequestWorldUiReload() const;
  void NotifyFrameInputCategoryMutation(const std::string& frame_name,
                                        bool reindex_only) const;
  bool BuildFrameStackSnapshot(bool show_hidden,
                               TooltipFrameStackSnapshot* snapshot) const;
  void DestroyNamedFrame(const std::string& frame_name) const;

  static WorldUiRuntimeContext* FromLua(lua_State* state) noexcept;
  static WorldUiRuntimeContext* FromActiveLua() noexcept;

 private:
  Ports ports_;
};

inline constexpr char kWorldUiRuntimeContextRegistryKey[] =
    "openwow.world_ui_runtime_context";

}
