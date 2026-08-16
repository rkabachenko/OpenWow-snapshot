#pragma once

#include "openwow/ui/widgets/script_object.h"
#include "openwow/foundation/text/ascii.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace openwow::ui {

enum class UiScriptHandlerOwner : std::uint8_t {
  Frame,
  Button,
  EditBox,
  ScrollFrame,
  MessageFrame,
  Slider,
  StatusBar,
  ColorSelect,
  HyperlinkedFrame,
  GameTooltip,
  MovieFrame,
  ModelFrame,
  Animation,
  AnimationGroup,
};

struct FrameScriptTypeInfo {
  const char* canonical_name;
  const char* wrapper_format;
  UiScriptHandlerOwner owner;

  std::uint16_t retail_slot_offset;
  std::uint16_t retail_ppc_slot_offset;

  constexpr FrameScriptTypeInfo(const char* name, const char* format,
                                const UiScriptHandlerOwner handler_owner,
                                const std::uint16_t x86_slot,
                                const std::uint16_t ppc_slot = 0) noexcept
      : canonical_name(name),
        wrapper_format(format),
        owner(handler_owner),
        retail_slot_offset(x86_slot),
        retail_ppc_slot_offset(ppc_slot == 0 ? x86_slot : ppc_slot) {}
};

inline constexpr const char* kDefaultFrameScriptWrapperFormat =
    "return function(self) %s end";

