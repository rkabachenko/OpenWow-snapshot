
#include "openwow/game/model_preview.h"

namespace openwow::game {

CharacterModelPreview& CharacterModelPreview::Get() {
    static CharacterModelPreview instance;
    return instance;
}

void CharacterModelPreview::SetRace(uint32_t raceId) {
    std::lock_guard lock(mutex_);
    race_ = raceId;
    dirty_ = true;
}

uint32_t CharacterModelPreview::GetRace() const {
    std::lock_guard lock(mutex_);
    return race_;
}

void CharacterModelPreview::SetGender(uint32_t genderId) {
    std::lock_guard lock(mutex_);
    gender_ = genderId;
    dirty_ = true;
}

uint32_t CharacterModelPreview::GetGender() const {
    std::lock_guard lock(mutex_);
    return gender_;
}

void CharacterModelPreview::SetSkinColor(uint32_t v) {
    std::lock_guard lock(mutex_);
    skin_ = v;
    dirty_ = true;
}

uint32_t CharacterModelPreview::GetSkinColor() const {
    std::lock_guard lock(mutex_);
    return skin_;
}

void CharacterModelPreview::SetFace(uint32_t v) {
    std::lock_guard lock(mutex_);
    face_ = v;
    dirty_ = true;
}

uint32_t CharacterModelPreview::GetFace() const {
    std::lock_guard lock(mutex_);
    return face_;
}

void CharacterModelPreview::SetHairStyle(uint32_t v) {
    std::lock_guard lock(mutex_);
    hair_style_ = v;
    dirty_ = true;
}

uint32_t CharacterModelPreview::GetHairStyle() const {
    std::lock_guard lock(mutex_);
    return hair_style_;
}

void CharacterModelPreview::SetHairColor(uint32_t v) {
    std::lock_guard lock(mutex_);
    hair_color_ = v;
    dirty_ = true;
}

uint32_t CharacterModelPreview::GetHairColor() const {
    std::lock_guard lock(mutex_);
    return hair_color_;
}

void CharacterModelPreview::SetFacialHair(uint32_t v) {
    std::lock_guard lock(mutex_);
    facial_hair_ = v;
    dirty_ = true;
}

uint32_t CharacterModelPreview::GetFacialHair() const {
    std::lock_guard lock(mutex_);
    return facial_hair_;
}

void CharacterModelPreview::EquipItem(PreviewSlotType slot, uint32_t displayId,
                                      uint32_t enchantVisual) {
    std::lock_guard lock(mutex_);
    auto key = static_cast<uint8_t>(slot);
    equipment_[key] = {slot, displayId, enchantVisual};
    dirty_ = true;
}

void CharacterModelPreview::ClearSlot(PreviewSlotType slot) {
    std::lock_guard lock(mutex_);
    equipment_.erase(static_cast<uint8_t>(slot));
    dirty_ = true;
}

void CharacterModelPreview::ClearAllSlots() {
    std::lock_guard lock(mutex_);
    equipment_.clear();
    dirty_ = true;
}

std::vector<PreviewEquipItem> CharacterModelPreview::GetEquippedItems() const {
    std::lock_guard lock(mutex_);
    std::vector<PreviewEquipItem> result;
    result.reserve(equipment_.size());
    for (const auto& [key, item] : equipment_) {
        result.push_back(item);
    }
    return result;
}

std::optional<PreviewEquipItem> CharacterModelPreview::GetItem(
    PreviewSlotType slot) const {
    std::lock_guard lock(mutex_);
    auto it = equipment_.find(static_cast<uint8_t>(slot));
    if (it != equipment_.end()) return it->second;
    return std::nullopt;
}

void CharacterModelPreview::SetRotation(float yaw, float pitch) {
    std::lock_guard lock(mutex_);
    yaw_ = yaw;
    pitch_ = pitch;
    dirty_ = true;
}

ModelPreviewRotation CharacterModelPreview::GetRotation() const {
    std::lock_guard lock(mutex_);
    return {yaw_, pitch_};
}

void CharacterModelPreview::Rotate(float deltaYaw, float deltaPitch) {
    std::lock_guard lock(mutex_);
    yaw_ += deltaYaw;
    pitch_ += deltaPitch;
    dirty_ = true;
}

void CharacterModelPreview::SetZoom(float zoom) {
    std::lock_guard lock(mutex_);
    zoom_ = zoom;
    dirty_ = true;
}

float CharacterModelPreview::GetZoom() const {
    std::lock_guard lock(mutex_);
    return zoom_;
}

void CharacterModelPreview::SetAnimation(uint32_t animId) {
    std::lock_guard lock(mutex_);
    anim_id_ = animId;
    dirty_ = true;
}

uint32_t CharacterModelPreview::GetAnimation() const {
    std::lock_guard lock(mutex_);
    return anim_id_;
}

void CharacterModelPreview::SetUndress(bool undressed) {
    std::lock_guard lock(mutex_);
    undressed_ = undressed;
    dirty_ = true;
}

bool CharacterModelPreview::IsUndressed() const {
    std::lock_guard lock(mutex_);
    return undressed_;
}

bool CharacterModelPreview::IsDirty() const {
    std::lock_guard lock(mutex_);
    return dirty_;
}

void CharacterModelPreview::MarkClean() {
    std::lock_guard lock(mutex_);
    dirty_ = false;
}

void CharacterModelPreview::Reset() {
    std::lock_guard lock(mutex_);
    race_ = 0;
    gender_ = 0;
    skin_ = 0;
    face_ = 0;
    hair_style_ = 0;
    hair_color_ = 0;
    facial_hair_ = 0;
    equipment_.clear();
    yaw_ = 0.0f;
    pitch_ = 0.0f;
    zoom_ = 1.0f;
    anim_id_ = 0;
    undressed_ = false;
    dirty_ = false;
}

}
