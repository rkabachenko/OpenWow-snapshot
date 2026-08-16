#pragma once

#include "openwow/game/object_guid.h"
#include "openwow/game/object_presentation_snapshot.h"
#include "openwow/game/targeting/world_click_types.h"
#include "openwow/render/api/math/render_math_types.h"
#include "openwow/world/coordinates/frustum.h"
#include "openwow/world/interaction/world_cursor_policy.h"

#include <cstdint>
#include <functional>

namespace openwow::render {

class ObjectRenderer;

}

namespace openwow::world {

class CollisionManager;

}

namespace openwow::render {

struct PickResult {
  enum class HitType : std::uint8_t {
    kNone = 0,
    kUnit,
    kGameObject,
    kCorpse,
    kTerrain,
    kItem,
  };

  bool hit{false};
  game::ObjectGuid guid;
  float distance{0.0f};
  float world_x{0.0f};
  float world_y{0.0f};
  float world_z{0.0f};
  HitType type{HitType::kNone};
};

[[nodiscard]] constexpr PickResult SelectNearestPick(PickResult model,
                                                     const PickResult &terrain) {
  return terrain.hit && (!model.hit || terrain.distance < model.distance)
             ? terrain
             : model;
}

struct WorldRay {
  float origin_x{0.0f};
  float origin_y{0.0f};
  float origin_z{0.0f};
  float dir_x{0.0f};
  float dir_y{0.0f};
  float dir_z{0.0f};
};

class WorldFrame {
public:
  WorldFrame() = default;

  void Initialize(std::uint32_t width, std::uint32_t height);

  void Resize(std::uint32_t width, std::uint32_t height);

  void SetCamera(RenderMatrix4x4View view_4x4, RenderMatrix4x4View proj_4x4);

  [[nodiscard]] const RenderMatrix4x4 &GetViewMatrix() const {
    return view_;
  }

  [[nodiscard]] const RenderMatrix4x4 &GetProjectionMatrix() const {
    return proj_;
  }

  [[nodiscard]] RenderVec3 GetCameraPosition() const;

  [[nodiscard]] RenderMatrix4x4
  BuildGroundFacingBillboardMatrix(RenderVec3View world_position) const;

  [[nodiscard]] std::uint32_t GetWidth() const {
    return width_;
  }
  [[nodiscard]] std::uint32_t GetHeight() const {
    return height_;
  }

  [[nodiscard]] WorldRay ScreenToWorldRay(int screen_x, int screen_y) const;

  [[nodiscard]] RenderScreenProjection WorldToScreen(RenderVec3View world_pos) const;

  [[nodiscard]] PickResult Pick(int screen_x, int screen_y) const;

  void OnMouseDown(int button, int x, int y);
  void OnMouseUp(int button, int x, int y);
  void OnMouseMove(int x, int y);

  void SetCursorPosition(int x, int y) noexcept {
    mouse_x_ = x;
    mouse_y_ = y;
  }
  void OnMouseWheel(float delta);

  void HandleLeftClick(int x, int y);

  void ApplyTargetSelection(const PickResult &pick);

  void HandleRightClick(int x, int y);

  [[nodiscard]] PickResult GetMouseoverTarget() const {
    return mouseover_pick_;
  }

  [[nodiscard]] game::ObjectGuid GetNameplateHoverGuid() const {
    return nameplate_hover_guid_;
  }

  [[nodiscard]] game::ObjectGuid GetMouseoverGuid() const {
    return mouseover_guid_;
  }
  [[nodiscard]] PickResult GetHoverPick() const {
    return hover_pick_;
  }
  [[nodiscard]] bool TryBuildTerrainClickInput(const PickResult &pick,
                                               game::targeting::WorldTerrainClick *out) const;
  [[nodiscard]] bool TryBuildHoverTerrainClickInput(game::targeting::WorldTerrainClick *out) const;

  void Update(float dt);

  [[nodiscard]] bool IsInFrustum(float cx, float cy, float cz, float radius) const;

  void BindObjectPresentation(const game::ObjectPresentationSnapshot *objects) {
    objects_ = objects;
  }

