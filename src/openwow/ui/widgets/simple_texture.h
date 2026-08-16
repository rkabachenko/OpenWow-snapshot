
#pragma once

#include "openwow/render/resources/textures/texture_asset.h"
#include "openwow/ui/ui_aspect_scales.h"
#include "openwow/ui/widgets/script_region.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_set>

namespace openwow::ui::xml {
struct ErrorContext;
struct XMLFrameDef;
}

namespace openwow::render {

}

namespace openwow::ui::widgets {

enum class BlendMode : uint8_t { Disable = 0, Alphakey = 1, Blend = 2, Add = 3, Mod = 4, COUNT_ };
enum class TextureGradientOrientation : uint8_t { Horizontal = 0, Vertical = 1 };

struct TextureGradientColor {
  float r{0.0f};
  float g{0.0f};
  float b{0.0f};
  float a{1.0f};
};

struct TextureCoordPoint {
  float u{0.0f};
  float v{0.0f};
};

struct VertexQuadPoint {
  float x{0.0f};
  float y{0.0f};
};

struct TextureCoordQuad {
  TextureCoordPoint upperLeft{0.0f, 0.0f};
  TextureCoordPoint lowerLeft{0.0f, 1.0f};
  TextureCoordPoint upperRight{1.0f, 0.0f};
  TextureCoordPoint lowerRight{1.0f, 1.0f};

  static constexpr float kAnchorOffsetLengthEpsilon = 0.00000011920929f;
  static constexpr float kLowerEdgeNormalizationEpsilon = 0.00000023841858f;

  [[nodiscard]] static constexpr TextureCoordQuad FromRect(float left, float right, float top,
                                                           float bottom) noexcept {
    TextureCoordQuad quad;
    quad.upperLeft = {left, top};
    quad.lowerLeft = {left, bottom};
    quad.upperRight = {right, top};
    quad.lowerRight = {right, bottom};
    return quad;
  }

  [[nodiscard]] constexpr std::array<float, 8> ToArray() const noexcept {
    return {
        upperLeft.u,  upperLeft.v,  lowerLeft.u,  lowerLeft.v,
        upperRight.u, upperRight.v, lowerRight.u, lowerRight.v,
    };
  }

  constexpr void OffsetAllPoints(float offsetU, float offsetV) noexcept {
    upperLeft.u += offsetU;
    upperLeft.v += offsetV;
    lowerLeft.u += offsetU;
    lowerLeft.v += offsetV;
    upperRight.u += offsetU;
    upperRight.v += offsetV;
    lowerRight.u += offsetU;
    lowerRight.v += offsetV;
  }

  [[nodiscard]] static constexpr TextureCoordPoint Midpoint(TextureCoordPoint first,
                                                            TextureCoordPoint second) noexcept {
    return {
        (first.u + second.u) * 0.5f,
        (first.v + second.v) * 0.5f,
    };
  }

  [[nodiscard]] constexpr TextureCoordPoint AnchorPoint(FramePoint point) const noexcept {
    switch (point) {
    case FramePoint::TopLeft:
      return upperLeft;
    case FramePoint::Top:
      return Midpoint(upperLeft, upperRight);
    case FramePoint::TopRight:
      return upperRight;
    case FramePoint::Left:
      return Midpoint(upperLeft, lowerLeft);
    case FramePoint::Center:
      return Midpoint(upperLeft, lowerRight);
    case FramePoint::Right:
      return Midpoint(upperRight, lowerRight);
    case FramePoint::BottomLeft:
      return lowerLeft;
    case FramePoint::Bottom:
      return Midpoint(lowerLeft, lowerRight);
    case FramePoint::BottomRight:
      return lowerRight;
    default:
      return {};
    }
  }

