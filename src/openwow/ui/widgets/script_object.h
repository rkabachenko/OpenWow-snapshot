
#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#ifdef _WIN32
#include <string.h>
#else
#include <strings.h>
#endif

namespace openwow::ui::widgets {

[[nodiscard]] inline bool StrCaseEq(const char* a, const char* b) noexcept {
#ifdef _WIN32
  return _stricmp(a, b) == 0;
#else
  return strcasecmp(a, b) == 0;
#endif
}

enum class ScriptObjectType : uint8_t {
  Object,
  Region,

  FontString,
  Texture,
  Line,

  Frame,
  Button,
  CheckButton,
  EditBox,
  Slider,
  StatusBar,
  ScrollFrame,
  ScrollingMessageFrame,
  MessageFrame,
  SimpleHTML,
  ColorSelect,
  Model,
  PlayerModel,
  DressUpModel,
  TabardModel,
  Minimap,
  GameTooltip,
  Cooldown,
  MovieFrame,
  WorldFrame,
  QuestPOIFrame,

  AnimationGroup,
  Animation,
  Alpha,
  Scale,
  Translation,
  Rotation,

  Font,

  COUNT_
};

[[nodiscard]] const char* ScriptObjectTypeName(ScriptObjectType type) noexcept;

[[nodiscard]] ScriptObjectType ScriptObjectTypeFromName(
    std::string_view name) noexcept;

[[nodiscard]] const char* ResolveRegisteredCreateFrameTypeName(
    std::string_view name) noexcept;

[[nodiscard]] inline ScriptObjectType ScriptObjectTypeParent(
    ScriptObjectType t) noexcept {
  using enum ScriptObjectType;

  if (t >= Frame && t <= QuestPOIFrame) {
    switch (t) {
      case Frame: return Region;
      case CheckButton: return Button;
      case PlayerModel: return Model;
      case DressUpModel:
      case TabardModel:
        return PlayerModel;
      default:
        return Frame;
    }
  }

  if (t >= AnimationGroup && t <= Rotation) {
    return t == AnimationGroup || t == Animation ? Object : Animation;
  }

  switch (t) {
    case Object: return COUNT_;
    case Region: return Object;
    case FontString: return Font;
    case Texture:
    case Line:
    case Font:
      return Region;
    default:
      return COUNT_;
  }
}

[[nodiscard]] inline bool IsScriptTypeKindOf(
    ScriptObjectType actual, ScriptObjectType required) noexcept {
  if (required == ScriptObjectType::Object) return true;
  ScriptObjectType t = actual;
  while (t != ScriptObjectType::COUNT_) {
    if (t == required) return true;
    t = ScriptObjectTypeParent(t);
  }
  return false;
}

class CScriptObject {
 public:
  explicit CScriptObject(ScriptObjectType type) noexcept : type_(type) {}
  virtual ~CScriptObject();

  CScriptObject(const CScriptObject&) = delete;
  CScriptObject& operator=(const CScriptObject&) = delete;
  CScriptObject(CScriptObject&&) noexcept = default;
  CScriptObject& operator=(CScriptObject&&) noexcept = default;

  virtual void SetName(const std::string& name);
  [[nodiscard]] const std::string& GetName() const noexcept { return name_; }

  [[nodiscard]] const char* GetDisplayName() const noexcept {
    return name_.empty() ? "<unnamed>" : name_.c_str();
  }

  void SetId(uint32_t id) noexcept { id_ = id; }
  [[nodiscard]] uint32_t GetId() const noexcept { return id_; }

  [[nodiscard]] ScriptObjectType GetObjectType() const noexcept {
    return type_;
  }
  [[nodiscard]] const char* GetObjectTypeName() const noexcept {
    return ScriptObjectTypeName(type_);
  }

  void SetLuaRef(int ref) noexcept { luaRef_ = ref; }
  [[nodiscard]] int GetLuaRef() const noexcept { return luaRef_; }

  [[nodiscard]] bool IsA(ScriptObjectType t) const noexcept {
    return type_ == t;
  }

  [[nodiscard]] virtual bool IsKindOf(ScriptObjectType t) const noexcept {
    return type_ == t || t == ScriptObjectType::Object;
  }

  [[nodiscard]] virtual bool IsTypeOf(const char* typeName) const noexcept;

 protected:
  ScriptObjectType type_;
  std::string name_;
  uint32_t id_{0};
  int luaRef_{-2};
};

}