inline constexpr std::array kUiScriptHandlerCatalog = {

    FrameScriptTypeInfo{"OnEvent", "return function(self,event,...) %s end",
                        UiScriptHandlerOwner::Frame, 0x00c},
    FrameScriptTypeInfo{"OnLoad", kDefaultFrameScriptWrapperFormat,
                        UiScriptHandlerOwner::Frame, 0x130},
    FrameScriptTypeInfo{"OnSizeChanged", "return function(self,w,h) %s end",
                        UiScriptHandlerOwner::Frame, 0x138},
    FrameScriptTypeInfo{"OnUpdate", "return function(self,elapsed) %s end",
                        UiScriptHandlerOwner::Frame, 0x140},
    FrameScriptTypeInfo{"OnShow", kDefaultFrameScriptWrapperFormat,
                        UiScriptHandlerOwner::Frame, 0x148},
    FrameScriptTypeInfo{"OnHide", kDefaultFrameScriptWrapperFormat,
                        UiScriptHandlerOwner::Frame, 0x150},
    FrameScriptTypeInfo{"OnEnter", "return function(self,motion) %s end",
                        UiScriptHandlerOwner::Frame, 0x158},
    FrameScriptTypeInfo{"OnLeave", "return function(self,motion) %s end",
                        UiScriptHandlerOwner::Frame, 0x160},
    FrameScriptTypeInfo{"OnMouseDown", "return function(self,button) %s end",
                        UiScriptHandlerOwner::Frame, 0x168},
    FrameScriptTypeInfo{"OnMouseUp", "return function(self,button) %s end",
                        UiScriptHandlerOwner::Frame, 0x170},
    FrameScriptTypeInfo{"OnMouseWheel", "return function(self,delta) %s end",
                        UiScriptHandlerOwner::Frame, 0x178},
    FrameScriptTypeInfo{"OnDragStart", "return function(self,button) %s end",
                        UiScriptHandlerOwner::Frame, 0x180},
    FrameScriptTypeInfo{"OnDragStop", kDefaultFrameScriptWrapperFormat,
                        UiScriptHandlerOwner::Frame, 0x188},
    FrameScriptTypeInfo{"OnReceiveDrag", kDefaultFrameScriptWrapperFormat,
                        UiScriptHandlerOwner::Frame, 0x190},
    FrameScriptTypeInfo{"OnChar", "return function(self,text) %s end",
                        UiScriptHandlerOwner::Frame, 0x198},
    FrameScriptTypeInfo{"OnKeyDown", "return function(self,key) %s end",
                        UiScriptHandlerOwner::Frame, 0x1a0},
    FrameScriptTypeInfo{"OnKeyUp", "return function(self,key) %s end",
                        UiScriptHandlerOwner::Frame, 0x1a8},
    FrameScriptTypeInfo{"OnAttributeChanged",
                        "return function(self,name,value) %s end",
                        UiScriptHandlerOwner::Frame, 0x1b0},
    FrameScriptTypeInfo{"OnEnable", kDefaultFrameScriptWrapperFormat,
                        UiScriptHandlerOwner::Frame, 0x1b8},
    FrameScriptTypeInfo{"OnDisable", kDefaultFrameScriptWrapperFormat,
                        UiScriptHandlerOwner::Frame, 0x1c0},

    FrameScriptTypeInfo{"PreClick", "return function(self,button,down) %s end",
                        UiScriptHandlerOwner::Button, 0x2d4},
    FrameScriptTypeInfo{"OnClick", "return function(self,button,down) %s end",
                        UiScriptHandlerOwner::Button, 0x2dc},
    FrameScriptTypeInfo{"PostClick", "return function(self,button,down) %s end",
                        UiScriptHandlerOwner::Button, 0x2e4},
    FrameScriptTypeInfo{"OnDoubleClick", "return function(self,button) %s end",
                        UiScriptHandlerOwner::Button, 0x2ec},

    FrameScriptTypeInfo{"OnEnterPressed", kDefaultFrameScriptWrapperFormat,
                        UiScriptHandlerOwner::EditBox, 0x37c},
    FrameScriptTypeInfo{"OnEscapePressed", kDefaultFrameScriptWrapperFormat,
                        UiScriptHandlerOwner::EditBox, 0x384},
    FrameScriptTypeInfo{"OnSpacePressed", kDefaultFrameScriptWrapperFormat,
                        UiScriptHandlerOwner::EditBox, 0x38c},
    FrameScriptTypeInfo{"OnTabPressed", kDefaultFrameScriptWrapperFormat,
                        UiScriptHandlerOwner::EditBox, 0x394},
    FrameScriptTypeInfo{"OnTextChanged",
                        "return function(self, userInput) %s end",
                        UiScriptHandlerOwner::EditBox, 0x39c},
    FrameScriptTypeInfo{"OnTextSet", kDefaultFrameScriptWrapperFormat,
                        UiScriptHandlerOwner::EditBox, 0x3a4},
    FrameScriptTypeInfo{"OnCursorChanged",
                        "return function(self,x,y,w,h) %s end",
                        UiScriptHandlerOwner::EditBox, 0x3ac},
    FrameScriptTypeInfo{"OnInputLanguageChanged",
                        "return function(self,language) %s end",
                        UiScriptHandlerOwner::EditBox, 0x3b4},
    FrameScriptTypeInfo{"OnEditFocusGained", kDefaultFrameScriptWrapperFormat,
                        UiScriptHandlerOwner::EditBox, 0x3bc},
    FrameScriptTypeInfo{"OnEditFocusLost", kDefaultFrameScriptWrapperFormat,
                        UiScriptHandlerOwner::EditBox, 0x3c4},
    FrameScriptTypeInfo{"OnCharComposition",
                        "return function(self,text) %s end",
                        UiScriptHandlerOwner::EditBox, 0x3cc},

    FrameScriptTypeInfo{"OnHorizontalScroll",
                        "return function(self,offset) %s end",
                        UiScriptHandlerOwner::ScrollFrame, 0x2b0},
    FrameScriptTypeInfo{"OnVerticalScroll",
                        "return function(self,offset) %s end",
                        UiScriptHandlerOwner::ScrollFrame, 0x2b8},
    FrameScriptTypeInfo{"OnScrollRangeChanged",
                        "return function(self,xrange,yrange) %s end",
                        UiScriptHandlerOwner::ScrollFrame, 0x2c0},

    FrameScriptTypeInfo{"OnMessageScrollChanged",
                        kDefaultFrameScriptWrapperFormat,
                        UiScriptHandlerOwner::MessageFrame, 0x338},

    FrameScriptTypeInfo{"OnValueChanged", "return function(self,value) %s end",
                        UiScriptHandlerOwner::Slider, 0x2b4},
    FrameScriptTypeInfo{"OnMinMaxChanged",
                        "return function(self,min,max) %s end",
                        UiScriptHandlerOwner::Slider, 0x2bc},
    FrameScriptTypeInfo{"OnValueChanged", "return function(self,value) %s end",
                        UiScriptHandlerOwner::StatusBar, 0x2b0},
    FrameScriptTypeInfo{"OnMinMaxChanged",
                        "return function(self,min,max) %s end",
                        UiScriptHandlerOwner::StatusBar, 0x2b8},

    FrameScriptTypeInfo{"OnColorSelect", "return function(self,r,g,b) %s end",
                        UiScriptHandlerOwner::ColorSelect, 0x2bc, 0x2c0},

    FrameScriptTypeInfo{"OnHyperlinkEnter",
                        "return function(self,link,text) %s end",
                        UiScriptHandlerOwner::HyperlinkedFrame, 0x29c},
    FrameScriptTypeInfo{"OnHyperlinkLeave",
                        "return function(self,link,text) %s end",
                        UiScriptHandlerOwner::HyperlinkedFrame, 0x2a4},
    FrameScriptTypeInfo{"OnHyperlinkClick",
                        "return function(self,link,text,button) %s end",
                        UiScriptHandlerOwner::HyperlinkedFrame, 0x2ac},

    FrameScriptTypeInfo{"OnTooltipSetDefaultAnchor",
                        kDefaultFrameScriptWrapperFormat,
                        UiScriptHandlerOwner::GameTooltip, 0x4c8},
    FrameScriptTypeInfo{"OnTooltipCleared", kDefaultFrameScriptWrapperFormat,
                        UiScriptHandlerOwner::GameTooltip, 0x4d0},
    FrameScriptTypeInfo{"OnTooltipAddMoney",
                        "return function(self,cost,maxcost) %s end",
                        UiScriptHandlerOwner::GameTooltip, 0x4d8},
    FrameScriptTypeInfo{"OnTooltipSetUnit", kDefaultFrameScriptWrapperFormat,
                        UiScriptHandlerOwner::GameTooltip, 0x4e0},
    FrameScriptTypeInfo{"OnTooltipSetItem", kDefaultFrameScriptWrapperFormat,
                        UiScriptHandlerOwner::GameTooltip, 0x4e8},
    FrameScriptTypeInfo{"OnTooltipSetSpell", kDefaultFrameScriptWrapperFormat,
                        UiScriptHandlerOwner::GameTooltip, 0x4f0},
    FrameScriptTypeInfo{"OnTooltipSetQuest", kDefaultFrameScriptWrapperFormat,
                        UiScriptHandlerOwner::GameTooltip, 0x4f8},
    FrameScriptTypeInfo{"OnTooltipSetAchievement",
                        kDefaultFrameScriptWrapperFormat,
                        UiScriptHandlerOwner::GameTooltip, 0x500},
    FrameScriptTypeInfo{"OnTooltipSetEquipmentSet",
                        kDefaultFrameScriptWrapperFormat,
                        UiScriptHandlerOwner::GameTooltip, 0x508},
    FrameScriptTypeInfo{"OnTooltipSetFrameStack",
                        kDefaultFrameScriptWrapperFormat,
                        UiScriptHandlerOwner::GameTooltip, 0x510},

    FrameScriptTypeInfo{"OnMovieFinished", kDefaultFrameScriptWrapperFormat,
                        UiScriptHandlerOwner::MovieFrame, 0x3b0},
    FrameScriptTypeInfo{"OnMovieShowSubtitle",
                        "return function(self,text) %s end",
                        UiScriptHandlerOwner::MovieFrame, 0x3b8},
    FrameScriptTypeInfo{"OnMovieHideSubtitle",
                        kDefaultFrameScriptWrapperFormat,
                        UiScriptHandlerOwner::MovieFrame, 0x3c0},

    FrameScriptTypeInfo{"OnUpdateModel", kDefaultFrameScriptWrapperFormat,
                        UiScriptHandlerOwner::ModelFrame, 0x354},
    FrameScriptTypeInfo{"OnAnimFinished", kDefaultFrameScriptWrapperFormat,
                        UiScriptHandlerOwner::ModelFrame, 0x35c},

    FrameScriptTypeInfo{"OnEvent", "return function(self,event,...) %s end",
                        UiScriptHandlerOwner::Animation, 0x00c},
    FrameScriptTypeInfo{"OnLoad", kDefaultFrameScriptWrapperFormat,
                        UiScriptHandlerOwner::Animation, 0x034},
    FrameScriptTypeInfo{"OnPlay", kDefaultFrameScriptWrapperFormat,
                        UiScriptHandlerOwner::Animation, 0x03c},
    FrameScriptTypeInfo{"OnPause", kDefaultFrameScriptWrapperFormat,
                        UiScriptHandlerOwner::Animation, 0x044},
    FrameScriptTypeInfo{"OnStop", "return function(self,requested) %s end",
                        UiScriptHandlerOwner::Animation, 0x04c},
    FrameScriptTypeInfo{"OnFinished", kDefaultFrameScriptWrapperFormat,
                        UiScriptHandlerOwner::Animation, 0x054},
    FrameScriptTypeInfo{"OnUpdate", "return function(self,elapsed) %s end",
                        UiScriptHandlerOwner::Animation, 0x05c},

    FrameScriptTypeInfo{"OnEvent", "return function(self,event,...) %s end",
                        UiScriptHandlerOwner::AnimationGroup, 0x00c},
    FrameScriptTypeInfo{"OnLoad", kDefaultFrameScriptWrapperFormat,
                        UiScriptHandlerOwner::AnimationGroup, 0x050},
    FrameScriptTypeInfo{"OnPlay", kDefaultFrameScriptWrapperFormat,
                        UiScriptHandlerOwner::AnimationGroup, 0x058},
    FrameScriptTypeInfo{"OnPause", kDefaultFrameScriptWrapperFormat,
                        UiScriptHandlerOwner::AnimationGroup, 0x060},
    FrameScriptTypeInfo{"OnStop", "return function(self,requested) %s end",
                        UiScriptHandlerOwner::AnimationGroup, 0x068},
    FrameScriptTypeInfo{"OnFinished",
                        "return function(self,requested) %s end",
                        UiScriptHandlerOwner::AnimationGroup, 0x070},
    FrameScriptTypeInfo{"OnUpdate", "return function(self,elapsed) %s end",
                        UiScriptHandlerOwner::AnimationGroup, 0x078},
    FrameScriptTypeInfo{"OnLoop", "return function(self,loopState) %s end",
                        UiScriptHandlerOwner::AnimationGroup, 0x080},
};