  [[nodiscard]] TextureCoordPoint
  ResolveTransformOrigin(FramePoint point, TextureCoordPoint localOffset) const noexcept {
    TextureCoordPoint origin = AnchorPoint(point);
    const float offsetLengthSquared = localOffset.u * localOffset.u + localOffset.v * localOffset.v;
    if (offsetLengthSquared <= kAnchorOffsetLengthEpsilon) {
      return origin;
    }

    TextureCoordPoint lowerEdge{
        lowerRight.u - lowerLeft.u,
        lowerRight.v - lowerLeft.v,
    };
    const float lowerEdgeLengthSquared = lowerEdge.u * lowerEdge.u + lowerEdge.v * lowerEdge.v;
    if (lowerEdgeLengthSquared > kLowerEdgeNormalizationEpsilon) {
      const float inverseLength = 1.0f / std::sqrt(lowerEdgeLengthSquared);
      lowerEdge.u *= inverseLength;
      lowerEdge.v *= inverseLength;
    }

    origin.u += localOffset.u * lowerEdge.u - localOffset.v * lowerEdge.v;
    origin.v += localOffset.u * lowerEdge.v + localOffset.v * lowerEdge.u;
    return origin;
  }

  void RotateAllPointsAround(TextureCoordPoint origin, float radians) noexcept {
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    const auto rotate_point = [origin, cosine, sine](TextureCoordPoint &point) {
      const float translatedU = point.u - origin.u;
      const float translatedV = point.v - origin.v;
      point.u = translatedU * cosine - translatedV * sine + origin.u;
      point.v = translatedU * sine + translatedV * cosine + origin.v;
    };

    rotate_point(upperLeft);
    rotate_point(lowerLeft);
    rotate_point(upperRight);
    rotate_point(lowerRight);
  }

  void ScaleAllPointsToward(TextureCoordPoint origin, float scaleU, float scaleV) noexcept {
    const auto scale_point = [origin, scaleU, scaleV](TextureCoordPoint &point) {
      point.u += (origin.u - point.u) * scaleU;
      point.v += (origin.v - point.v) * scaleV;
    };

    scale_point(upperLeft);
    scale_point(lowerLeft);
    scale_point(upperRight);
    scale_point(lowerRight);
  }
};

struct VertexQuad {
  VertexQuadPoint lowerLeft{0.0f, 0.0f};
  VertexQuadPoint upperLeft{0.0f, 0.0f};
  VertexQuadPoint lowerRight{0.0f, 0.0f};
  VertexQuadPoint upperRight{0.0f, 0.0f};
  float depth{0.0f};

  [[nodiscard]] static constexpr VertexQuad FromRect(const ScreenRect &rect,
                                                     float quadDepth = 0.0f) noexcept {
    VertexQuad quad;
    quad.lowerLeft = {rect.left, rect.bottom};
    quad.upperLeft = {rect.left, rect.top};
    quad.lowerRight = {rect.right, rect.bottom};
    quad.upperRight = {rect.right, rect.top};
    quad.depth = quadDepth;
    return quad;
  }

  [[nodiscard]] constexpr std::array<float, 12> ToArray() const noexcept {
    return {
        lowerLeft.x,  lowerLeft.y,  depth, upperLeft.x,  upperLeft.y,  depth,
        lowerRight.x, lowerRight.y, depth, upperRight.x, upperRight.y, depth,
    };
  }

  constexpr void OffsetAllPoints(float offsetX, float offsetY) noexcept {
    lowerLeft.x += offsetX;
    lowerLeft.y += offsetY;
    upperLeft.x += offsetX;
    upperLeft.y += offsetY;
    lowerRight.x += offsetX;
    lowerRight.y += offsetY;
    upperRight.x += offsetX;
    upperRight.y += offsetY;
  }

  [[nodiscard]] static constexpr VertexQuadPoint Midpoint(VertexQuadPoint first,
                                                          VertexQuadPoint second) noexcept {
    return {
        (first.x + second.x) * 0.5f,
        (first.y + second.y) * 0.5f,
    };
  }

