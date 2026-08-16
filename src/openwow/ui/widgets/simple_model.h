#pragma once

#include "openwow/foundation/math/vec3_normalize_if_length_squared_exceeds_client_epsilon.h"
#include "openwow/ui/widgets/simple_frame.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace openwow::ui::widgets {

class CSimpleModel : public CSimpleFrame {
 public:
  enum class LightBank : std::uint8_t {
    General,
    Character,
    Pet,
    Count,
  };

  struct LightSettings {
    bool enabled{false};
    bool omni{false};
    float dirX{0.0f};
    float dirY{0.0f};
    float dirZ{0.0f};
    float ambientR{1.0f};
    float ambientG{1.0f};
    float ambientB{1.0f};
    float diffuseR{1.0f};
    float diffuseG{1.0f};
    float diffuseB{1.0f};
  };

  static constexpr std::size_t kLightSlotCount = 2;
  static constexpr std::size_t kLightsPerSlot = 4;

  struct LightCollection {
    std::array<LightSettings, kLightsPerSlot> values{};
    std::size_t count{0};
    bool dirty{false};

    [[nodiscard]] bool Add(const LightSettings& light) noexcept {
      if (count == values.size()) {
        return false;
      }
      values[count++] = light;
      dirty = true;
      return true;
    }

    void Reset() noexcept {
      count = 0;
      dirty = false;
    }
  };

  explicit CSimpleModel(ScriptObjectType type = ScriptObjectType::Model)
      : CSimpleFrame(type) {}
  [[nodiscard]] bool IsKindOf(ScriptObjectType t) const noexcept override {
    return t == ScriptObjectType::Model || CSimpleFrame::IsKindOf(t);
  }
  [[nodiscard]] bool IsTypeOf(const char* typeName) const noexcept override {
    return StrCaseEq(typeName, "Model") || CSimpleFrame::IsTypeOf(typeName);
  }

  void SetModel(const std::string& path) { modelPath_ = path; }
  [[nodiscard]] const std::string& GetModel() const noexcept {
    return modelPath_;
  }

  void SetModelScale(float s) noexcept { modelScale_ = s; }
  [[nodiscard]] float GetModelScale() const noexcept { return modelScale_; }

  void SetPosition(float x, float y, float z) noexcept {
    posX_ = x; posY_ = y; posZ_ = z;
  }
  void GetPosition(float& x, float& y, float& z) const noexcept {
    x = posX_; y = posY_; z = posZ_;
  }

  void SetFacing(float facing) noexcept { facing_ = facing; }
  [[nodiscard]] float GetFacing() const noexcept { return facing_; }

  void SetCamera(uint32_t index) noexcept { cameraIndex_ = index; }
  [[nodiscard]] uint32_t GetCamera() const noexcept { return cameraIndex_; }

  void SetLight(bool enabled, bool omni, float dirX, float dirY, float dirZ,
                float ambR, float ambG, float ambB,
                float dirR, float dirG, float dirB) {
    float direction[3] = {dirX, dirY, dirZ};
    if (!omni) {
      openwow::math::vec3::NormalizeInPlaceIfLengthSquaredExceedsClientEpsilon(direction);
    }
    light_ = {
        .enabled = enabled,
        .omni = omni,
        .dirX = direction[0],
        .dirY = direction[1],
        .dirZ = direction[2],
        .ambientR = ambR,
        .ambientG = ambG,
        .ambientB = ambB,
        .diffuseR = dirR,
        .diffuseG = dirG,
        .diffuseB = dirB,
    };
  }
  [[nodiscard]] bool HasLight() const noexcept { return light_.enabled; }
  [[nodiscard]] const LightSettings& GetLightSettings() const noexcept {
    return light_;
  }

  [[nodiscard]] bool AddLight(const LightBank bank, const std::size_t slot,
                              const LightSettings& light) noexcept {
    if (bank == LightBank::Count || slot >= kLightSlotCount) {
      return false;
    }
    return lightBanks_[static_cast<std::size_t>(bank)][slot].Add(light);
  }

  [[nodiscard]] const LightCollection& GetLights(
      const LightBank bank, const std::size_t slot) const noexcept {
    return lightBanks_[static_cast<std::size_t>(bank)][slot];
  }

  void ResetLights() noexcept {
    for (auto& bank : lightBanks_) {
      for (auto& slot : bank) {
        slot.Reset();
      }
    }
  }

  void SetFogColor(float r, float g, float b) noexcept {
    fogR_ = r; fogG_ = g; fogB_ = b;
    fogEnabled_ = true;
  }
  void SetFogNear(float n) noexcept { fogNear_ = n; }
  void SetFogFar(float f) noexcept { fogFar_ = f; }

