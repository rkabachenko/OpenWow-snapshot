#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/game/framescript/core/frame_base_methods.h"
#include "openwow/ui/game/framescript/core/frame_region_factory.h"
#include "openwow/ui/game/framescript/core/frame_types_widgets.h"
#include "openwow/ui/game/framescript/widgets/quest_poi_frame_methods.h"
#include "openwow/ui/game/framescript/widgets/button_method_support.h"
#include "openwow/ui/game/framescript/widgets/color_select_methods.h"
#include "openwow/ui/game/framescript/widgets/edit_box_methods.h"
#include "openwow/ui/game/framescript/core/frame_font_runtime.h"
#include "openwow/ui/game/framescript/core/frame_layout_methods.h"
#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/game/framescript/core/frame_method_table_runtime.h"
#include "openwow/ui/game/framescript/core/frame_model_lifecycle.h"
#include "openwow/ui/game/framescript/core/frame_script_object_runtime.h"
#include "openwow/ui/game/framescript/widgets/frame_tooltip_methods.h"
#include "openwow/ui/game/framescript/widgets/media_widget_methods.h"
#include "openwow/ui/game/framescript/widgets/model_widget_methods.h"
#include "openwow/ui/game/framescript/widgets/scrolling_widget_methods.h"
#include "openwow/ui/game/framescript/widgets/status_bar_methods.h"
#include "openwow/ui/game/framescript/widgets/text_widget_methods.h"
#include "openwow/ui/game/tooltip_object_bridge.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/ui_enum_helpers.h"
#include "openwow/foundation/text/ascii.h"

#include <lua.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace openwow::ui::game::frame_api {

void RegisterCooldownScriptMethods(lua_State *L) {
  if (L == nullptr) {
    return;
  }

  RegisterFrameScriptMethods(L);
  if (PushRegisteredMethodTable(L, kCooldownMethodTableRegistryKey)) {
    lua_pop(L, 1);
    return;
  }

  lua_newtable(L);
  CopyRegisteredMethodTableFields(L, kFrameMethodTableRegistryKey, -1);
  ApplyCooldownMethods(L);
  CacheFunctionFieldsAsMethodTable(L, -1,
                                   kCooldownMethodTableRegistryKey);
  lua_pop(L, 1);
}

void RegisterSimpleFrameLayoutMethods(lua_State *L) {
  if (L == nullptr) {
    return;
  }

  if (PushRegisteredMethodTable(L, kSimpleFrameLayoutMethodTableRegistryKey)) {
    lua_pop(L, 1);
    return;
  }

  lua_newtable(L);
  ApplySimpleFrameLayoutMethods(L);
  CacheFunctionFieldsAsMethodTable(L, -1, kSimpleFrameLayoutMethodTableRegistryKey);
  lua_pop(L, 1);
}

void UnregisterSimpleFrameLayoutMethods(lua_State *L) {
  UnregisterCachedMethodTable(L, kSimpleFrameLayoutMethodTableRegistryKey);
}

void ApplyRegisteredSimpleFrameLayoutMethods(lua_State *L) {
  if (L == nullptr || lua_istable(L, -1) == 0) {
    return;
  }

  ApplyCachedMethodTableAndStripFunctions(
      L, -1, kSimpleFrameLayoutMethodTableRegistryKey);
}

void RegisterTextureScriptMethods(lua_State *L) {
  if (L == nullptr) {
    return;
  }

  if (PushRegisteredMethodTable(L, kTextureMethodTableRegistryKey)) {
    lua_pop(L, 1);
    return;
  }

  lua_newtable(L);
  const int parent_index = lua_absindex(L, -1);
  CreateTextureTable(L, parent_index);
  CacheFunctionFieldsAsMethodTable(L, -1, kTextureMethodTableRegistryKey);
  lua_pop(L, 2);
}

void UnregisterTextureScriptMethods(lua_State *L) {
  UnregisterCachedMethodTable(L, kTextureMethodTableRegistryKey);
}