  [[nodiscard]] constexpr VertexQuadPoint AnchorPoint(FramePoint point) const noexcept {
    switch (point) {
    case FramePoint::TopLeft:
      return upperLeft;
    case FramePoint::Top:
      return Midpoint(upperLeft, upperRight);
    case FramePoint::TopRight:
      return upperRight;
    case FramePoint::Left:
      return Midpoint(upperLeft, lowerLeft);
    case FramePoint::Center:
      return Midpoint(upperLeft, lowerRight);
    case FramePoint::Right:
      return Midpoint(upperRight, lowerRight);
    case FramePoint::BottomLeft:
      return lowerLeft;
    case FramePoint::Bottom:
      return Midpoint(lowerLeft, lowerRight);
    case FramePoint::BottomRight:
      return lowerRight;
    default:
      return {};
    }
  }

  [[nodiscard]] VertexQuadPoint ResolveTransformOrigin(FramePoint point,
                                                       VertexQuadPoint localOffset) const noexcept {
    VertexQuadPoint origin = AnchorPoint(point);
    const float offsetLengthSquared = localOffset.x * localOffset.x + localOffset.y * localOffset.y;
    if (offsetLengthSquared <= TextureCoordQuad::kAnchorOffsetLengthEpsilon) {
      return origin;
    }

    VertexQuadPoint lowerEdge{
        lowerRight.x - lowerLeft.x,
        lowerRight.y - lowerLeft.y,
    };
    const float lowerEdgeLengthSquared = lowerEdge.x * lowerEdge.x + lowerEdge.y * lowerEdge.y;
    if (lowerEdgeLengthSquared > TextureCoordQuad::kLowerEdgeNormalizationEpsilon) {
      const float inverseLength = 1.0f / std::sqrt(lowerEdgeLengthSquared);
      lowerEdge.x *= inverseLength;
      lowerEdge.y *= inverseLength;
    }

    origin.x += localOffset.x * lowerEdge.x - localOffset.y * lowerEdge.y;
    origin.y += localOffset.x * lowerEdge.y + localOffset.y * lowerEdge.x;
    return origin;
  }

  void RotateAllPointsAround(VertexQuadPoint origin, float radians) noexcept {
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    const auto rotatePoint = [origin, cosine, sine](VertexQuadPoint &point) {
      const float translatedX = point.x - origin.x;
      const float translatedY = point.y - origin.y;
      point.x = translatedX * cosine - translatedY * sine + origin.x;
      point.y = translatedX * sine + translatedY * cosine + origin.y;
    };

    rotatePoint(lowerLeft);
    rotatePoint(upperLeft);
    rotatePoint(lowerRight);
    rotatePoint(upperRight);
  }

  void ScaleAllPointsToward(VertexQuadPoint origin, float scaleX, float scaleY) noexcept {
    const auto scalePoint = [origin, scaleX, scaleY](VertexQuadPoint &point) {
      point.x += (origin.x - point.x) * scaleX;
      point.y += (origin.y - point.y) * scaleY;
    };

    scalePoint(lowerLeft);
    scalePoint(upperLeft);
    scalePoint(lowerRight);
    scalePoint(upperRight);
  }

  void AddDeltaFrom(const VertexQuad &reference, const VertexQuad &deltaSource) noexcept {
    lowerLeft.x += deltaSource.lowerLeft.x - reference.lowerLeft.x;
    lowerLeft.y += deltaSource.lowerLeft.y - reference.lowerLeft.y;
    upperLeft.x += deltaSource.upperLeft.x - reference.upperLeft.x;
    upperLeft.y += deltaSource.upperLeft.y - reference.upperLeft.y;
    lowerRight.x += deltaSource.lowerRight.x - reference.lowerRight.x;
    lowerRight.y += deltaSource.lowerRight.y - reference.lowerRight.y;
    upperRight.x += deltaSource.upperRight.x - reference.upperRight.x;
    upperRight.y += deltaSource.upperRight.y - reference.upperRight.y;
  }
};

class CSimpleTexture : public CScriptRegion {
public:
  CSimpleTexture() : CScriptRegion(ScriptObjectType::Texture) {}