  void BindPickingScene(const ObjectRenderer *renderer, const world::CollisionManager *collision) {
    object_renderer_ = renderer;
    collision_ = collision;
  }

  void SetCursorContext(world::CursorInfoContext context) {
    cursor_context_ = context;
  }
  using ScrollCallback = std::function<void(float)>;
  using MouseDeltaCallback = std::function<void(float, float)>;
  using InteractCallback = std::function<void(game::ObjectGuid)>;
  using TerrainClickCallback = std::function<void(const game::targeting::WorldTerrainClick &)>;
  using TargetSelectionCallback = std::function<void(game::ObjectGuid)>;

  void SetScrollCallback(ScrollCallback fn) {
    scroll_cb_ = std::move(fn);
  }
  void SetMouseDeltaCallback(MouseDeltaCallback fn) {
    mouse_delta_cb_ = std::move(fn);
  }
  void SetInteractCallback(InteractCallback fn) {
    interact_cb_ = std::move(fn);
  }
  void SetTerrainClickCallback(TerrainClickCallback fn) {
    terrain_click_cb_ = std::move(fn);
  }
  void SetTargetSelectionCallback(TargetSelectionCallback fn) {
    target_selection_cb_ = std::move(fn);
  }

  bool SetMouseoverGuid(game::ObjectGuid guid);

  void UpdateNameplateHover(std::uint64_t target_guid);

  bool SetExplicitMouseoverGuid(game::ObjectGuid guid);

  void ClearSelectionHighlights();

  void SetSelectionHighlight(std::uint64_t guid) {
    highlight_guid_ = guid;
  }
  [[nodiscard]] std::uint64_t GetSelectionHighlight() const {
    return highlight_guid_;
  }

  void SetSoftHighlight(std::uint64_t guid) {
    soft_highlight_guid_ = guid;
  }
  [[nodiscard]] std::uint64_t GetSoftHighlight() const {
    return soft_highlight_guid_;
  }

  [[nodiscard]] static RenderVec3 GetFacingDirection(float yaw);

  enum class CursorMode : int {
    kNone = 0,
    kInteract = 1,
    kCombat = 3,
  };

  [[nodiscard]] CursorMode GetCursorMode() const {
    return cursor_mode_;
  }
  void SetCursorMode(CursorMode mode) {
    cursor_mode_ = mode;
  }

  static constexpr float kMaxPickDistance = 100.0f;

  static constexpr int kClickDragThreshold = 4;

private:

  [[nodiscard]] static RenderMatrix4x4 MatMul4x4(RenderMatrix4x4View a, RenderMatrix4x4View b);

  PickResult RayTestTerrain(const WorldRay &ray) const;

  std::uint32_t width_{1024};
  std::uint32_t height_{768};

  RenderMatrix4x4 view_{kRenderIdentityMatrix4x4};
  RenderMatrix4x4 proj_{kRenderIdentityMatrix4x4};
  world::Frustum frustum_;

  game::ObjectGuid nameplate_hover_guid_;
  game::ObjectGuid mouseover_guid_;
  PickResult mouseover_pick_;
  PickResult hover_pick_;
  int mouse_x_{0};
  int mouse_y_{0};

  bool lmb_down_{false};
  bool rmb_down_{false};
  int click_start_x_{0};
  int click_start_y_{0};

  CursorMode cursor_mode_{CursorMode::kNone};

  std::uint64_t highlight_guid_{0};
  std::uint64_t soft_highlight_guid_{0};

  RenderMatrix4x4 vp_{kRenderIdentityMatrix4x4};

  float viewport_left_{0.0f};
  float viewport_top_{0.0f};
  float viewport_right_{1.0f};
  float viewport_bottom_{1.0f};

  const game::ObjectPresentationSnapshot *objects_{nullptr};
  const ObjectRenderer *object_renderer_{nullptr};
  const world::CollisionManager *collision_{nullptr};
  world::CursorInfoContext cursor_context_;

  ScrollCallback scroll_cb_;
  MouseDeltaCallback mouse_delta_cb_;
  InteractCallback interact_cb_;
  TerrainClickCallback terrain_click_cb_;
  TargetSelectionCallback target_selection_cb_;
};

}
