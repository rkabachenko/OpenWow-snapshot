
#include "openwow/game/model_frame_data.h"

namespace openwow::game {

ModelFrameData& ModelFrameData::Get() {
    static ModelFrameData instance;
    return instance;
}

std::uint32_t ModelFrameData::CreateFrame(const std::string& name) {
    std::lock_guard lock(mutex_);
    std::uint32_t id = next_id_++;
    frames_[id].name = name;
    return id;
}

void ModelFrameData::DestroyFrame(std::uint32_t frame_id) {
    std::lock_guard lock(mutex_);
    frames_.erase(frame_id);
}

std::uint32_t ModelFrameData::GetNumActiveFrames() const {
    std::lock_guard lock(mutex_);
    return static_cast<std::uint32_t>(frames_.size());
}

void ModelFrameData::ForEachFrame(
    const std::function<void(std::uint32_t id, const std::string& name)>& fn) const {
    std::lock_guard lock(mutex_);
    for (const auto& [id, state] : frames_) {
        fn(id, state.name);
    }
}

void ModelFrameData::SetDisplayInfo(std::uint32_t frame_id,
                                    const ModelDisplayInfo& info) {
    std::lock_guard lock(mutex_);
    auto it = frames_.find(frame_id);
    if (it != frames_.end()) {
        it->second.display_info = info;
    }
}

std::optional<ModelDisplayInfo> ModelFrameData::GetDisplayInfo(
    std::uint32_t frame_id) const {
    std::lock_guard lock(mutex_);
    auto it = frames_.find(frame_id);
    if (it == frames_.end()) return std::nullopt;
    return it->second.display_info;
}

void ModelFrameData::SetRotation(std::uint32_t frame_id, float yaw, float pitch) {
    std::lock_guard lock(mutex_);
    auto it = frames_.find(frame_id);
    if (it != frames_.end()) {
        it->second.yaw = yaw;
        it->second.pitch = pitch;
    }
}

std::pair<float, float> ModelFrameData::GetRotation(std::uint32_t frame_id) const {
    std::lock_guard lock(mutex_);
    auto it = frames_.find(frame_id);
    if (it == frames_.end()) return {0.0f, 0.0f};
    return {it->second.yaw, it->second.pitch};
}

void ModelFrameData::SetZoom(std::uint32_t frame_id, float zoom) {
    std::lock_guard lock(mutex_);
    auto it = frames_.find(frame_id);
    if (it != frames_.end()) {
        it->second.zoom = zoom;
    }
}

float ModelFrameData::GetZoom(std::uint32_t frame_id) const {
    std::lock_guard lock(mutex_);
    auto it = frames_.find(frame_id);
    if (it == frames_.end()) return 1.0f;
    return it->second.zoom;
}

void ModelFrameData::SetAnimation(std::uint32_t frame_id, std::uint32_t anim_id) {
    std::lock_guard lock(mutex_);
    auto it = frames_.find(frame_id);
    if (it != frames_.end()) {
        it->second.anim_id = anim_id;
    }
}

std::uint32_t ModelFrameData::GetAnimation(std::uint32_t frame_id) const {
    std::lock_guard lock(mutex_);
    auto it = frames_.find(frame_id);
    if (it == frames_.end()) return 0;
    return it->second.anim_id;
}

void ModelFrameData::SetAutoRotate(std::uint32_t frame_id, bool enabled) {
    std::lock_guard lock(mutex_);
    auto it = frames_.find(frame_id);
    if (it != frames_.end()) {
        it->second.auto_rotate = enabled;
    }
}

bool ModelFrameData::GetAutoRotate(std::uint32_t frame_id) const {
    std::lock_guard lock(mutex_);
    auto it = frames_.find(frame_id);
    if (it == frames_.end()) return false;
    return it->second.auto_rotate;
}

void ModelFrameData::SetPaused(std::uint32_t frame_id, bool paused) {
    std::lock_guard lock(mutex_);
    auto it = frames_.find(frame_id);
    if (it != frames_.end()) {
        it->second.paused = paused;
    }
}

bool ModelFrameData::IsPaused(std::uint32_t frame_id) const {
    std::lock_guard lock(mutex_);
    auto it = frames_.find(frame_id);
    if (it == frames_.end()) return false;
    return it->second.paused;
}

void ModelFrameData::Reset() {
    std::lock_guard lock(mutex_);
    frames_.clear();
    next_id_ = 1;
}

}