  void GetFogColor(float& r, float& g, float& b) const noexcept {
    r = fogR_; g = fogG_; b = fogB_;
  }
  [[nodiscard]] float GetFogNear() const noexcept { return fogNear_; }
  [[nodiscard]] float GetFogFar() const noexcept { return fogFar_; }

  void ClearFog() noexcept { fogEnabled_ = false; }
  [[nodiscard]] bool IsFogEnabled() const noexcept { return fogEnabled_; }

  void SetSequence(uint32_t seq) noexcept { sequence_ = seq; }
  [[nodiscard]] uint32_t GetSequence() const noexcept { return sequence_; }

  void SetSequenceTime(uint32_t seq, float time) noexcept {
    sequence_ = seq;
    sequenceTime_ = time;
  }

  void ClearModel() {
    modelPath_.clear();
  }

  [[nodiscard]] virtual bool EnsureModelTexturesLoaded() const noexcept {
    return !modelPath_.empty();
  }

 protected:
  std::string modelPath_;
  float modelScale_{1.0f};
  float posX_{0.0f}, posY_{0.0f}, posZ_{0.0f};
  float facing_{0.0f};
  uint32_t cameraIndex_{0};
  LightSettings light_{};
  std::array<std::array<LightCollection, kLightSlotCount>,
             static_cast<std::size_t>(LightBank::Count)>
      lightBanks_{};
  bool fogEnabled_{false};

  float fogR_{0}, fogG_{0}, fogB_{0}, fogNear_{0}, fogFar_{0};
  uint32_t sequence_{0};
  float sequenceTime_{0.0f};
};

class CSimplePlayerModel : public CSimpleModel {
 public:
  static constexpr LightSettings kDefaultLight{
      .enabled = true,
      .omni = false,
      .dirX = 0.0f,
      .dirY = 1.0f,
      .dirZ = 0.0f,
      .ambientR = 0.69999999f,
      .ambientG = 0.69999999f,
      .ambientB = 0.69999999f,
      .diffuseR = 0.80000001f,
      .diffuseG = 0.80000001f,
      .diffuseB = 0.63999999f,
  };

  CSimplePlayerModel() : CSimpleModel(ScriptObjectType::PlayerModel) {
    const auto& light = kDefaultLight;
    SetLight(light.enabled, light.omni, light.dirX, light.dirY, light.dirZ,
             light.ambientR, light.ambientG, light.ambientB, light.diffuseR,
             light.diffuseG, light.diffuseB);
  }
  [[nodiscard]] bool IsKindOf(ScriptObjectType t) const noexcept override {
    return t == ScriptObjectType::PlayerModel || CSimpleModel::IsKindOf(t);
  }
  [[nodiscard]] bool IsTypeOf(const char* typeName) const noexcept override {
    return StrCaseEq(typeName, "PlayerModel") || CSimpleModel::IsTypeOf(typeName);
  }

  void SetUnit(const std::string& unit) { unit_ = unit; }
  [[nodiscard]] const std::string& GetUnit() const noexcept { return unit_; }

  void SetRotation(float r) noexcept { rotation_ = r; }
  [[nodiscard]] float GetRotation() const noexcept { return rotation_; }

  void SetPortraitZoom(float z) noexcept { portraitZoom_ = z; }
  [[nodiscard]] float GetPortraitZoom() const noexcept { return portraitZoom_; }

  void RefreshUnit() {  }

 private:
  std::string unit_{"player"};
  float rotation_{0.0f};
  float portraitZoom_{0.0f};
};

class CSimpleDressUpModel : public CSimplePlayerModel {
 public:
  enum class WeaponOverrideSlot : uint8_t {
    MainHand = 15,
    OffHand = 16,
  };

  struct WeaponOverrideState {
    uint32_t itemDisplayId{0};
    uint8_t inventoryType{0};
    uint32_t enchantVisual{0};

    [[nodiscard]] bool IsActive() const noexcept { return itemDisplayId != 0; }
  };

  static constexpr std::size_t kPreviewSlotCount = 12;

  CSimpleDressUpModel() { type_ = ScriptObjectType::DressUpModel; }
  ~CSimpleDressUpModel() override { previewState_.reset(); }
  [[nodiscard]] bool IsKindOf(ScriptObjectType t) const noexcept override {
    return t == ScriptObjectType::DressUpModel ||
           CSimplePlayerModel::IsKindOf(t);
  }
  [[nodiscard]] bool IsTypeOf(const char* typeName) const noexcept override {
    return StrCaseEq(typeName, "DressUpModel") || CSimplePlayerModel::IsTypeOf(typeName);
  }

