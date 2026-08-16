#include "openwow/game/actions/bindings/adapters/platform/sdl_binding_input.h"

#include "openwow/core/decimal_parse.h"
#include "openwow/foundation/text/ascii.h"
#include "openwow/ui/lua_client_environment.h"
#include "openwow/ui/xml/xml_tree.h"

#include <SDL.h>

#include <cctype>
#include <string>

namespace openwow::game::actions::bindings::adapters::platform {
namespace {

[[nodiscard]] std::string_view Attribute(
    const openwow::ui::xml::CXMLNode& node,
    const std::string_view name) {
  for (const auto& attribute : node.attributes.entries) {
    if (openwow::text::EqualsIgnoreCaseAscii(attribute.name, name)) {
      return attribute.value;
    }
  }
  return {};
}

[[nodiscard]] bool PlatformMatches(const std::string_view platform) {
  if (platform.empty()) return true;
  const std::string_view active =
      openwow::ui::GetHostLuaClientPlatform() ==
              openwow::ui::LuaClientPlatform::kMac
          ? std::string_view("mac")
          : std::string_view("windows");
  return openwow::text::EqualsIgnoreCaseAscii(platform, active);
}

[[nodiscard]] std::string TrimCdata(std::string value) {
  const auto trim = [](std::string& text) {
    while (!text.empty() &&
           std::isspace(static_cast<unsigned char>(text.front())) != 0) {
      text.erase(text.begin());
    }
    while (!text.empty() &&
           std::isspace(static_cast<unsigned char>(text.back())) != 0) {
      text.pop_back();
    }
  };
  trim(value);
  constexpr std::string_view kPrefix = "<![CDATA[";
  constexpr std::string_view kSuffix = "]]>";
  if (value.starts_with(kPrefix) && value.ends_with(kSuffix)) {
    value = value.substr(
        kPrefix.size(),
        value.size() - kPrefix.size() - kSuffix.size());
    trim(value);
  }
  return value;
}

[[nodiscard]] std::optional<bool> BooleanAttribute(
    const openwow::ui::xml::CXMLNode& node,
    const std::string_view name) {
  const auto value = Attribute(node, name);
  if (value.empty()) return std::nullopt;
  const std::string normalized =
      openwow::text::ToUpperAscii(std::string(value));
  return normalized == "1" || normalized == "TRUE" ||
         normalized == "YES" || normalized == "ON" ||
         normalized == "ENABLED";
}

[[nodiscard]] std::uint32_t IndexAttribute(
    const openwow::ui::xml::CXMLNode& node,
    const std::string_view name,
    const std::uint32_t fallback) {
  const auto value = Attribute(node, name);
  return value.empty()
             ? fallback
             : openwow::core::ParseSignedDecimalLikeSub76F0D0(value) - 1u;
}

[[nodiscard]] int ButtonAttribute(
    const openwow::ui::xml::CXMLNode& node,
    const std::string_view name,
    const int fallback) {
  const auto value = Attribute(node, name);
  return value.empty()
             ? fallback
             : static_cast<int>(
                   openwow::core::ParseSignedDecimalLikeSub76F0D0(value) -
                   1u);
}

}

std::optional<std::string> QueryPrimaryJoystickName() {
  if (SDL_WasInit(SDL_INIT_JOYSTICK) == 0 || SDL_NumJoysticks() <= 0) {
    return std::nullopt;
  }
  const char* name = SDL_JoystickNameForIndex(0);
  return name == nullptr || *name == '\0'
             ? std::nullopt
             : std::optional<std::string>(name);
}

std::optional<JoystickConfigProfile> ParseJoystickConfigProfile(
    const std::string_view xml_text,
    const std::string_view joystick_name) {
  if (xml_text.empty() || joystick_name.empty()) return std::nullopt;
  auto* tree =
      openwow::ui::xml::XMLTree_Parse(xml_text.data(), xml_text.size());
  if (tree == nullptr || tree->root == nullptr) {
    if (tree != nullptr) openwow::ui::xml::XMLTree_Free(tree);
    return std::nullopt;
  }

  const openwow::ui::xml::CXMLNode* matched = nullptr;
  for (auto* node = tree->root->first_child; node != nullptr;
       node = node->right_sibling) {
    if (!openwow::text::EqualsIgnoreCaseAscii(node->tag, "Joystick") ||
        !PlatformMatches(Attribute(*node, "platform"))) {
      continue;
    }
    for (auto* child = node->first_child; child != nullptr;
         child = child->right_sibling) {
      if (openwow::text::EqualsIgnoreCaseAscii(child->tag, "name") &&
          openwow::text::EqualsIgnoreCaseAscii(
              Attribute(*child, "name"), joystick_name)) {
        matched = node;
        break;
      }
    }
    if (matched != nullptr) break;
  }

  if (matched == nullptr) {
    openwow::ui::xml::XMLTree_Free(tree);
    return std::nullopt;
  }

  JoystickConfigProfile profile;
  profile.name = joystick_name;
  for (auto* child = matched->first_child; child != nullptr;
       child = child->right_sibling) {
    if (openwow::text::EqualsIgnoreCaseAscii(
            child->tag, "DefaultBindings")) {
      profile.default_bindings =
          TrimCdata(child->text != nullptr ? child->text : "");
    } else if (openwow::text::EqualsIgnoreCaseAscii(
                   child->tag, "stick")) {
      profile.sticks.push_back({
          .stick_index = IndexAttribute(*child, "index", 0),
          .axis_x = IndexAttribute(*child, "axisX", 0),
          .axis_y = IndexAttribute(*child, "axisY", 1),
      });
    } else if (openwow::text::EqualsIgnoreCaseAscii(
                   child->tag, "slider")) {
      const auto axis = Attribute(*child, "axis");
      if (!axis.empty()) {
        profile.slider_axes.push_back(
            openwow::core::ParseSignedDecimalLikeSub76F0D0(axis) - 1u);
      }
    } else if (openwow::text::EqualsIgnoreCaseAscii(
                   child->tag, "hat")) {
      profile.hats.push_back({
          .hat_index = IndexAttribute(*child, "index", 0),
          .buttons = {
              ButtonAttribute(*child, "button1", -1),
              ButtonAttribute(*child, "button2", -1),
              ButtonAttribute(*child, "button3", -1),
              ButtonAttribute(*child, "button4", -1),
          },
      });
    }
  }

  if (const auto* mouse =
          openwow::ui::xml::XMLNode_FindChildByNameNoCase(matched, "mouse");
      mouse != nullptr) {
    JoystickMouseConfig config;
    config.enabled = BooleanAttribute(*mouse, "enabled");
    config.stick_index = IndexAttribute(*mouse, "stickIndex", 0);
    config.button_left = ButtonAttribute(*mouse, "buttonLeft", 0);
    config.button_right = ButtonAttribute(*mouse, "buttonRight", 1);
    const auto speed = Attribute(*mouse, "speedScale");
    if (!speed.empty()) {
      config.speed_scale = static_cast<float>(
          openwow::core::ParseFloatLikeSub76FB80(speed));
    }
    profile.mouse = config;
  }

  openwow::ui::xml::XMLTree_Free(tree);
  return profile;
}

}
