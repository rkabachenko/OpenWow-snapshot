
#include "openwow/ui/game/ui_coordination.h"

#include <algorithm>

namespace openwow::ui {

UnitFrameRegistry& UnitFrameRegistry::Get() {
  static UnitFrameRegistry instance;
  return instance;
}

void UnitFrameRegistry::RegisterUnitFrame(const std::string& frame_name,
                                          const std::string& unit_id) {
  std::lock_guard lock(mutex_);

  auto old_it = frame_to_unit_.find(frame_name);
  if (old_it != frame_to_unit_.end()) {
    const auto& old_unit = old_it->second;
    auto& frames = unit_to_frames_[old_unit];
    frames.erase(std::remove(frames.begin(), frames.end(), frame_name),
                 frames.end());
    if (frames.empty()) {
      unit_to_frames_.erase(old_unit);
    }
  }

  frame_to_unit_[frame_name] = unit_id;
  unit_to_frames_[unit_id].push_back(frame_name);
}

void UnitFrameRegistry::UnregisterUnitFrame(const std::string& frame_name) {
  std::lock_guard lock(mutex_);

  auto it = frame_to_unit_.find(frame_name);
  if (it == frame_to_unit_.end()) return;

  const auto& unit_id = it->second;
  auto& frames = unit_to_frames_[unit_id];
  frames.erase(std::remove(frames.begin(), frames.end(), frame_name),
               frames.end());
  if (frames.empty()) {
    unit_to_frames_.erase(unit_id);
  }
  frame_to_unit_.erase(it);
}

std::vector<std::string> UnitFrameRegistry::GetFramesForUnit(
    const std::string& unit_id) const {
  std::lock_guard lock(mutex_);
  auto it = unit_to_frames_.find(unit_id);
  if (it != unit_to_frames_.end()) {
    return it->second;
  }
  return {};
}

std::string UnitFrameRegistry::GetUnitForFrame(
    const std::string& frame_name) const {
  std::lock_guard lock(mutex_);
  auto it = frame_to_unit_.find(frame_name);
  if (it != frame_to_unit_.end()) {
    return it->second;
  }
  return {};
}

void UnitFrameRegistry::Reset() {
  std::lock_guard lock(mutex_);
  frame_to_unit_.clear();
  unit_to_frames_.clear();
}

const std::string ModalStack::kEmpty;

ModalStack& ModalStack::Get() {
  static ModalStack instance;
  return instance;
}

void ModalStack::PushModal(const std::string& frame_name) {
  std::lock_guard lock(mutex_);

  for (const auto& s : stack_) {
    if (s == frame_name) return;
  }
  stack_.push_back(frame_name);
}

void ModalStack::PopModal(const std::string& frame_name) {
  std::lock_guard lock(mutex_);
  stack_.erase(std::remove(stack_.begin(), stack_.end(), frame_name),
               stack_.end());
}

bool ModalStack::IsModal(const std::string& frame_name) const {
  std::lock_guard lock(mutex_);
  for (const auto& s : stack_) {
    if (s == frame_name) return true;
  }
  return false;
}

bool ModalStack::HasModal() const {
  std::lock_guard lock(mutex_);
  return !stack_.empty();
}

const std::string& ModalStack::GetTopModal() const {
  std::lock_guard lock(mutex_);
  if (stack_.empty()) return kEmpty;
  return stack_.back();
}

std::size_t ModalStack::GetNumModals() const {
  std::lock_guard lock(mutex_);
  return stack_.size();
}

void ModalStack::Reset() {
  std::lock_guard lock(mutex_);
  stack_.clear();
}

StaticPopupManager& StaticPopupManager::Get() {
  static StaticPopupManager instance;
  return instance;
}

void StaticPopupManager::ShowPopup(const StaticPopupData& data) {
  std::lock_guard lock(mutex_);

  for (auto& p : active_popups_) {
    if (p.which == data.which) {
      p = data;
      return;
    }
  }
  active_popups_.push_back(data);
}

void StaticPopupManager::HidePopup(const std::string& which) {
  std::lock_guard lock(mutex_);
  active_popups_.erase(
      std::remove_if(active_popups_.begin(), active_popups_.end(),
                     [&](const StaticPopupData& p) { return p.which == which; }),
      active_popups_.end());
}

void StaticPopupManager::HideAllPopups() {
  std::lock_guard lock(mutex_);
  active_popups_.clear();
}

bool StaticPopupManager::IsPopupShown(const std::string& which) const {
  std::lock_guard lock(mutex_);
  for (const auto& p : active_popups_) {
    if (p.which == which) return true;
  }
  return false;
}

std::size_t StaticPopupManager::GetNumActivePopups() const {
  std::lock_guard lock(mutex_);
  return active_popups_.size();
}

const StaticPopupData* StaticPopupManager::GetPopup(std::size_t index) const {
  std::lock_guard lock(mutex_);
  if (index >= active_popups_.size()) return nullptr;
  return &active_popups_[index];
}

void StaticPopupManager::Reset() {
  std::lock_guard lock(mutex_);
  active_popups_.clear();
}

FrameStrata FrameStrataFromString(const std::string& s) {
  if (s == "WORLD")             return FrameStrata::World;
  if (s == "BACKGROUND")        return FrameStrata::Background;
  if (s == "LOW")               return FrameStrata::Low;
  if (s == "MEDIUM")            return FrameStrata::Medium;
  if (s == "HIGH")              return FrameStrata::High;
  if (s == "DIALOG")            return FrameStrata::Dialog;
  if (s == "FULLSCREEN")        return FrameStrata::Fullscreen;
  if (s == "FULLSCREEN_DIALOG") return FrameStrata::FullscreenDialog;
  if (s == "TOOLTIP")           return FrameStrata::Tooltip;
  return FrameStrata::Medium;
}

const char* FrameStrataToString(FrameStrata strata) {
  switch (strata) {
    case FrameStrata::World:            return "WORLD";
    case FrameStrata::Background:       return "BACKGROUND";
    case FrameStrata::Low:              return "LOW";
    case FrameStrata::Medium:           return "MEDIUM";
    case FrameStrata::High:             return "HIGH";
    case FrameStrata::Dialog:           return "DIALOG";
    case FrameStrata::Fullscreen:       return "FULLSCREEN";
    case FrameStrata::FullscreenDialog: return "FULLSCREEN_DIALOG";
    case FrameStrata::Tooltip:          return "TOOLTIP";
  }
  return "MEDIUM";
}

PanelManager& PanelManager::Get() {
  static PanelManager instance;
  return instance;
}

void PanelManager::RegisterPanel(const std::string& frame_name,
                                 const std::string& group) {
  std::lock_guard lock(mutex_);

  auto old_it = panel_to_group_.find(frame_name);
  if (old_it != panel_to_group_.end()) {
    const auto& old_group = old_it->second;
    auto& panels = group_to_panels_[old_group];
    panels.erase(std::remove(panels.begin(), panels.end(), frame_name),
                 panels.end());
    if (panels.empty()) {
      group_to_panels_.erase(old_group);
    }
  }

  panel_to_group_[frame_name] = group;
  group_to_panels_[group].push_back(frame_name);

  if (group_visible_.find(group) == group_visible_.end()) {
    group_visible_[group] = false;
  }
}

void PanelManager::UnregisterPanel(const std::string& frame_name) {
  std::lock_guard lock(mutex_);

  auto it = panel_to_group_.find(frame_name);
  if (it == panel_to_group_.end()) return;

  const auto& group = it->second;
  auto& panels = group_to_panels_[group];
  panels.erase(std::remove(panels.begin(), panels.end(), frame_name),
               panels.end());
  if (panels.empty()) {
    group_to_panels_.erase(group);
    group_visible_.erase(group);
  }
  panel_to_group_.erase(it);
}

void PanelManager::ShowGroup(const std::string& group) {
  std::lock_guard lock(mutex_);
  group_visible_[group] = true;
}

void PanelManager::HideGroup(const std::string& group) {
  std::lock_guard lock(mutex_);
  group_visible_[group] = false;
}

void PanelManager::ToggleGroup(const std::string& group) {
  std::lock_guard lock(mutex_);
  group_visible_[group] = !group_visible_[group];
}

bool PanelManager::IsGroupVisible(const std::string& group) const {
  std::lock_guard lock(mutex_);
  auto it = group_visible_.find(group);
  if (it != group_visible_.end()) return it->second;
  return false;
}

std::vector<std::string> PanelManager::GetPanelsInGroup(
    const std::string& group) const {
  std::lock_guard lock(mutex_);
  auto it = group_to_panels_.find(group);
  if (it != group_to_panels_.end()) return it->second;
  return {};
}

void PanelManager::Reset() {
  std::lock_guard lock(mutex_);
  panel_to_group_.clear();
  group_to_panels_.clear();
  group_visible_.clear();
}

}
