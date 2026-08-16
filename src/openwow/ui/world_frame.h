
#pragma once

#include <array>
#include <cstdint>
#include <mutex>

namespace openwow::ui {

struct Viewport {
    int x{0};
    int y{0};
    int w{1024};
    int h{768};
};

struct ScreenPoint {
    float sx{0.0f};
    float sy{0.0f};
    bool onScreen{false};
};

struct WorldRayResult {
    float ox{0.0f}, oy{0.0f}, oz{0.0f};
    float dx{0.0f}, dy{0.0f}, dz{0.0f};
};

class WorldFrameManager {
 public:
    static WorldFrameManager& Get();

    void SetViewport(int x, int y, int width, int height);
    [[nodiscard]] Viewport GetViewport() const;

    void SetFieldOfView(float fovDegrees);
    [[nodiscard]] float GetFieldOfView() const;
    static constexpr float kDefaultFOV = 90.0f;

    void SetAspectRatio(float ratio);
    [[nodiscard]] float GetAspectRatio() const;

    void SetNearClip(float near);
    [[nodiscard]] float GetNearClip() const;
    static constexpr float kDefaultNear = 0.2f;

    void SetFarClip(float far);
    [[nodiscard]] float GetFarClip() const;
    static constexpr float kDefaultFar = 1600.0f;

    void SetCameraPosition(float x, float y, float z);
    void SetCameraTarget(float x, float y, float z);
    void SetCameraUp(float x, float y, float z);

    [[nodiscard]] std::array<float, 16> GetProjectionMatrix() const;

    [[nodiscard]] std::array<float, 16> GetViewMatrix() const;

    [[nodiscard]] ScreenPoint WorldToScreen(float wx, float wy, float wz) const;

    [[nodiscard]] WorldRayResult ScreenToWorldRay(float sx, float sy) const;

    [[nodiscard]] bool IsPointOnScreen(float wx, float wy, float wz) const;

    void SetFullscreen(bool fs);
    [[nodiscard]] bool IsFullscreen() const;

    void Update(float dt);

    void Reset();

 private:
    WorldFrameManager() = default;

    void RebuildProjection() const;
    void RebuildView() const;
    static void MatMul4x4(const float* a, const float* b, float* out);

    int vp_x_{0}, vp_y_{0}, vp_w_{1024}, vp_h_{768};

    float fov_deg_{kDefaultFOV};
    float aspect_{4.0f / 3.0f};
    float near_{kDefaultNear};
    float far_{kDefaultFar};

    float cam_pos_[3]{0.0f, 0.0f, 10.0f};
    float cam_target_[3]{0.0f, 0.0f, 0.0f};
    float cam_up_[3]{0.0f, 0.0f, 1.0f};

    bool fullscreen_{false};

    mutable bool proj_dirty_{true};
    mutable bool view_dirty_{true};
    mutable std::array<float, 16> proj_mat_{};
    mutable std::array<float, 16> view_mat_{};

    mutable std::mutex mutex_;
};

}
