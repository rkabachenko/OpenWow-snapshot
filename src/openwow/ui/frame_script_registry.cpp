#include "openwow/ui/frame_script_registry.h"

#include <array>
#include <utility>

namespace openwow::ui {

namespace {

struct ScriptTypeMeta {
  FrameScriptType type;
  const char* name;
};

constexpr std::array kScriptTypeMeta = {
    ScriptTypeMeta{FrameScriptType::OnLoad, "OnLoad"},
    ScriptTypeMeta{FrameScriptType::OnShow, "OnShow"},
    ScriptTypeMeta{FrameScriptType::OnHide, "OnHide"},
    ScriptTypeMeta{FrameScriptType::OnUpdate, "OnUpdate"},
    ScriptTypeMeta{FrameScriptType::OnEvent, "OnEvent"},
    ScriptTypeMeta{FrameScriptType::OnClick, "OnClick"},
    ScriptTypeMeta{FrameScriptType::OnDoubleClick, "OnDoubleClick"},
    ScriptTypeMeta{FrameScriptType::OnDragStart, "OnDragStart"},
    ScriptTypeMeta{FrameScriptType::OnDragStop, "OnDragStop"},
    ScriptTypeMeta{FrameScriptType::OnReceiveDrag, "OnReceiveDrag"},
    ScriptTypeMeta{FrameScriptType::OnEnter, "OnEnter"},
    ScriptTypeMeta{FrameScriptType::OnLeave, "OnLeave"},
    ScriptTypeMeta{FrameScriptType::OnMouseDown, "OnMouseDown"},
    ScriptTypeMeta{FrameScriptType::OnMouseUp, "OnMouseUp"},
    ScriptTypeMeta{FrameScriptType::OnMouseWheel, "OnMouseWheel"},
    ScriptTypeMeta{FrameScriptType::OnKeyDown, "OnKeyDown"},
    ScriptTypeMeta{FrameScriptType::OnKeyUp, "OnKeyUp"},
    ScriptTypeMeta{FrameScriptType::OnChar, "OnChar"},
    ScriptTypeMeta{FrameScriptType::OnSizeChanged, "OnSizeChanged"},
    ScriptTypeMeta{FrameScriptType::OnValueChanged, "OnValueChanged"},
    ScriptTypeMeta{FrameScriptType::OnTextChanged, "OnTextChanged"},
    ScriptTypeMeta{FrameScriptType::OnEditFocusGained, "OnEditFocusGained"},
    ScriptTypeMeta{FrameScriptType::OnEditFocusLost, "OnEditFocusLost"},
    ScriptTypeMeta{FrameScriptType::OnTabPressed, "OnTabPressed"},
    ScriptTypeMeta{FrameScriptType::OnEscapePressed, "OnEscapePressed"},
    ScriptTypeMeta{FrameScriptType::OnEnterPressed, "OnEnterPressed"},
    ScriptTypeMeta{FrameScriptType::OnCursorChanged, "OnCursorChanged"},
    ScriptTypeMeta{FrameScriptType::OnTooltipSetUnit, "OnTooltipSetUnit"},
    ScriptTypeMeta{FrameScriptType::OnTooltipSetItem, "OnTooltipSetItem"},
};

}

void FrameScriptRegistry::RegisterScript(const std::string& frameName,
                                         FrameScriptType type,
                                         ScriptHandler handler) {
  scripts_[frameName][type] = std::move(handler);
}

bool FrameScriptRegistry::UnregisterScript(const std::string& frameName,
                                           FrameScriptType type) {
  auto fit = scripts_.find(frameName);
  if (fit == scripts_.end()) return false;
  auto count = fit->second.erase(type);
  if (fit->second.empty()) scripts_.erase(fit);
  return count > 0;
}

bool FrameScriptRegistry::HasScript(const std::string& frameName,
                                    FrameScriptType type) const {
  auto fit = scripts_.find(frameName);
  if (fit == scripts_.end()) return false;
  return fit->second.count(type) > 0;
}

bool FrameScriptRegistry::FireScript(const std::string& frameName,
                                     FrameScriptType type) {
  auto fit = scripts_.find(frameName);
  if (fit == scripts_.end()) return false;
  auto hit = fit->second.find(type);
  if (hit == fit->second.end()) return false;
  hit->second(frameName);
  return true;
}

std::vector<FrameScriptType> FrameScriptRegistry::GetScripts(
    const std::string& frameName) const {
  std::vector<FrameScriptType> result;
  auto fit = scripts_.find(frameName);
  if (fit == scripts_.end()) return result;
  result.reserve(fit->second.size());
  for (const auto& [t, _] : fit->second) result.push_back(t);
  return result;
}

std::size_t FrameScriptRegistry::GetFrameCount() const {
  return scripts_.size();
}

std::size_t FrameScriptRegistry::GetTotalScriptCount() const {
  std::size_t total = 0;
  for (const auto& [_, map] : scripts_) total += map.size();
  return total;
}

std::string FrameScriptRegistry::GetScriptTypeName(FrameScriptType type) {
  auto idx = static_cast<std::size_t>(type);
  if (idx < kScriptTypeMeta.size()) return kScriptTypeMeta[idx].name;
  return "Unknown";
}

std::optional<FrameScriptType> FrameScriptRegistry::GetScriptTypeByName(
    const std::string& name) {
  for (const auto& m : kScriptTypeMeta) {
    if (m.name == name) return m.type;
  }
  return std::nullopt;
}

void FrameScriptRegistry::ClearFrame(const std::string& frameName) {
  scripts_.erase(frameName);
}

void FrameScriptRegistry::ClearAll() { scripts_.clear(); }

void FrameScriptRegistry::Reset() { ClearAll(); }

}