void RegisterFontStringScriptMethods(lua_State *L) {
  if (L == nullptr) {
    return;
  }

  if (PushRegisteredMethodTable(L, kFontStringMethodTableRegistryKey)) {
    lua_pop(L, 1);
    return;
  }

  lua_newtable(L);
  const int parent_index = lua_absindex(L, -1);
  CreateFontStringTable(L, parent_index);
  CacheFunctionFieldsAsMethodTable(L, -1, kFontStringMethodTableRegistryKey);
  lua_pop(L, 2);
}

void UnregisterFontStringScriptMethods(lua_State *L) {
  UnregisterCachedMethodTable(L, kFontStringMethodTableRegistryKey);
}

void RegisterFontObjectScriptMethods(lua_State *L) {
  if (L == nullptr) {
    return;
  }

  if (PushRegisteredMethodTable(L, kFontObjectMethodTableRegistryKey)) {
    lua_pop(L, 1);
    return;
  }

  lua_newtable(L);
  ApplyFontObjectMethods(L, -1);
  CacheFunctionFieldsAsMethodTable(L, -1, kFontObjectMethodTableRegistryKey);
  lua_pop(L, 1);
}

void UnregisterFontObjectScriptMethods(lua_State *L) {
  UnregisterCachedMethodTable(L, kFontObjectMethodTableRegistryKey);
}

void RegisterFrameScriptMethodsImpl(lua_State *L) {
  if (L == nullptr) {
    return;
  }

  RegisterSimpleFrameLayoutMethods(L);
  if (PushRegisteredMethodTable(L, kFrameMethodTableRegistryKey)) {
    lua_pop(L, 1);
    return;
  }

  lua_newtable(L);
  CopyRegisteredMethodTableFields(L, kSimpleFrameLayoutMethodTableRegistryKey, -1);
  ApplyBaseFrameMethods(L);
  ApplyCommonFrameMethods(L);
  CacheFunctionFieldsAsMethodTable(L, -1, kFrameMethodTableRegistryKey);
  lua_pop(L, 1);
}

void UnregisterFrameScriptMethods(lua_State *L) {
  UnregisterCachedMethodTable(L, kFrameMethodTableRegistryKey);
}

void ApplyRegisteredFrameMethodsImpl(lua_State *L) {
  if (L == nullptr || lua_istable(L, -1) == 0) {
    return;
  }

  ApplyCachedMethodTableAndStripFunctions(L, -1, kFrameMethodTableRegistryKey);
}

void RegisterGameTooltipScriptMethods(lua_State *L) {
  if (L == nullptr) {
    return;
  }

  if (PushRegisteredMethodTable(L, kGameTooltipMethodTableRegistryKey)) {
    lua_pop(L, 1);
    return;
  }

  lua_newtable(L);
  ApplyBaseFrameMethods(L);
  ApplyCommonFrameMethods(L);
  ApplyGameTooltipMethods(L);
  ApplyGameTooltipContentMethods(L);
  CacheFunctionFieldsAsMethodTable(L, -1, kGameTooltipMethodTableRegistryKey);
  lua_pop(L, 1);
}

void UnregisterGameTooltipScriptMethods(lua_State *L) {
  UnregisterCachedMethodTable(L, kGameTooltipMethodTableRegistryKey);
}

void ApplyRegisteredGameTooltipMethods(lua_State *L) {
  if (L == nullptr || lua_istable(L, -1) == 0) {
    return;
  }

  ApplyPerObjectGameTooltipMethods(L, -1);
}

