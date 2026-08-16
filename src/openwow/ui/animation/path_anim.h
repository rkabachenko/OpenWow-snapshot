
#pragma once

#include "openwow/ui/animation/animation.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace openwow::ui::anim {

class PathAnim;

struct PathControlPointData {
  float offset_x{0.0f};
  float offset_y{0.0f};
  float adjusted_curve{0.0f};
  int order{-1};
};

class PathControlPoint {
 public:
  explicit PathControlPoint(PathAnim* parent = nullptr);
  ~PathControlPoint();

  const std::string& GetName() const { return name_; }
  void SetName(std::string name) { name_ = std::move(name); }

  PathAnim* GetParent() const { return parent_; }
  void SetParentRaw(PathAnim* parent) { parent_ = parent; }

  float GetOffsetX() const { return offset_x_; }
  float GetOffsetY() const { return offset_y_; }
  void SetOffset(float x, float y);

  float GetAdjustedCurve() const { return adjusted_curve_; }
  void SetAdjustedCurve(float adjusted_curve) { adjusted_curve_ = adjusted_curve; }

  int GetOrder() const { return order_; }
  void SetOrder(int order, bool notify_parent = true, bool validate = false);
  void SetLuaObjectRef(lua_State* L, int lua_ref);

 private:
  std::string name_;
  PathAnim* parent_{nullptr};
  float offset_x_{0.0f};
  float offset_y_{0.0f};
  float adjusted_curve_{0.0f};
  int order_{-1};
  lua_State* object_lua_state_{nullptr};
  int object_lua_ref_{LUA_NOREF};
};

class PathAnim : public Animation {
 public:
  AnimKind GetKind() const override { return AnimKind::Path; }

  void SetCurve(uint8_t curve) { curve_ = curve; }
  uint8_t GetCurve() const { return curve_; }

  PathControlPoint* CreateControlPoint(const std::string& name = "");
  PathControlPoint* AddControlPoint(const PathControlPointData& cp);
  bool ReparentControlPoint(PathControlPoint& point, PathAnim* new_parent);
  void OnControlPointOrderChanged(PathControlPoint& point);

  size_t GetNumControlPoints() const { return ordered_control_points_.size(); }
  PathControlPoint* GetControlPoint(size_t idx);
  const PathControlPoint* GetControlPoint(size_t idx) const;
  int GetMaxOrder() const;
  void ClearControlPoints();

  void Apply(float progress) override;
  void ResetEffect() override;
  void FinalizeXmlLoad() override;

 float GetCurrentX() const { return current_x_; }
  float GetCurrentY() const { return current_y_; }

 private:
  static constexpr int kMaxControlPointOrder = 99;

  std::unique_ptr<PathControlPoint> ExtractOwnedControlPoint(PathControlPoint& point);
  void AdoptControlPoint(std::unique_ptr<PathControlPoint> point);
  void ApplySignedFactor(float factor) override;

  int FindRepresentativeIndexByOrder(int order) const;
  void MoveBeforeRepresentative(PathControlPoint& point);
  void InsertRepresentative(PathControlPoint& point);
  void RebuildControlPointCache();

  std::vector<std::unique_ptr<PathControlPoint>> control_points_;
  std::vector<PathControlPoint*> ordered_control_points_;
  uint8_t curve_{0};
  float current_x_{0.0f};
  float current_y_{0.0f};
};

}
