#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::ui {

class UnitFrameRegistry {
 public:
  static UnitFrameRegistry& Get();

  void RegisterUnitFrame(const std::string& frame_name,
                         const std::string& unit_id);

  void UnregisterUnitFrame(const std::string& frame_name);

  std::vector<std::string> GetFramesForUnit(const std::string& unit_id) const;

  std::string GetUnitForFrame(const std::string& frame_name) const;

  void Reset();

 private:
  UnitFrameRegistry() = default;

  std::unordered_map<std::string, std::string> frame_to_unit_;

  std::unordered_map<std::string, std::vector<std::string>> unit_to_frames_;
  mutable std::mutex mutex_;
};

class ModalStack {
 public:
  static ModalStack& Get();

  void PushModal(const std::string& frame_name);

  void PopModal(const std::string& frame_name);

  bool IsModal(const std::string& frame_name) const;

  bool HasModal() const;

  const std::string& GetTopModal() const;

  std::size_t GetNumModals() const;

  void Reset();

 private:
  ModalStack() = default;
  std::vector<std::string> stack_;
  mutable std::mutex mutex_;
  static const std::string kEmpty;
};

struct StaticPopupData {
  std::string which;
  std::string text;
  std::string button1;
  std::string button2;
  std::string button3;
  float timeout = 0;
  bool show_alert = false;
  bool exclusive = false;
  uint32_t data = 0;
  std::string data_str;
};

class StaticPopupManager {
 public:
  static StaticPopupManager& Get();

  void ShowPopup(const StaticPopupData& data);

  void HidePopup(const std::string& which);

  void HideAllPopups();

  bool IsPopupShown(const std::string& which) const;

  std::size_t GetNumActivePopups() const;

  const StaticPopupData* GetPopup(std::size_t index) const;

  void Reset();

 private:
  StaticPopupManager() = default;
  std::vector<StaticPopupData> active_popups_;
  mutable std::mutex mutex_;
};

enum class FrameStrata : uint8_t {
  World = 0,
  Background = 1,
  Low = 2,
  Medium = 3,
  High = 4,
  Dialog = 5,
  Fullscreen = 6,
  FullscreenDialog = 7,
  Tooltip = 8,
};

FrameStrata FrameStrataFromString(const std::string& s);

const char* FrameStrataToString(FrameStrata strata);

class PanelManager {
 public:
  static PanelManager& Get();

  void RegisterPanel(const std::string& frame_name, const std::string& group);

  void UnregisterPanel(const std::string& frame_name);

  void ShowGroup(const std::string& group);

  void HideGroup(const std::string& group);

  void ToggleGroup(const std::string& group);

  bool IsGroupVisible(const std::string& group) const;

  std::vector<std::string> GetPanelsInGroup(const std::string& group) const;

  void Reset();

 private:
  PanelManager() = default;
  std::unordered_map<std::string, std::string> panel_to_group_;
  std::unordered_map<std::string, std::vector<std::string>> group_to_panels_;
  std::unordered_map<std::string, bool> group_visible_;
  mutable std::mutex mutex_;
};

}