namespace detail {

constexpr char FoldCatalogAscii(const char value) noexcept {
  return value >= 'A' && value <= 'Z'
             ? static_cast<char>(value + ('a' - 'A'))
             : value;
}

constexpr bool CatalogNamesEqual(const char* lhs, const char* rhs) noexcept {
  if (lhs == nullptr || rhs == nullptr) {
    return lhs == rhs;
  }
  while (*lhs != '\0' && *rhs != '\0') {
    if (FoldCatalogAscii(*lhs) != FoldCatalogAscii(*rhs)) {
      return false;
    }
    ++lhs;
    ++rhs;
  }
  return *lhs == *rhs;
}

constexpr std::size_t DistinctCatalogNameCount() noexcept {
  std::size_t count = 0;
  for (std::size_t i = 0; i < kUiScriptHandlerCatalog.size(); ++i) {
    bool seen = false;
    for (std::size_t j = 0; j < i; ++j) {
      if (CatalogNamesEqual(kUiScriptHandlerCatalog[i].canonical_name,
                            kUiScriptHandlerCatalog[j].canonical_name)) {
        seen = true;
        break;
      }
    }
    if (!seen) {
      ++count;
    }
  }
  return count;
}

constexpr bool HasDuplicateOwnerVariant() noexcept {
  for (std::size_t i = 0; i < kUiScriptHandlerCatalog.size(); ++i) {
    for (std::size_t j = 0; j < i; ++j) {
      if (kUiScriptHandlerCatalog[i].owner == kUiScriptHandlerCatalog[j].owner &&
          CatalogNamesEqual(kUiScriptHandlerCatalog[i].canonical_name,
                            kUiScriptHandlerCatalog[j].canonical_name)) {
        return true;
      }
    }
  }
  return false;
}

inline const FrameScriptTypeInfo* LookupCatalogVariant(
    const UiScriptHandlerOwner owner,
    const std::string_view script_name) noexcept {
  for (const auto& entry : kUiScriptHandlerCatalog) {
    if (entry.owner == owner &&
        openwow::text::EqualsIgnoreCaseAscii(script_name,
                                             entry.canonical_name)) {
      return &entry;
    }
  }
  return nullptr;
}

inline widgets::ScriptObjectType ResolveFrameScriptObjectType(
    const std::string_view object_type) noexcept {
  if (openwow::text::EqualsIgnoreCaseAscii(object_type, "ModelFFX")) {
    return widgets::ScriptObjectType::Model;
  }
  return widgets::ScriptObjectTypeFromName(object_type);
}

using ScriptObjectTypeMask = std::uint64_t;
static_assert(static_cast<std::uint8_t>(widgets::ScriptObjectType::COUNT_) < 64,
              "script object applicability mask must fit in 64 bits");

template <typename... Types>
constexpr ScriptObjectTypeMask ScriptTypes(const Types... types) noexcept {
  return (ScriptObjectTypeMask{0} | ... |
          (ScriptObjectTypeMask{1}
           << static_cast<std::uint8_t>(types)));
}

struct FrameScriptOwnerTypeBinding {
  UiScriptHandlerOwner owner;
  ScriptObjectTypeMask accepted_types;
};

inline constexpr std::array kFrameScriptOwnerTypeBindings = {
    FrameScriptOwnerTypeBinding{
        UiScriptHandlerOwner::Frame,
        ScriptTypes(
            widgets::ScriptObjectType::Frame,
            widgets::ScriptObjectType::Button,
            widgets::ScriptObjectType::CheckButton,
            widgets::ScriptObjectType::EditBox,
            widgets::ScriptObjectType::Slider,
            widgets::ScriptObjectType::StatusBar,
            widgets::ScriptObjectType::ScrollFrame,
            widgets::ScriptObjectType::ScrollingMessageFrame,
            widgets::ScriptObjectType::MessageFrame,
            widgets::ScriptObjectType::SimpleHTML,
            widgets::ScriptObjectType::ColorSelect,
            widgets::ScriptObjectType::Model,
            widgets::ScriptObjectType::PlayerModel,
            widgets::ScriptObjectType::DressUpModel,
            widgets::ScriptObjectType::TabardModel,
            widgets::ScriptObjectType::Minimap,
            widgets::ScriptObjectType::GameTooltip,
            widgets::ScriptObjectType::Cooldown,
            widgets::ScriptObjectType::MovieFrame,
            widgets::ScriptObjectType::WorldFrame,
            widgets::ScriptObjectType::QuestPOIFrame)},
    FrameScriptOwnerTypeBinding{
        UiScriptHandlerOwner::Button,
        ScriptTypes(widgets::ScriptObjectType::Button,
                    widgets::ScriptObjectType::CheckButton)},
    FrameScriptOwnerTypeBinding{
        UiScriptHandlerOwner::EditBox,
        ScriptTypes(widgets::ScriptObjectType::EditBox)},
    FrameScriptOwnerTypeBinding{
        UiScriptHandlerOwner::ScrollFrame,
        ScriptTypes(widgets::ScriptObjectType::ScrollFrame)},

    FrameScriptOwnerTypeBinding{
        UiScriptHandlerOwner::HyperlinkedFrame,
        ScriptTypes(widgets::ScriptObjectType::SimpleHTML,
                    widgets::ScriptObjectType::MessageFrame,
                    widgets::ScriptObjectType::ScrollingMessageFrame)},
    FrameScriptOwnerTypeBinding{
        UiScriptHandlerOwner::MessageFrame,
        ScriptTypes(widgets::ScriptObjectType::MessageFrame)},
    FrameScriptOwnerTypeBinding{
        UiScriptHandlerOwner::Slider,
        ScriptTypes(widgets::ScriptObjectType::Slider)},
    FrameScriptOwnerTypeBinding{
        UiScriptHandlerOwner::StatusBar,
        ScriptTypes(widgets::ScriptObjectType::StatusBar)},
    FrameScriptOwnerTypeBinding{
        UiScriptHandlerOwner::ColorSelect,
        ScriptTypes(widgets::ScriptObjectType::ColorSelect)},
    FrameScriptOwnerTypeBinding{
        UiScriptHandlerOwner::GameTooltip,
        ScriptTypes(widgets::ScriptObjectType::GameTooltip)},
    FrameScriptOwnerTypeBinding{
        UiScriptHandlerOwner::MovieFrame,
        ScriptTypes(widgets::ScriptObjectType::MovieFrame)},
    FrameScriptOwnerTypeBinding{
        UiScriptHandlerOwner::ModelFrame,
        ScriptTypes(widgets::ScriptObjectType::Model,
                    widgets::ScriptObjectType::PlayerModel,
                    widgets::ScriptObjectType::DressUpModel,
                    widgets::ScriptObjectType::TabardModel)},
};

constexpr bool FrameScriptOwnerAcceptsType(
    const UiScriptHandlerOwner owner,
    const widgets::ScriptObjectType type) noexcept {
  if (type >= widgets::ScriptObjectType::COUNT_) {
    return false;
  }
  const auto bit = ScriptObjectTypeMask{1}
                   << static_cast<std::uint8_t>(type);
  for (const auto& binding : kFrameScriptOwnerTypeBindings) {
    if (binding.owner == owner) {
      return (binding.accepted_types & bit) != 0;
    }
  }
  return false;
}

inline bool IsFrameOwner(const UiScriptHandlerOwner owner) noexcept {
  return owner != UiScriptHandlerOwner::Animation &&
         owner != UiScriptHandlerOwner::AnimationGroup;
}

}