  [[nodiscard]] bool IsKindOf(ScriptObjectType t) const noexcept override {
    return t == ScriptObjectType::Texture || CScriptRegion::IsKindOf(t);
  }
  [[nodiscard]] bool IsTypeOf(const char *typeName) const noexcept override {
    return StrCaseEq(typeName, "Texture") || CScriptRegion::IsTypeOf(typeName);
  }

  void LoadXML(const openwow::ui::xml::XMLFrameDef &frame_def,
               openwow::ui::xml::ErrorContext *error_handler = nullptr);

  bool SetTexture(const std::string &path) {
    if (path.empty()) {
      BindTexture({}, {}, false);
      return true;
    }
    auto texture = openwow::render::TextureAsset::File(path);
    if (!texture) {
      return false;
    }
    BindTexture(std::move(texture), path, false);
    return true;
  }
  [[nodiscard]] const std::string &GetTexture() const noexcept {
    return texturePath_;
  }

  [[nodiscard]] const openwow::render::TextureAssetPtr& texture_asset() const noexcept {
    return texture_asset_;
  }

  [[nodiscard]] bool IsTextureReady() const noexcept {
    return !texture_asset_ || texture_asset_->ready();
  }

  [[nodiscard]] bool HasRenderableContent() const noexcept {
    return texture_asset_ != nullptr;
  }

  void RegisterRenderCallbacks(SimpleRenderBatchSink &sink) const override {
    if (!texture_asset_) {
      return;
    }

    sink.AddEmbeddedTexture(*this);
  }

  void ApplyAnimRotation(FramePoint anchorPoint, const float* originOffset,
                         float radians) override {
    if (originOffset != nullptr) {
      RotateVertexQuad(anchorPoint, originOffset[0], originOffset[1], radians);
    }
  }

  [[nodiscard]] bool GetFilePath(std::string &outPath) const {
    if (!texture_asset_ || texture_asset_->name().empty())
      return false;
    outPath = texture_asset_->name();
    return true;
  }

  bool GetFilePath(char *buffer, size_t bufferSize) const {
    std::string path;
    if (!buffer || bufferSize == 0 || !GetFilePath(path))
      return false;
    size_t copyLen = std::min(path.size(), bufferSize - 1);
    std::memcpy(buffer, path.c_str(), copyLen);
    buffer[copyLen] = '\0';
    return true;
  }

  void SetColorTexture(float r, float g, float b, float a = 1.0f) noexcept {
    colorR_ = r;
    colorG_ = g;
    colorB_ = b;
    colorA_ = a;

    const std::uint32_t packedColor = PackSolidTextureColor(r, g, b, a);
    BindTexture(openwow::render::TextureAsset::Solid(packedColor), {}, true);
  }
  [[nodiscard]] bool IsSolidColor() const noexcept {
    return isSolidColor_;
  }

  void SetVertexColor(float r, float g, float b, float a = 1.0f) noexcept {
    vertR_ = r;
    vertG_ = g;
    vertB_ = b;
    vertA_ = a;
  }
  void GetVertexColor(float &r, float &g, float &b, float &a) const noexcept {
    r = vertR_;
    g = vertG_;
    b = vertB_;
    a = vertA_;
  }

  void SetGradient(TextureGradientOrientation orientation,
                   TextureGradientColor min_color,
                   TextureGradientColor max_color) noexcept {
    hasGradient_ = true;
    gradientOrientation_ = orientation;
    gradientMinColor_ = min_color;
    gradientMaxColor_ = max_color;
    QueueOwningFrameDrawLayerStateUpdateIfVisible();
  }
  void ClearGradient() noexcept {
    hasGradient_ = false;
    QueueOwningFrameDrawLayerStateUpdateIfVisible();
  }
  [[nodiscard]] bool HasGradient() const noexcept {
    return hasGradient_;
  }
  [[nodiscard]] TextureGradientOrientation GetGradientOrientation() const noexcept {
    return gradientOrientation_;
  }
  [[nodiscard]] const TextureGradientColor &GetGradientMinColor() const noexcept {
    return gradientMinColor_;
  }
  [[nodiscard]] const TextureGradientColor &GetGradientMaxColor() const noexcept {
    return gradientMaxColor_;
  }