  void Dress() {  }
  void Undress() {
    if (!previewState_) {
      return;
    }

    previewState_->previewSlots.fill(0);
    previewState_->tryOnHistory.clear();
    previewState_->mainHandOverride = {};
    previewState_->offHandOverride = {};
  }
  void TryOn(uint32_t itemId) {
    if (itemId != 0) {
      EnsurePreviewState().tryOnHistory.push_back(itemId);
    }
  }

  void SetPreviewSlotItem(std::size_t slot, uint32_t itemDisplayId) {
    if (slot >= kPreviewSlotCount) {
      return;
    }
    if (itemDisplayId == 0 && !previewState_) {
      return;
    }

    EnsurePreviewState().previewSlots[slot] = itemDisplayId;
  }
  [[nodiscard]] uint32_t GetPreviewSlotItem(std::size_t slot) const noexcept {
    if (slot >= kPreviewSlotCount || !previewState_) {
      return 0;
    }

    return previewState_->previewSlots[slot];
  }

  void SetWeaponOverride(WeaponOverrideSlot slot,
                         uint32_t itemDisplayId,
                         uint8_t inventoryType,
                         uint32_t enchantVisual) {
    if (itemDisplayId == 0 && inventoryType == 0 && enchantVisual == 0) {
      ClearWeaponOverride(slot);
      return;
    }

    auto& preview_state = EnsurePreviewState();
    auto& state = slot == WeaponOverrideSlot::MainHand
                      ? preview_state.mainHandOverride
                      : preview_state.offHandOverride;
    state.itemDisplayId = itemDisplayId;
    state.inventoryType = inventoryType;
    state.enchantVisual = enchantVisual;
  }
  void ClearWeaponOverride(WeaponOverrideSlot slot) noexcept {
    if (!previewState_) {
      return;
    }

    auto& state = slot == WeaponOverrideSlot::MainHand
                      ? previewState_->mainHandOverride
                      : previewState_->offHandOverride;
    state = {};
  }
  [[nodiscard]] bool HasWeaponOverride(WeaponOverrideSlot slot) const noexcept {
    if (!previewState_) {
      return false;
    }

    const auto& state = slot == WeaponOverrideSlot::MainHand
                            ? previewState_->mainHandOverride
                            : previewState_->offHandOverride;
    return state.IsActive();
  }
  [[nodiscard]] const WeaponOverrideState& GetWeaponOverride(
      WeaponOverrideSlot slot) const noexcept {
    if (!previewState_) {
      return EmptyWeaponOverrideState();
    }

    return slot == WeaponOverrideSlot::MainHand
               ? previewState_->mainHandOverride
               : previewState_->offHandOverride;
  }

  [[nodiscard]] const std::vector<uint32_t>& GetTryOnHistory() const noexcept {
    return previewState_ ? previewState_->tryOnHistory : EmptyTryOnHistory();
  }

  [[nodiscard]] bool HasPreviewState() const noexcept {
    return previewState_ != nullptr;
  }

 private:
  struct PreviewState {
    std::array<uint32_t, kPreviewSlotCount> previewSlots{};
    WeaponOverrideState mainHandOverride{};
    WeaponOverrideState offHandOverride{};
    std::vector<uint32_t> tryOnHistory{};
  };

  PreviewState& EnsurePreviewState() {
    if (!previewState_) {
      previewState_ = std::make_unique<PreviewState>();
    }

    return *previewState_;
  }

  static const WeaponOverrideState& EmptyWeaponOverrideState() noexcept {
    static const WeaponOverrideState empty_state{};
    return empty_state;
  }

  static const std::vector<uint32_t>& EmptyTryOnHistory() noexcept {
    static const std::vector<uint32_t> empty_history;
    return empty_history;
  }

  std::unique_ptr<PreviewState> previewState_{};
};

class CSimpleTabardModel : public CSimplePlayerModel {
 public:
  CSimpleTabardModel() { type_ = ScriptObjectType::TabardModel; }
  [[nodiscard]] bool IsKindOf(ScriptObjectType t) const noexcept override {
    return t == ScriptObjectType::TabardModel ||
           CSimplePlayerModel::IsKindOf(t);
  }
  [[nodiscard]] bool IsTypeOf(const char* typeName) const noexcept override {
    return StrCaseEq(typeName, "TabardModel") || CSimplePlayerModel::IsTypeOf(typeName);
  }

  void InitializeTabardColors() {  }

  void SetUpperEmblemTexture(const std::string& tex) { upperEmblem_ = tex; }
  void SetLowerEmblemTexture(const std::string& tex) { lowerEmblem_ = tex; }
  void SetUpperBackgroundTexture(const std::string& tex) { upperBg_ = tex; }
  void SetLowerBackgroundTexture(const std::string& tex) { lowerBg_ = tex; }

 private:
  std::string upperEmblem_, lowerEmblem_, upperBg_, lowerBg_;
};

}
