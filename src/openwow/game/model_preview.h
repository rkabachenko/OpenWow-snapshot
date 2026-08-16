
#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::game {

enum class PreviewSlotType : uint8_t {
    Head      = 0,
    Shoulder  = 1,
    Chest     = 2,
    Waist     = 3,
    Legs      = 4,
    Feet      = 5,
    Wrist     = 6,
    Hands     = 7,
    Back      = 8,
    MainHand  = 9,
    OffHand   = 10,
    Tabard    = 11,
    Shirt     = 12,
};

struct PreviewEquipItem {
    PreviewSlotType slotType{};
    uint32_t itemDisplayId    = 0;
    uint32_t enchantVisualId  = 0;
};

struct ModelPreviewRotation {
    float yaw   = 0.0f;
    float pitch  = 0.0f;
};

class CharacterModelPreview {
 public:
  static CharacterModelPreview& Get();

  CharacterModelPreview(const CharacterModelPreview&) = delete;
  CharacterModelPreview& operator=(const CharacterModelPreview&) = delete;

  void SetRace(uint32_t raceId);
  [[nodiscard]] uint32_t GetRace() const;

  void SetGender(uint32_t genderId);
  [[nodiscard]] uint32_t GetGender() const;

  void SetSkinColor(uint32_t v);
  [[nodiscard]] uint32_t GetSkinColor() const;

  void SetFace(uint32_t v);
  [[nodiscard]] uint32_t GetFace() const;

  void SetHairStyle(uint32_t v);
  [[nodiscard]] uint32_t GetHairStyle() const;

  void SetHairColor(uint32_t v);
  [[nodiscard]] uint32_t GetHairColor() const;

  void SetFacialHair(uint32_t v);
  [[nodiscard]] uint32_t GetFacialHair() const;

  void EquipItem(PreviewSlotType slot, uint32_t displayId,
                 uint32_t enchantVisual = 0);
  void ClearSlot(PreviewSlotType slot);
  void ClearAllSlots();
  [[nodiscard]] std::vector<PreviewEquipItem> GetEquippedItems() const;
  [[nodiscard]] std::optional<PreviewEquipItem> GetItem(
      PreviewSlotType slot) const;

  void SetRotation(float yaw, float pitch);
  [[nodiscard]] ModelPreviewRotation GetRotation() const;
  void Rotate(float deltaYaw, float deltaPitch);

  void SetZoom(float zoom);
  [[nodiscard]] float GetZoom() const;

  void SetAnimation(uint32_t animId);
  [[nodiscard]] uint32_t GetAnimation() const;

  void SetUndress(bool undressed);
  [[nodiscard]] bool IsUndressed() const;

  [[nodiscard]] bool IsDirty() const;
  void MarkClean();

  void Reset();

 private:
  CharacterModelPreview() = default;

  uint32_t race_     = 0;
  uint32_t gender_   = 0;
  uint32_t skin_     = 0;
  uint32_t face_     = 0;
  uint32_t hair_style_ = 0;
  uint32_t hair_color_ = 0;
  uint32_t facial_hair_ = 0;

  std::unordered_map<uint8_t, PreviewEquipItem> equipment_;

  float yaw_   = 0.0f;
  float pitch_  = 0.0f;
  float zoom_   = 1.0f;

  uint32_t anim_id_ = 0;
  bool undressed_    = false;
  bool dirty_        = false;

  mutable std::mutex mutex_;
};

}