void EnsureFrameTypeMethodTableRegistered(lua_State* L,
                                          const char* frame_type) {
  if (L == nullptr || frame_type == nullptr ||
      !IsFrameTypeMethodAutoRegistrationEnabled(L)) {
    return;
  }

  using RegisterMethods = void (*)(lua_State*);
  struct FrameTypeRegistration final {
    std::string_view type;
    RegisterMethods register_methods;
  };
  static constexpr std::array registrations{
      FrameTypeRegistration{"Button", RegisterButtonScriptMethods},
      FrameTypeRegistration{"CheckButton", RegisterCheckButtonScriptMethods},
      FrameTypeRegistration{"StatusBar", RegisterStatusBarScriptMethods},
      FrameTypeRegistration{"EditBox", RegisterEditBoxScriptMethods},
      FrameTypeRegistration{"ScrollFrame", RegisterScrollFrameScriptMethods},
      FrameTypeRegistration{"ScrollingMessageFrame",
                            RegisterScrollingMessageFrameScriptMethods},
      FrameTypeRegistration{"Slider", RegisterSliderScriptMethods},
      FrameTypeRegistration{"Cooldown", RegisterCooldownScriptMethods},
      FrameTypeRegistration{"ColorSelect", RegisterColorSelectScriptMethods},
      FrameTypeRegistration{"Model", RegisterModelScriptMethods},
      FrameTypeRegistration{"ModelFFX", RegisterModelScriptMethods},
      FrameTypeRegistration{"PlayerModel", RegisterPlayerModelScriptMethods},
      FrameTypeRegistration{"DressUpModel", RegisterDressUpModelScriptMethods},
      FrameTypeRegistration{"TabardModel", RegisterTabardModelScriptMethods},
      FrameTypeRegistration{"MessageFrame", RegisterMessageFrameScriptMethods},
      FrameTypeRegistration{"SimpleHTML", RegisterSimpleHTMLScriptMethods},
      FrameTypeRegistration{"Minimap", RegisterMinimapScriptMethods},
      FrameTypeRegistration{"GameTooltip", RegisterGameTooltipScriptMethods},
      FrameTypeRegistration{"MovieFrame", RegisterMovieFrameScriptMethods},
      FrameTypeRegistration{"QuestPOIFrame", RegisterQuestPOIFrameScriptMethods},
  };

  const auto registration = std::ranges::find(
      registrations, std::string_view{frame_type},
      &FrameTypeRegistration::type);
  if (registration != registrations.end()) {
    registration->register_methods(L);
  }
}

void UnregisterCooldownScriptMethods(lua_State *L) {
  UnregisterCachedMethodTable(L, kCooldownMethodTableRegistryKey);
}

void ApplyRegisteredCooldownMethods(lua_State *L) {
  if (L == nullptr || lua_istable(L, -1) == 0) {
    return;
  }

  if (!PushRegisteredMethodTable(L, kCooldownMethodTableRegistryKey)) {
    ApplyCooldownMethods(L);
    return;
  }

  RemoveFunctionFieldsFromTable(L, -2);
  lua_setmetatable(L, -2);
}

void RegisterModelScriptMethods(lua_State *L) {
  if (L == nullptr) {
    return;
  }

  if (PushRegisteredMethodTable(L, kModelMethodTableRegistryKey)) {
    lua_pop(L, 1);
    return;
  }

  lua_newtable(L);
  ApplyBaseFrameMethods(L);
  ApplyCommonFrameMethods(L);
  ApplyModelMethods(L);
  ApplyModelLightMethods(L);
  CacheFunctionFieldsAsMethodTable(L, -1, kModelMethodTableRegistryKey);
  lua_pop(L, 1);
}

void UnregisterModelScriptMethods(lua_State *L) {
  UnregisterCachedMethodTable(L, kModelMethodTableRegistryKey);
}

void ApplyRegisteredModelMethods(lua_State *L) {
  if (L == nullptr || lua_istable(L, -1) == 0) {
    return;
  }

  ApplyCachedMethodTableAndStripFunctions(L, -1, kModelMethodTableRegistryKey);
}

void RegisterPlayerModelScriptMethods(lua_State *L) {
  if (L == nullptr) {
    return;
  }

  RegisterModelScriptMethods(L);
  if (PushRegisteredMethodTable(L, kPlayerModelMethodTableRegistryKey)) {
    lua_pop(L, 1);
    return;
  }

  lua_newtable(L);
  CopyRegisteredMethodTableFields(L, kModelMethodTableRegistryKey, -1);
  ApplyPlayerModelSpecificMethods(L);
  ApplyPlayerModelIconTextureMethods(L);
  CacheFunctionFieldsAsMethodTable(L, -1, kPlayerModelMethodTableRegistryKey);
  lua_pop(L, 1);
}

