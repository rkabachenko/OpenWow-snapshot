#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::ui {

enum class FrameScriptType : uint8_t {
  OnLoad,
  OnShow,
  OnHide,
  OnUpdate,
  OnEvent,
  OnClick,
  OnDoubleClick,
  OnDragStart,
  OnDragStop,
  OnReceiveDrag,
  OnEnter,
  OnLeave,
  OnMouseDown,
  OnMouseUp,
  OnMouseWheel,
  OnKeyDown,
  OnKeyUp,
  OnChar,
  OnSizeChanged,
  OnValueChanged,
  OnTextChanged,
  OnEditFocusGained,
  OnEditFocusLost,
  OnTabPressed,
  OnEscapePressed,
  OnEnterPressed,
  OnCursorChanged,
  OnTooltipSetUnit,
  OnTooltipSetItem,
  COUNT_
};

struct FrameScriptTypeHash {
  std::size_t operator()(FrameScriptType t) const noexcept {
    return static_cast<std::size_t>(t);
  }
};

using ScriptHandler = std::function<void(const std::string& frameName)>;

class FrameScriptRegistry {
 public:
  FrameScriptRegistry() = default;
  ~FrameScriptRegistry() = default;

  void RegisterScript(const std::string& frameName, FrameScriptType type,
                      ScriptHandler handler);

  bool UnregisterScript(const std::string& frameName, FrameScriptType type);

  [[nodiscard]] bool HasScript(const std::string& frameName,
                               FrameScriptType type) const;

  bool FireScript(const std::string& frameName, FrameScriptType type);

  [[nodiscard]] std::vector<FrameScriptType> GetScripts(
      const std::string& frameName) const;

  [[nodiscard]] std::size_t GetFrameCount() const;

  [[nodiscard]] std::size_t GetTotalScriptCount() const;

  [[nodiscard]] static std::string GetScriptTypeName(FrameScriptType type);

  [[nodiscard]] static std::optional<FrameScriptType> GetScriptTypeByName(
      const std::string& name);

  void ClearFrame(const std::string& frameName);

  void ClearAll();

  void Reset();

 private:

  std::unordered_map<std::string,
                     std::unordered_map<FrameScriptType, ScriptHandler,
                                        FrameScriptTypeHash>>
      scripts_;
};

}