  void SetTexCoord(float left, float right, float top, float bottom) noexcept {
    SetTexCoordQuad(TextureCoordQuad::FromRect(left, right, top, bottom));
  }
  void GetTexCoord(float &left, float &right, float &top, float &bottom) const noexcept {
    left = texCoordQuad_.upperLeft.u;
    right = texCoordQuad_.upperRight.u;
    top = texCoordQuad_.upperLeft.v;
    bottom = texCoordQuad_.lowerLeft.v;
  }

  void SetTexCoordQuad(const TextureCoordQuad &texCoordQuad) noexcept {
    texCoordQuad_ = texCoordQuad;
    QueueOwningFrameDrawLayerStateUpdateIfVisible();
  }

  [[nodiscard]] const TextureCoordQuad &GetTexCoordQuad() const noexcept {
    return texCoordQuad_;
  }

  [[nodiscard]] const VertexQuad &GetActiveVertexQuad() const noexcept {
    return activeVertexQuad_;
  }

  [[nodiscard]] const VertexQuad &GetReferenceVertexQuad() const noexcept {
    return referenceVertexQuad_;
  }

  [[nodiscard]] bool HasActiveVertexTransform() const noexcept {
    return hasActiveVertexTransform_;
  }

  void EnableVertexTransformMode() noexcept {
    hasActiveVertexTransform_ = true;
    RebuildReferenceVertexQuadFromRect();
    SyncActiveVertexQuadToReference();
    QueueOwningFrameDrawLayerStateUpdateIfVisible();
  }

  void DisableVertexTransformMode() noexcept {
    hasActiveVertexTransform_ = false;
    RebuildActiveVertexQuadFromRect();
    QueueOwningFrameDrawLayerStateUpdateIfVisible();
  }

  void OffsetVertexQuad(float offsetX, float offsetY) noexcept {
    activeVertexQuad_.OffsetAllPoints(offsetX, offsetY);
    QueueOwningFrameDrawLayerStateUpdateIfVisible();
  }

  void RotateVertexQuad(FramePoint anchorPoint, float offsetX, float offsetY,
                        float radians) noexcept {
    activeVertexQuad_.RotateAllPointsAround(
        activeVertexQuad_.ResolveTransformOrigin(anchorPoint, {offsetX, offsetY}), radians);
    QueueOwningFrameDrawLayerStateUpdateIfVisible();
  }

  void ScaleVertexQuadTowardAnchor(FramePoint anchorPoint, float offsetX, float offsetY,
                                   float scaleX, float scaleY) noexcept {
    activeVertexQuad_.ScaleAllPointsToward(
        activeVertexQuad_.ResolveTransformOrigin(anchorPoint, {offsetX, offsetY}), scaleX, scaleY);
    QueueOwningFrameDrawLayerStateUpdateIfVisible();
  }

  void SetBlendMode(BlendMode mode) noexcept {
    if (blendMode_ == mode) {
      return;
    }

    blendMode_ = mode;
    QueueOwningFrameDrawLayerStateUpdateIfVisible();
  }
  [[nodiscard]] BlendMode GetBlendMode() const noexcept {
    return blendMode_;
  }

  void SetRotation(float radians) noexcept {
    rotation_ = radians;
  }
  [[nodiscard]] float GetRotation() const noexcept {
    return rotation_;
  }

  void SetDesaturated(bool desat) noexcept {
    if (desaturated_ == desat) {
      return;
    }
    desaturated_ = desat;
    QueueOwningFrameDrawLayerStateUpdateIfVisible();
  }
  [[nodiscard]] bool IsDesaturated() const noexcept {
    return desaturated_;
  }

  void SetNonBlocking(bool nb) noexcept {
    nonBlocking_ = nb;
  }
  [[nodiscard]] bool IsNonBlocking() const noexcept {
    return nonBlocking_;
  }

  void SetHorizTile(bool tile) noexcept {
    horizTile_ = tile;
  }
  [[nodiscard]] bool GetHorizTile() const noexcept {
    return horizTile_;
  }