void UnregisterPlayerModelScriptMethods(lua_State *L) {
  UnregisterCachedMethodTable(L, kPlayerModelMethodTableRegistryKey);
}

void ApplyRegisteredPlayerModelMethods(lua_State *L) {
  if (L == nullptr || lua_istable(L, -1) == 0) {
    return;
  }

  ApplyCachedMethodTableAndStripFunctions(L, -1,
                                          kPlayerModelMethodTableRegistryKey);
}

void RegisterDressUpModelScriptMethods(lua_State *L) {
  if (L == nullptr) {
    return;
  }

  RegisterPlayerModelScriptMethods(L);
  if (PushRegisteredMethodTable(L, kDressUpModelMethodTableRegistryKey)) {
    lua_pop(L, 1);
    return;
  }

  lua_newtable(L);
  CopyRegisteredMethodTableFields(L, kPlayerModelMethodTableRegistryKey, -1);
  ApplyDressUpModelSpecificMethods(L);
  CacheFunctionFieldsAsMethodTable(L, -1,
                                   kDressUpModelMethodTableRegistryKey);
  lua_pop(L, 1);
}

void UnregisterDressUpModelScriptMethods(lua_State *L) {
  if (L != nullptr) {
    CancelPendingDressUpItemTemplates(L);
  }
  UnregisterCachedMethodTable(L, kDressUpModelMethodTableRegistryKey);
}

void RegisterTabardModelScriptMethods(lua_State *L) {
  if (L == nullptr) {
    return;
  }

  RegisterPlayerModelScriptMethods(L);
  if (PushRegisteredMethodTable(L, kTabardModelMethodTableRegistryKey)) {
    lua_pop(L, 1);
    return;
  }

  lua_newtable(L);
  CopyRegisteredMethodTableFields(L, kPlayerModelMethodTableRegistryKey, -1);
  ApplyTabardModelSpecificMethods(L);
  CacheFunctionFieldsAsMethodTable(L, -1,
                                   kTabardModelMethodTableRegistryKey);
  lua_pop(L, 1);
}

void UnregisterTabardModelScriptMethods(lua_State *L) {
  UnregisterCachedMethodTable(L, kTabardModelMethodTableRegistryKey);
}

void RegisterQuestPOIFrameScriptMethods(lua_State *L) {
  if (L == nullptr) {
    return;
  }

  RegisterFrameScriptMethods(L);
  if (PushRegisteredMethodTable(L, kQuestPOIFrameMethodTableRegistryKey)) {
    lua_pop(L, 1);
    return;
  }

  lua_newtable(L);
  CopyRegisteredMethodTableFields(L, kFrameMethodTableRegistryKey, -1);
  ApplyQuestPOIFrameSpecificMethods(L);
  CacheFunctionFieldsAsMethodTable(L, -1, kQuestPOIFrameMethodTableRegistryKey);
  lua_pop(L, 1);
}

void UnregisterQuestPOIFrameScriptMethods(lua_State *L) {
  UnregisterCachedMethodTable(L, kQuestPOIFrameMethodTableRegistryKey);
}

void RegisterMovieFrameScriptMethods(lua_State *L) {
  if (L == nullptr) {
    return;
  }

  RegisterFrameScriptMethods(L);
  if (PushRegisteredMethodTable(L, kMovieFrameMethodTableRegistryKey)) {
    lua_pop(L, 1);
    return;
  }

  lua_newtable(L);
  CopyRegisteredMethodTableFields(L, kFrameMethodTableRegistryKey, -1);
  ApplyMovieFrameMethods(L);
  CacheFunctionFieldsAsMethodTable(L, -1, kMovieFrameMethodTableRegistryKey);
  lua_pop(L, 1);
}

void UnregisterMovieFrameScriptMethods(lua_State *L) {
  UnregisterCachedMethodTable(L, kMovieFrameMethodTableRegistryKey);
}

