
#include "openwow/game/dressing_room.h"

#include <algorithm>

namespace openwow::game {

DressingRoom& DressingRoom::Get() {
    static DressingRoom instance;
    return instance;
}

void DressingRoom::Open() {
    std::lock_guard lock(mutex_);
    open_ = true;
}

void DressingRoom::Close() {
    std::lock_guard lock(mutex_);
    open_ = false;
}

bool DressingRoom::IsOpen() const {
    std::lock_guard lock(mutex_);
    return open_;
}

void DressingRoom::SetModelUnit(ObjectGuid guid) {
    std::lock_guard lock(mutex_);
    model_unit_ = guid;
}

ObjectGuid DressingRoom::GetModelUnit() const {
    std::lock_guard lock(mutex_);
    return model_unit_;
}

void DressingRoom::EquipItem(std::uint32_t slot_id, std::uint32_t item_id) {
    std::lock_guard lock(mutex_);
    if (slot_id < kNumPreviewSlots) {
        preview_items_[slot_id] = item_id;
    }
}

void DressingRoom::UnequipItem(std::uint32_t slot_id) {
    std::lock_guard lock(mutex_);
    if (slot_id < kNumPreviewSlots) {
        preview_items_[slot_id] = 0;
    }
}

std::uint32_t DressingRoom::GetPreviewItem(std::uint32_t slot_id) const {
    std::lock_guard lock(mutex_);
    if (slot_id >= kNumPreviewSlots) return 0;
    return preview_items_[slot_id];
}

void DressingRoom::ResetToEquipped() {
    std::lock_guard lock(mutex_);
    preview_items_ = actual_items_;
}

void DressingRoom::Undress() {
    std::lock_guard lock(mutex_);
    preview_items_.fill(0);
}

bool DressingRoom::IsUndressed() const {
    std::lock_guard lock(mutex_);
    return std::all_of(preview_items_.begin(), preview_items_.end(),
                       [](std::uint32_t v) { return v == 0; });
}

bool DressingRoom::HasChanges() const {
    std::lock_guard lock(mutex_);
    return preview_items_ != actual_items_;
}

void DressingRoom::ForEachPreviewItem(
    const std::function<void(std::uint32_t slot, std::uint32_t item)>& fn) const {
    std::lock_guard lock(mutex_);
    for (std::uint32_t i = 0; i < kNumPreviewSlots; ++i) {
        fn(i, preview_items_[i]);
    }
}

void DressingRoom::SetBackground(std::uint32_t bg_id) {
    std::lock_guard lock(mutex_);
    background_id_ = bg_id;
}

std::uint32_t DressingRoom::GetBackground() const {
    std::lock_guard lock(mutex_);
    return background_id_;
}

bool DressingRoom::CanPreviewItem(std::uint32_t item_id) {

    return item_id != 0;
}

void DressingRoom::SetActualEquipped(std::uint32_t slot_id, std::uint32_t item_id) {
    std::lock_guard lock(mutex_);
    if (slot_id < kNumPreviewSlots) {
        actual_items_[slot_id] = item_id;
    }
}

void DressingRoom::Reset() {
    std::lock_guard lock(mutex_);
    open_ = false;
    model_unit_ = ObjectGuid{};
    background_id_ = 0;
    preview_items_.fill(0);
    actual_items_.fill(0);
}

}
