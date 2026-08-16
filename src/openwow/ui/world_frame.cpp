
#include "openwow/ui/world_frame.h"

#include "openwow/foundation/math/vec3_cross.h"
#include "openwow/foundation/math/vec3_negate.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace openwow::ui {

namespace {
constexpr float kPi = 3.14159265358979323846f;

float DegToRad(float deg) { return deg * (kPi / 180.0f); }

void Vec3Sub(const float* a, const float* b, float* out) {
    out[0] = a[0] - b[0];
    out[1] = a[1] - b[1];
    out[2] = a[2] - b[2];
}

float Vec3Dot(const float* a, const float* b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

float Vec3Len(const float* v) {
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

void Vec3Normalize(float* v) {
    float len = Vec3Len(v);
    if (len > 1e-8f) {
        v[0] /= len;
        v[1] /= len;
        v[2] /= len;
    }
}
}

WorldFrameManager& WorldFrameManager::Get() {
    static WorldFrameManager instance;
    return instance;
}

void WorldFrameManager::SetViewport(int x, int y, int width, int height) {
    std::lock_guard lock(mutex_);
    vp_x_ = x;
    vp_y_ = y;
    vp_w_ = std::max(1, width);
    vp_h_ = std::max(1, height);
    aspect_ = static_cast<float>(vp_w_) / static_cast<float>(vp_h_);
    proj_dirty_ = true;
}

Viewport WorldFrameManager::GetViewport() const {
    std::lock_guard lock(mutex_);
    return {vp_x_, vp_y_, vp_w_, vp_h_};
}

void WorldFrameManager::SetFieldOfView(float fovDegrees) {
    std::lock_guard lock(mutex_);
    fov_deg_ = std::clamp(fovDegrees, 1.0f, 179.0f);
    proj_dirty_ = true;
}

float WorldFrameManager::GetFieldOfView() const {
    std::lock_guard lock(mutex_);
    return fov_deg_;
}

void WorldFrameManager::SetAspectRatio(float ratio) {
    std::lock_guard lock(mutex_);
    aspect_ = std::max(0.01f, ratio);
    proj_dirty_ = true;
}

float WorldFrameManager::GetAspectRatio() const {
    std::lock_guard lock(mutex_);
    return aspect_;
}

void WorldFrameManager::SetNearClip(float n) {
    std::lock_guard lock(mutex_);
    near_ = std::max(0.001f, n);
    proj_dirty_ = true;
}

float WorldFrameManager::GetNearClip() const {
    std::lock_guard lock(mutex_);
    return near_;
}

void WorldFrameManager::SetFarClip(float f) {
    std::lock_guard lock(mutex_);
    far_ = std::max(near_ + 0.1f, f);
    proj_dirty_ = true;
}

float WorldFrameManager::GetFarClip() const {
    std::lock_guard lock(mutex_);
    return far_;
}

void WorldFrameManager::SetCameraPosition(float x, float y, float z) {
    std::lock_guard lock(mutex_);
    cam_pos_[0] = x;
    cam_pos_[1] = y;
    cam_pos_[2] = z;
    view_dirty_ = true;
}

void WorldFrameManager::SetCameraTarget(float x, float y, float z) {
    std::lock_guard lock(mutex_);
    cam_target_[0] = x;
    cam_target_[1] = y;
    cam_target_[2] = z;
    view_dirty_ = true;
}

void WorldFrameManager::SetCameraUp(float x, float y, float z) {
    std::lock_guard lock(mutex_);
    cam_up_[0] = x;
    cam_up_[1] = y;
    cam_up_[2] = z;
    view_dirty_ = true;
}

void WorldFrameManager::RebuildProjection() const {

    float fov = DegToRad(fov_deg_);
    float f = 1.0f / std::tan(fov * 0.5f);
    float range_inv = 1.0f / (near_ - far_);

    std::memset(proj_mat_.data(), 0, 16 * sizeof(float));
    proj_mat_[0]  = f / aspect_;
    proj_mat_[5]  = f;
    proj_mat_[10] = (far_ + near_) * range_inv;
    proj_mat_[11] = -1.0f;
    proj_mat_[14] = 2.0f * far_ * near_ * range_inv;

    proj_dirty_ = false;
}

void WorldFrameManager::RebuildView() const {

    float forward[3], right[3], up[3];
    Vec3Sub(cam_target_, cam_pos_, forward);
    Vec3Normalize(forward);
    openwow::math::vec3::Cross(right, forward, cam_up_);
    Vec3Normalize(right);
    openwow::math::vec3::Cross(up, right, forward);

    std::memset(view_mat_.data(), 0, 16 * sizeof(float));
    view_mat_[0]  = right[0];
    view_mat_[1]  = right[1];
    view_mat_[2]  = right[2];
    view_mat_[3]  = -Vec3Dot(right, cam_pos_);

    view_mat_[4]  = up[0];
    view_mat_[5]  = up[1];
    view_mat_[6]  = up[2];
    view_mat_[7]  = -Vec3Dot(up, cam_pos_);

    openwow::math::vec3::CopyNegated(view_mat_.data() + 8, forward);
    view_mat_[11] = Vec3Dot(forward, cam_pos_);

    view_mat_[12] = 0.0f;
    view_mat_[13] = 0.0f;
    view_mat_[14] = 0.0f;
    view_mat_[15] = 1.0f;

    view_dirty_ = false;
}

std::array<float, 16> WorldFrameManager::GetProjectionMatrix() const {
    std::lock_guard lock(mutex_);
    if (proj_dirty_) RebuildProjection();
    return proj_mat_;
}

std::array<float, 16> WorldFrameManager::GetViewMatrix() const {
    std::lock_guard lock(mutex_);
    if (view_dirty_) RebuildView();
    return view_mat_;
}

ScreenPoint WorldFrameManager::WorldToScreen(float wx, float wy, float wz) const {
    std::lock_guard lock(mutex_);
    if (proj_dirty_) RebuildProjection();
    if (view_dirty_) RebuildView();

    float vp[16];
    MatMul4x4(proj_mat_.data(), view_mat_.data(), vp);

    float cx = vp[0] * wx + vp[1] * wy + vp[2] * wz + vp[3];
    float cy = vp[4] * wx + vp[5] * wy + vp[6] * wz + vp[7];

    float cw = vp[12] * wx + vp[13] * wy + vp[14] * wz + vp[15];

    if (std::abs(cw) < 1e-8f) return {0.0f, 0.0f, false};

    float ndcX = cx / cw;
    float ndcY = cy / cw;

    float sx = (ndcX * 0.5f + 0.5f) * static_cast<float>(vp_w_) + static_cast<float>(vp_x_);
    float sy = (1.0f - (ndcY * 0.5f + 0.5f)) * static_cast<float>(vp_h_) + static_cast<float>(vp_y_);

    bool onScreen = (ndcX >= -1.0f && ndcX <= 1.0f &&
                     ndcY >= -1.0f && ndcY <= 1.0f &&
                     cw > 0.0f);
    return {sx, sy, onScreen};
}

WorldRayResult WorldFrameManager::ScreenToWorldRay(float sx, float sy) const {
    std::lock_guard lock(mutex_);

    const float normalized_x =
        (sx - static_cast<float>(vp_x_)) / static_cast<float>(vp_w_);
    const float normalized_y =
        1.0f - ((sy - static_cast<float>(vp_y_)) / static_cast<float>(vp_h_));
    if (normalized_x < 0.0f || normalized_x > 1.0f ||
        normalized_y < 0.0f || normalized_y > 1.0f) {
        return {cam_pos_[0], cam_pos_[1], cam_pos_[2], 0.0f, 0.0f, -1.0f};
    }

    float forward[3];
    Vec3Sub(cam_target_, cam_pos_, forward);
    Vec3Normalize(forward);

    float right[3];
    openwow::math::vec3::Cross(right, forward, cam_up_);
    Vec3Normalize(right);

    float up[3];
    openwow::math::vec3::Cross(up, right, forward);

    const float half_height = std::tan(DegToRad(fov_deg_) * 0.5f);
    const float horizontal = (normalized_x * 2.0f - 1.0f) *
                             half_height * aspect_;
    const float vertical = (normalized_y * 2.0f - 1.0f) * half_height;
    float direction[3] = {
        forward[0] + horizontal * right[0] + vertical * up[0],
        forward[1] + horizontal * right[1] + vertical * up[1],
        forward[2] + horizontal * right[2] + vertical * up[2],
    };
    Vec3Normalize(direction);

    return {
        cam_pos_[0] + near_ * direction[0],
        cam_pos_[1] + near_ * direction[1],
        cam_pos_[2] + near_ * direction[2],
        direction[0],
        direction[1],
        direction[2],
    };
}

bool WorldFrameManager::IsPointOnScreen(float wx, float wy, float wz) const {
    return WorldToScreen(wx, wy, wz).onScreen;
}

void WorldFrameManager::SetFullscreen(bool fs) {
    std::lock_guard lock(mutex_);
    fullscreen_ = fs;
}

bool WorldFrameManager::IsFullscreen() const {
    std::lock_guard lock(mutex_);
    return fullscreen_;
}

void WorldFrameManager::Update([[maybe_unused]] float dt) {

}

void WorldFrameManager::Reset() {
    std::lock_guard lock(mutex_);
    vp_x_ = 0;
    vp_y_ = 0;
    vp_w_ = 1024;
    vp_h_ = 768;
    fov_deg_ = kDefaultFOV;
    aspect_ = 4.0f / 3.0f;
    near_ = kDefaultNear;
    far_ = kDefaultFar;
    cam_pos_[0] = 0.0f;  cam_pos_[1] = 0.0f;  cam_pos_[2] = 10.0f;
    cam_target_[0] = 0.0f; cam_target_[1] = 0.0f; cam_target_[2] = 0.0f;
    cam_up_[0] = 0.0f;  cam_up_[1] = 0.0f;  cam_up_[2] = 1.0f;
    fullscreen_ = false;
    proj_dirty_ = true;
    view_dirty_ = true;
}

void WorldFrameManager::MatMul4x4(const float* a, const float* b, float* out) {
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            out[r * 4 + c] = a[r * 4 + 0] * b[0 * 4 + c] +
                              a[r * 4 + 1] * b[1 * 4 + c] +
                              a[r * 4 + 2] * b[2 * 4 + c] +
                              a[r * 4 + 3] * b[3 * 4 + c];
        }
    }
}

}