void RegisterMinimapScriptMethods(lua_State *L) {
  if (L == nullptr) {
    return;
  }

  RegisterFrameScriptMethods(L);
  if (PushRegisteredMethodTable(L, kMinimapMethodTableRegistryKey)) {
    lua_pop(L, 1);
    return;
  }

  lua_newtable(L);
  CopyRegisteredMethodTableFields(L, kFrameMethodTableRegistryKey, -1);
  ApplyMinimapMethods(L);
  ApplyMinimapTextureMethods(L);
  CacheFunctionFieldsAsMethodTable(L, -1, kMinimapMethodTableRegistryKey);
  lua_pop(L, 1);
}

void UnregisterMinimapScriptMethods(lua_State *L) {
  UnregisterCachedMethodTable(L, kMinimapMethodTableRegistryKey);
}

void RegisterButtonScriptMethods(lua_State *L) {
  if (L == nullptr) return;
  RegisterFrameScriptMethods(L);
  if (PushRegisteredMethodTable(L, kButtonMethodTableRegistryKey)) {
    lua_pop(L, 1);
    return;
  }
  lua_newtable(L);
  CopyRegisteredMethodTableFields(L, kFrameMethodTableRegistryKey, -1);
  ApplyButtonMethods(L);
  CacheFunctionFieldsAsMethodTable(L, -1, kButtonMethodTableRegistryKey);
  lua_pop(L, 1);
}

void UnregisterButtonScriptMethods(lua_State *L) {
  UnregisterCachedMethodTable(L, kButtonMethodTableRegistryKey);
}

void RegisterCheckButtonScriptMethods(lua_State *L) {
  if (L == nullptr) return;
  RegisterButtonScriptMethods(L);
  if (PushRegisteredMethodTable(L, kCheckButtonMethodTableRegistryKey)) {
    lua_pop(L, 1);
    return;
  }
  lua_newtable(L);
  CopyRegisteredMethodTableFields(L, kButtonMethodTableRegistryKey, -1);
  ApplyCheckButtonMethods(L);
  CacheFunctionFieldsAsMethodTable(L, -1, kCheckButtonMethodTableRegistryKey);
  lua_pop(L, 1);
}

void UnregisterCheckButtonScriptMethods(lua_State *L) {
  UnregisterCachedMethodTable(L, kCheckButtonMethodTableRegistryKey);
}

void RegisterEditBoxScriptMethods(lua_State *L) {
  if (L == nullptr) return;
  RegisterFrameScriptMethods(L);
  if (PushRegisteredMethodTable(L, kEditBoxMethodTableRegistryKey)) {
    lua_pop(L, 1);
    return;
  }
  lua_newtable(L);
  CopyRegisteredMethodTableFields(L, kFrameMethodTableRegistryKey, -1);
  ApplyEditBoxMethods(L);
  ApplyEditBoxStateMethods(L);

  const int methods = lua_absindex(L, -1);
  InstallTypedFontObjectPair(L, methods, "EditBox");
  InstallTypedFontShadowMethods(L, methods, "EditBox");
  InstallTypedFontJustifyPair(L, methods, "EditBox", true, "LEFT");
  InstallTypedFontJustifyPair(L, methods, "EditBox", false, "MIDDLE");
  InstallTypedFontSpacingPair(L, methods, "EditBox");
  CacheFunctionFieldsAsMethodTable(L, -1, kEditBoxMethodTableRegistryKey);
  lua_pop(L, 1);
}

void UnregisterEditBoxScriptMethods(lua_State *L) {
  UnregisterCachedMethodTable(L, kEditBoxMethodTableRegistryKey);
}

void RegisterSimpleHTMLScriptMethods(lua_State *L) {
  if (L == nullptr) return;
  RegisterFrameScriptMethods(L);
  if (PushRegisteredMethodTable(L, kSimpleHTMLMethodTableRegistryKey)) {
    lua_pop(L, 1);
    return;
  }
  lua_newtable(L);
  CopyRegisteredMethodTableFields(L, kFrameMethodTableRegistryKey, -1);

  ApplySimpleHTMLMethods(L);
  InstallTypedFontObjectPair(L, lua_absindex(L, -1), "SimpleHTML");
  CacheFunctionFieldsAsMethodTable(L, -1, kSimpleHTMLMethodTableRegistryKey);
  lua_pop(L, 1);
}