static_assert(detail::DistinctCatalogNameCount() == 65,
              "3.3.5a exposes exactly 65 canonical UI script handlers");
static_assert(!detail::HasDuplicateOwnerVariant(),
              "script catalog must contain one row per owner/name pair");

inline const FrameScriptTypeInfo* LookupKnownUiScriptHandler(
    const std::string_view script_name) noexcept {
  for (const auto& entry : kUiScriptHandlerCatalog) {
    if (openwow::text::EqualsIgnoreCaseAscii(script_name,
                                             entry.canonical_name)) {
      return &entry;
    }
  }
  return nullptr;
}

inline const FrameScriptTypeInfo* LookupUiScriptHandlerVariant(
    const UiScriptHandlerOwner owner,
    const std::string_view script_name) noexcept {
  return detail::LookupCatalogVariant(owner, script_name);
}

inline std::string CanonicalizeUiScriptHandlerName(
    const std::string_view script_name) {
  if (const auto* info = LookupKnownUiScriptHandler(script_name);
      info != nullptr) {
    return info->canonical_name;
  }

  return std::string(script_name);
}

inline const FrameScriptTypeInfo* LookupFrameScriptTypeInfo(
    const std::string_view object_type,
    const std::string_view script_name) noexcept {
  const auto type = detail::ResolveFrameScriptObjectType(object_type);
  if (type == widgets::ScriptObjectType::COUNT_ ||
      !widgets::IsScriptTypeKindOf(type, widgets::ScriptObjectType::Frame)) {
    return nullptr;
  }

  const auto type_bit = detail::ScriptObjectTypeMask{1}
                        << static_cast<std::uint8_t>(type);
  for (const auto& binding : detail::kFrameScriptOwnerTypeBindings) {
    if ((binding.accepted_types & type_bit) == 0) {
      continue;
    }
    if (const auto* info =
            detail::LookupCatalogVariant(binding.owner, script_name)) {
      return info;
    }
  }
  return nullptr;
}

inline const char* GetAnyFrameScriptWrapperFormat(
    const std::string_view script_name) noexcept {
  for (const auto& entry : kUiScriptHandlerCatalog) {
    if (detail::IsFrameOwner(entry.owner) &&
        openwow::text::EqualsIgnoreCaseAscii(script_name,
                                             entry.canonical_name)) {
      return entry.wrapper_format;
    }
  }
  return nullptr;
}

}
