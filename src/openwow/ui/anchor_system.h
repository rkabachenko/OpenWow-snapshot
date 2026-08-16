#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::ui {

enum class AnchorPoint : uint8_t {
  TopLeft,
  Top,
  TopRight,
  Left,
  Center,
  Right,
  BottomLeft,
  Bottom,
  BottomRight,
  COUNT_
};

struct AnchorPointHash {
  std::size_t operator()(AnchorPoint p) const noexcept {
    return static_cast<std::size_t>(p);
  }
};

struct FrameAnchor {
  AnchorPoint point{AnchorPoint::TopLeft};
  std::string relativeTo;
  AnchorPoint relativePoint{AnchorPoint::TopLeft};
  float offsetX{0.0f};
  float offsetY{0.0f};
};

struct FrameSize {
  float width{0.0f};
  float height{0.0f};
};

struct FrameRect {
  float left{0.0f};
  float top{0.0f};
  float right{0.0f};
  float bottom{0.0f};
};

class AnchorSystem {
 public:
  AnchorSystem() = default;
  ~AnchorSystem() = default;

  void SetPoint(const std::string& frame, const FrameAnchor& anchor);

  void SetAllPoints(const std::string& frame, const std::string& relativeTo);

  void ClearPoints(const std::string& frame);

  void ClearPoint(const std::string& frame, AnchorPoint point);

  [[nodiscard]] std::vector<FrameAnchor> GetAnchors(
      const std::string& frame) const;

  [[nodiscard]] std::size_t GetAnchorCount(const std::string& frame) const;

  [[nodiscard]] bool HasAnchor(const std::string& frame,
                               AnchorPoint point) const;

  void SetSize(const std::string& frame, float width, float height);

  [[nodiscard]] FrameSize GetSize(const std::string& frame) const;

  [[nodiscard]] FrameRect ResolvePosition(const std::string& frame) const;

  [[nodiscard]] float GetEffectiveWidth(const std::string& frame) const;
  [[nodiscard]] float GetEffectiveHeight(const std::string& frame) const;

  [[nodiscard]] std::size_t GetFrameCount() const;

  void Reset();

 private:
  struct FrameData {
    std::unordered_map<AnchorPoint, FrameAnchor, AnchorPointHash> anchors;
    FrameSize size{};
  };

  [[nodiscard]] const FrameData* FindFrame(const std::string& name) const;
  FrameData& GetOrCreate(const std::string& name);

  std::unordered_map<std::string, FrameData> frames_;
};

}