void UnregisterSimpleHTMLScriptMethods(lua_State *L) {
  UnregisterCachedMethodTable(L, kSimpleHTMLMethodTableRegistryKey);
}

void RegisterMessageFrameScriptMethods(lua_State *L) {
  if (L == nullptr) return;
  RegisterFrameScriptMethods(L);
  if (PushRegisteredMethodTable(L, kMessageFrameMethodTableRegistryKey)) {
    lua_pop(L, 1);
    return;
  }
  lua_newtable(L);
  CopyRegisteredMethodTableFields(L, kFrameMethodTableRegistryKey, -1);

  ApplyScrollingMessageFrameMethods(L);
  ApplyMessageFrameStateMethods(L);
  ApplyMessageFrameTextOverrides(L);
  const int methods = lua_absindex(L, -1);
  InstallTypedFontObjectPair(L, methods, "MessageFrame");
  InstallTypedFontShadowMethods(L, methods, "MessageFrame");
  InstallTypedFontJustifyPair(L, methods, "MessageFrame", true, "LEFT");
  InstallTypedFontJustifyPair(L, methods, "MessageFrame", false, "MIDDLE");
  InstallTypedFontSpacingPair(L, methods, "MessageFrame");
  for (const char *name : kScrollingMessageFrameOnlyMethods) {
    lua_pushnil(L);
    lua_setfield(L, methods, name);
  }
  CacheFunctionFieldsAsMethodTable(L, -1, kMessageFrameMethodTableRegistryKey);
  lua_pop(L, 1);
}

void UnregisterMessageFrameScriptMethods(lua_State *L) {
  UnregisterCachedMethodTable(L, kMessageFrameMethodTableRegistryKey);
}

void RegisterScrollingMessageFrameScriptMethods(lua_State *L) {
  if (L == nullptr) return;
  RegisterFrameScriptMethods(L);
  if (PushRegisteredMethodTable(L,
                                kScrollingMessageFrameMethodTableRegistryKey)) {
    lua_pop(L, 1);
    return;
  }
  lua_newtable(L);
  CopyRegisteredMethodTableFields(L, kFrameMethodTableRegistryKey, -1);

  ApplyScrollingMessageFrameMethods(L);
  ApplyMessageFrameStateMethods(L);
  ApplyScrollingMessageFrameStateMethods(L);
  ApplyScrollingMessageFrameWidgetExtras(L);
  const int methods = lua_absindex(L, -1);
  InstallTypedFontObjectPair(L, methods, "ScrollingMessageFrame");
  InstallTypedFontShadowMethods(L, methods, "ScrollingMessageFrame");
  InstallTypedFontJustifyPair(L, methods, "ScrollingMessageFrame", true,
                              "LEFT");
  InstallTypedFontJustifyPair(L, methods, "ScrollingMessageFrame", false,
                              "MIDDLE");
  InstallTypedFontSpacingPair(L, methods, "ScrollingMessageFrame");
  CacheFunctionFieldsAsMethodTable(
      L, -1, kScrollingMessageFrameMethodTableRegistryKey);
  lua_pop(L, 1);
}

void UnregisterScrollingMessageFrameScriptMethods(lua_State *L) {
  UnregisterCachedMethodTable(L, kScrollingMessageFrameMethodTableRegistryKey);
}

void RegisterScrollFrameScriptMethods(lua_State *L) {
  if (L == nullptr) return;
  RegisterFrameScriptMethods(L);
  if (PushRegisteredMethodTable(L, kScrollFrameMethodTableRegistryKey)) {
    lua_pop(L, 1);
    return;
  }
  lua_newtable(L);
  CopyRegisteredMethodTableFields(L, kFrameMethodTableRegistryKey, -1);
  ApplyScrollFrameMethods(L);
  ApplyScrollFrameWidgetExtras(L);
  CacheFunctionFieldsAsMethodTable(L, -1, kScrollFrameMethodTableRegistryKey);
  lua_pop(L, 1);
}

