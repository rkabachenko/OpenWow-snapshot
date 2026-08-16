#pragma once

#include "openwow/game/missile_node.h"
#include "openwow/game/missile_trajectory.h"

#include <cstdint>
#include <optional>

namespace openwow::game {

class CGUnit_C;
class WorldSession;

class UnitMountComponent final {
public:
  UnitMountComponent() = default;
  UnitMountComponent(const UnitMountComponent &) = delete;
  UnitMountComponent &operator=(const UnitMountComponent &) = delete;
  UnitMountComponent(UnitMountComponent &&) noexcept = default;
  UnitMountComponent &operator=(UnitMountComponent &&) noexcept = default;
  ~UnitMountComponent() = default;

  [[nodiscard]] bool IsMounted(const CGUnit_C &owner) const;
  [[nodiscard]] std::uint32_t DisplayId(const CGUnit_C &owner) const;
  [[nodiscard]] bool IsMountedStateActive(const CGUnit_C &owner) const;
  [[nodiscard]] bool HasMountedDisplay(const CGUnit_C &owner) const;

  void SetDisplayScale(float scale) noexcept { display_scale_ = scale; }
  [[nodiscard]] float DisplayScale() const noexcept { return display_scale_; }

  void SetOverlayM2InstanceId(std::uint32_t id) noexcept {
    overlay_m2_instance_id_ = id;
  }
  [[nodiscard]] std::uint32_t OverlayM2InstanceId() const noexcept {
    return overlay_m2_instance_id_;
  }

  void SetModelDefaultAnimationId(std::optional<std::uint16_t> id) noexcept {
    model_default_anim_id_ = id;
  }
  [[nodiscard]] std::optional<std::uint16_t> ModelDefaultAnimationId() const noexcept {
    return model_default_anim_id_;
  }

  void SetCachedDisplayForSpell(std::uint32_t id) noexcept {
    cached_display_for_spell_ = id;
  }
  [[nodiscard]] std::uint32_t CachedDisplayForSpell() const noexcept {
    return cached_display_for_spell_;
  }

  void SetPendingDisplayChange(std::optional<std::uint32_t> id) noexcept {
    pending_display_change_ = id;
  }
  [[nodiscard]] std::optional<std::uint32_t> PendingDisplayChange() const noexcept {
    return pending_display_change_;
  }

  void SetPendingTransitionSpellId(std::uint32_t id) noexcept {
    pending_transition_spell_id_ = id;
  }
  [[nodiscard]] std::uint32_t PendingTransitionSpellId() const noexcept {
    return pending_transition_spell_id_;
  }
  void ClearPendingTransitionSpellId() noexcept {
    pending_transition_spell_id_ = 0;
  }

  void SetTransitionData(MountTransitionObjectHandle handle,
                         CMissileNode_C *node) noexcept {
    transition_handle_ = handle;
    transition_node_ = node;
  }
  [[nodiscard]] MountTransitionObjectHandle TransitionHandle() const noexcept {
    return transition_handle_;
  }
  [[nodiscard]] CMissileNode_C *TransitionNode() const noexcept {
    return transition_node_;
  }
  void ClearTransitionData() noexcept {
    transition_handle_ = {};
    transition_node_ = nullptr;
  }

  [[nodiscard]] bool HasCompletedTransition() const;
  void CompleteTransition(CGUnit_C &owner, const WorldSession &session);
  void SetPendingTransition(std::uint32_t spell_id,
                            std::uint32_t cached_mount_display) noexcept;
  void HandleDismountPacket(CGUnit_C &owner);
  void Dismount(CGUnit_C &owner, bool update_spell_visuals);
  void ApplyDisplayChange(CGUnit_C &owner, const WorldSession &session,
                          std::uint32_t mount_display_id);
  void ReleaseOverlayM2Instance(CGUnit_C &owner);

private:
  float display_scale_{1.0f};
  std::uint32_t overlay_m2_instance_id_{0};
  std::optional<std::uint16_t> model_default_anim_id_{};
  std::uint32_t cached_display_for_spell_{0};
  std::optional<std::uint32_t> pending_display_change_{};
  std::uint32_t pending_transition_spell_id_{0};
  MountTransitionObjectHandle transition_handle_{};
  CMissileNode_C *transition_node_{nullptr};
};

}