  void SetVertTile(bool tile) noexcept {
    vertTile_ = tile;
  }
  [[nodiscard]] bool GetVertTile() const noexcept {
    return vertTile_;
  }

  [[nodiscard]] bool IsGeneratedTexCoordDirty() const noexcept {
    return generatedTexCoordsDirty_;
  }

  void ClearGeneratedTexCoordDirty() noexcept {
    generatedTexCoordsDirty_ = false;
  }

  void OnLayout() override {
    const VertexQuad previousActive = activeVertexQuad_;
    const VertexQuad previousReference = referenceVertexQuad_;
    const VertexQuad rebuiltReference = VertexQuad::FromRect(GetRect(), vertexQuadDepth_);

    if (hasActiveVertexTransform_) {
      activeVertexQuad_ = rebuiltReference;
      activeVertexQuad_.AddDeltaFrom(previousReference, previousActive);
      referenceVertexQuad_ = rebuiltReference;
    } else {
      activeVertexQuad_ = rebuiltReference;
    }

    if (horizTile_ || vertTile_) {
      generatedTexCoordsDirty_ = true;
      QueueOwningFrameDrawLayerStateUpdateIfVisible();
    }

    NotifyOwningScrollFrameOfContentChangeIfVisible();
  }

private:
  bool LoadXMLWithInheritance(
      const openwow::ui::xml::XMLFrameDef &frame_def,
      openwow::ui::xml::ErrorContext *error_handler,
      std::unordered_set<std::string> *inheritance_stack);

  [[nodiscard]] static std::uint32_t PackSolidTextureColor(const float r, const float g,
                                                           const float b, const float a) noexcept {
    const auto toByte = [](const float channel) -> std::uint32_t {
      return static_cast<std::uint32_t>(static_cast<std::uint8_t>(
          std::clamp(channel, 0.0f, 1.0f) * 255.0f));
    };

    return toByte(r) | (toByte(g) << 8) | (toByte(b) << 16) | (toByte(a) << 24);
  }

  void BindTexture(openwow::render::TextureAssetPtr texture,
                   std::string texturePath,
                   const bool solidColor) {
    texture_asset_ = std::move(texture);
    texturePath_ = std::move(texturePath);
    isSolidColor_ = solidColor;
    QueueOwningFrameDrawLayerStateUpdateIfVisible();
  }

  void RebuildReferenceVertexQuadFromRect() noexcept {
    referenceVertexQuad_ = VertexQuad::FromRect(GetRect(), vertexQuadDepth_);
  }

  void RebuildActiveVertexQuadFromRect() noexcept {
    activeVertexQuad_ = VertexQuad::FromRect(GetRect(), vertexQuadDepth_);
  }

  void SyncActiveVertexQuadToReference() noexcept {
    activeVertexQuad_ = referenceVertexQuad_;
  }

  std::string texturePath_;
  openwow::render::TextureAssetPtr texture_asset_;
  bool isSolidColor_{false};
  float colorR_{1.0f}, colorG_{1.0f}, colorB_{1.0f}, colorA_{1.0f};
  float vertR_{1.0f}, vertG_{1.0f}, vertB_{1.0f}, vertA_{1.0f};
  TextureCoordQuad texCoordQuad_{};

  VertexQuad activeVertexQuad_{};
  VertexQuad referenceVertexQuad_{};
  float vertexQuadDepth_{0.0f};
  BlendMode blendMode_{BlendMode::Blend};
  float rotation_{0.0f};
  bool desaturated_{false};
  bool nonBlocking_{false};
  bool horizTile_{false};
  bool vertTile_{false};
  bool hasActiveVertexTransform_{false};
  bool generatedTexCoordsDirty_{false};
  bool hasGradient_{false};
  TextureGradientOrientation gradientOrientation_{TextureGradientOrientation::Horizontal};
  TextureGradientColor gradientMinColor_{};
  TextureGradientColor gradientMaxColor_{};
};

}