void UnregisterScrollFrameScriptMethods(lua_State *L) {
  UnregisterCachedMethodTable(L, kScrollFrameMethodTableRegistryKey);
}

void RegisterSliderScriptMethods(lua_State *L) {
  if (L == nullptr) return;
  RegisterFrameScriptMethods(L);
  if (PushRegisteredMethodTable(L, kSliderMethodTableRegistryKey)) {
    lua_pop(L, 1);
    return;
  }
  lua_newtable(L);
  CopyRegisteredMethodTableFields(L, kFrameMethodTableRegistryKey, -1);
  ApplySliderMethods(L);
  CacheFunctionFieldsAsMethodTable(L, -1, kSliderMethodTableRegistryKey);
  lua_pop(L, 1);
}

void UnregisterSliderScriptMethods(lua_State *L) {
  UnregisterCachedMethodTable(L, kSliderMethodTableRegistryKey);
}

void RegisterStatusBarScriptMethods(lua_State *L) {
  if (L == nullptr) return;
  RegisterFrameScriptMethods(L);
  if (PushRegisteredMethodTable(L, kStatusBarMethodTableRegistryKey)) {
    lua_pop(L, 1);
    return;
  }
  lua_newtable(L);
  CopyRegisteredMethodTableFields(L, kFrameMethodTableRegistryKey, -1);
  ApplyStatusBarMethods(L);
  CacheFunctionFieldsAsMethodTable(L, -1, kStatusBarMethodTableRegistryKey);
  lua_pop(L, 1);
}

void UnregisterStatusBarScriptMethods(lua_State *L) {
  UnregisterCachedMethodTable(L, kStatusBarMethodTableRegistryKey);
}

void RegisterColorSelectScriptMethods(lua_State *L) {
  if (L == nullptr) return;
  RegisterFrameScriptMethods(L);
  if (PushRegisteredMethodTable(L, kColorSelectMethodTableRegistryKey)) {
    lua_pop(L, 1);
    return;
  }
  lua_newtable(L);
  CopyRegisteredMethodTableFields(L, kFrameMethodTableRegistryKey, -1);
  ApplyColorSelectMethods(L);
  CacheFunctionFieldsAsMethodTable(L, -1, kColorSelectMethodTableRegistryKey);
  lua_pop(L, 1);
}

void UnregisterColorSelectScriptMethods(lua_State *L) {
  UnregisterCachedMethodTable(L, kColorSelectMethodTableRegistryKey);
}

void RegisterSimpleModelBaseScriptMethods(lua_State *L) {
  if (L == nullptr) return;
  RegisterFrameScriptMethods(L);
  if (PushRegisteredMethodTable(L, kSimpleModelBaseMethodTableRegistryKey)) {
    lua_pop(L, 1);
    return;
  }
  lua_newtable(L);
  CopyRegisteredMethodTableFields(L, kFrameMethodTableRegistryKey, -1);
  ApplyModelMethods(L);
  CacheFunctionFieldsAsMethodTable(L, -1, kSimpleModelBaseMethodTableRegistryKey);
  lua_pop(L, 1);
}

void UnregisterSimpleModelBaseScriptMethods(lua_State *L) {
  UnregisterCachedMethodTable(L, kSimpleModelBaseMethodTableRegistryKey);
}

void ApplyFrameTypeMethodsImpl(lua_State *L, const char *frame_type) {
  if (!frame_type)
    return;

  EnsureFrameTypeMethodTableRegistered(L, frame_type);

  if (std::strcmp(frame_type, "Button") == 0) {
    ApplyRegisteredMethodTableAsMetatable(L, kButtonMethodTableRegistryKey);
  } else if (std::strcmp(frame_type, "CheckButton") == 0) {
    ApplyRegisteredMethodTableAsMetatable(L, kCheckButtonMethodTableRegistryKey);
  } else if (std::strcmp(frame_type, "StatusBar") == 0) {
    ApplyRegisteredMethodTableAsMetatable(L, kStatusBarMethodTableRegistryKey);
  } else if (std::strcmp(frame_type, "EditBox") == 0) {
    ApplyRegisteredMethodTableAsMetatable(L, kEditBoxMethodTableRegistryKey);
    InitializeEditBoxInstanceDefaults(L, lua_absindex(L, -1));
  } else if (std::strcmp(frame_type, "ScrollFrame") == 0) {
    ApplyRegisteredMethodTableAsMetatable(L, kScrollFrameMethodTableRegistryKey);
  } else if (std::strcmp(frame_type, "ScrollingMessageFrame") == 0) {
    ApplyRegisteredMethodTableAsMetatable(
        L, kScrollingMessageFrameMethodTableRegistryKey);
  } else if (std::strcmp(frame_type, "Slider") == 0) {
    ApplyRegisteredMethodTableAsMetatable(L, kSliderMethodTableRegistryKey);
  } else if (std::strcmp(frame_type, "Cooldown") == 0) {
    ApplyRegisteredMethodTableAsMetatable(L,
                                          kCooldownMethodTableRegistryKey);
  } else if (std::strcmp(frame_type, "ColorSelect") == 0) {
    ApplyRegisteredMethodTableAsMetatable(L,
                                          kColorSelectMethodTableRegistryKey);
  } else if (std::strcmp(frame_type, "Model") == 0) {
    ApplyRegisteredModelMethods(L);
  } else if (std::strcmp(frame_type, "ModelFFX") == 0) {
    ApplyRegisteredModelMethods(L);
  } else if (std::strcmp(frame_type, "PlayerModel") == 0) {
    ApplyRegisteredPlayerModelMethods(L);
  } else if (std::strcmp(frame_type, "DressUpModel") == 0) {
    ApplyRegisteredMethodTableAsMetatable(
        L, kDressUpModelMethodTableRegistryKey);
  } else if (std::strcmp(frame_type, "TabardModel") == 0) {
    ApplyRegisteredMethodTableAsMetatable(
        L, kTabardModelMethodTableRegistryKey);
  } else if (std::strcmp(frame_type, "MessageFrame") == 0) {
    ApplyRegisteredMethodTableAsMetatable(
        L, kMessageFrameMethodTableRegistryKey);
  } else if (std::strcmp(frame_type, "SimpleHTML") == 0) {
    ApplyRegisteredMethodTableAsMetatable(
        L, kSimpleHTMLMethodTableRegistryKey);
  } else if (std::strcmp(frame_type, "Minimap") == 0) {
    ApplyRegisteredMethodTableAsMetatable(L, kMinimapMethodTableRegistryKey);
  } else if (std::strcmp(frame_type, "GameTooltip") == 0) {
    ApplyRegisteredGameTooltipMethods(L);
  } else if (std::strcmp(frame_type, "MovieFrame") == 0) {
    ApplyRegisteredMethodTableAsMetatable(L, kMovieFrameMethodTableRegistryKey);
  } else if (std::strcmp(frame_type, "QuestPOIFrame") == 0) {
    InitializeQuestPOIFrameDefaults(L, lua_absindex(L, -1));
    ApplyRegisteredMethodTableAsMetatable(L,
                                          kQuestPOIFrameMethodTableRegistryKey);
  }
}

int PushFontObjectJustify(lua_State *L, const char *field_name,
                          const char *default_value, bool horizontal) {
  const int self = ValidateFrameObjectSelf(L, "Font");
  lua_getfield(L, self, field_name);
  const char *stored = lua_tostring(L, -1);
  if (stored == nullptr || *stored == '\0') {
    lua_pop(L, 1);
    lua_pushstring(L, default_value);
    return 1;
  }

  uint32_t flags = 0;
  const int parsed = horizontal ? openwow::ui::StringToHorizontalJustify(stored, &flags)
                                : openwow::ui::StringToVerticalJustify(stored, &flags);
  lua_pop(L, 1);
  lua_pushstring(L, horizontal ? openwow::ui::HorizontalJustifyFlagsToString(parsed ? flags : 0)
                               : openwow::ui::VerticalJustifyFlagsToString(parsed ? flags : 0));
  return 1;
}

}
