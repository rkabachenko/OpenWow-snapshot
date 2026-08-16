#include "openwow/game/actions/bindings/adapters/xml/binding_xml_adapter.h"

#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/foundation/text/ascii.h"
#include "openwow/game/actions/bindings/adapters/lua/binding_script_executor.h"
#include "openwow/game/actions/bindings/adapters/retail/modified_click_adapter.h"
#include "openwow/game/actions/bindings/application/binding_profiles.h"
#include "openwow/ui/game/ui_load_status.h"
#include "openwow/ui/lua_client_environment.h"
#include "openwow/ui/xml/xml_tree.h"
#include "openwow/ui/xml/xml_tree_file_loader.h"
#include "openwow/vfs/virtual_file_system.h"

#include <cctype>
#include <memory>
#include <optional>
#include <string>

namespace openwow::game::actions::bindings::adapters::xml {
namespace {

class CompiledBindingScript {
 public:
  CompiledBindingScript(lua_State* state, const int reference)
      : state_(state), reference_(reference) {}
  ~CompiledBindingScript() {
    ::openwow::game::actions::bindings::adapters::lua::
        ReleaseBindingScript(state_, reference_);
  }

  void Execute(const BindingInvocation& invocation) const {
    std::optional<
        ::openwow::game::actions::bindings::adapters::lua::
            ScopedLuaModifierState>
        modifier_state;
    if (invocation.modifier_state) {
      modifier_state.emplace(state_, *invocation.modifier_state);
    }
    ::openwow::game::actions::bindings::adapters::lua::
        ExecuteBindingScript(
            state_, reference_,
            {
                .key_down = invocation.pressed,
                .pressure = invocation.pressure,
                .angle = invocation.angle,
                .precision = invocation.precision,
            });
  }

 private:
  lua_State* state_;
  int reference_;
};

void Report(openwow::ui::game::UiLoadStatusSink* status,
            const std::intptr_t code,
            const std::string_view message) {
  if (message.empty()) return;
  if (status != nullptr) status->AppendStatus(code, message);
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                            std::string(message));
}

[[nodiscard]] std::optional<std::string_view> FindAttribute(
    const openwow::ui::xml::CXMLNode& node,
    const std::string_view name) {
  for (const auto& attribute : node.attributes.entries) {
    if (openwow::text::EqualsIgnoreCaseAscii(attribute.name, name)) {
      return attribute.value;
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::string Attribute(
    const openwow::ui::xml::CXMLNode& node,
    const std::string_view name) {
  const auto attribute = FindAttribute(node, name);
  return attribute ? std::string(*attribute) : std::string{};
}

[[nodiscard]] bool AttributeBoolean(
    const openwow::ui::xml::CXMLNode& node,
    const std::string_view name) {
  const std::string value =
      openwow::text::ToUpperAscii(Attribute(node, name));
  return value == "1" || value == "TRUE" || value == "YES" ||
         value == "ON" || value == "ENABLED";
}

[[nodiscard]] bool PlatformMatches(
    const std::optional<std::string_view> platform) {
  if (!platform) return true;
  if (platform->empty()) return false;
  const std::string_view active =
      openwow::ui::GetHostLuaClientPlatform() ==
              openwow::ui::LuaClientPlatform::kMac
          ? std::string_view("mac")
          : std::string_view("windows");
  return openwow::text::EqualsIgnoreCaseAscii(*platform, active);
}

[[nodiscard]] std::string TrimBody(std::string body) {
  const auto trim = [](std::string& value) {
    while (!value.empty() && value.front() != '\0' &&
           std::isspace(static_cast<unsigned char>(value.front())) != 0) {
      value.erase(value.begin());
    }
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.back())) != 0) {
      value.pop_back();
    }
  };
  trim(body);
  constexpr std::string_view kCdataPrefix = "<![CDATA[";
  constexpr std::string_view kCdataSuffix = "]]>";
  if (body.starts_with(kCdataPrefix) && body.ends_with(kCdataSuffix)) {
    body = body.substr(kCdataPrefix.size(),
                       body.size() - kCdataPrefix.size() -
                           kCdataSuffix.size());
    trim(body);
  }
  return body;
}

void ParseBinding(BindingProfiles& profiles,
                  lua_State* state,
                  const std::string_view path,
                  const openwow::ui::xml::CXMLNode& node,
                  openwow::ui::game::UiLoadStatusSink* status) {
  const std::string name = Attribute(node, "name");
  if (name.empty()) {
    Report(status, 1, "Found binding with no name in " + std::string(path));
    return;
  }
  if (AttributeBoolean(node, "debug") ||
      !PlatformMatches(FindAttribute(node, "platform"))) {
    return;
  }
  if (profiles.HasBindingDefinition(BindingCommand(name))) {
    Report(status, 1, "Binding " + name + " is defined more than once in " +
                          std::string(path));
    return;
  }

  const std::string header = Attribute(node, "header");
  if (!header.empty()) {
    const BindingCommand header_command("HEADER_" + header);
    if (profiles.HasBindingDefinition(header_command)) {
      Report(status, 1, "Binding header " + header +
                            " is defined more than once in " +
                            std::string(path));
    } else {
      BindingDefinition definition(header_command);
      definition.display_index = profiles.GetNumBindings();
      definition.is_header = true;
      profiles.RegisterBindingDefinition(std::move(definition));
    }
  }

  BindingDefinition definition{BindingCommand(name)};
  definition.run_on_up = AttributeBoolean(node, "runOnUp");
  definition.requires_pressure = AttributeBoolean(node, "pressure");
  definition.requires_angle = AttributeBoolean(node, "angle");
  const bool hidden =
      AttributeBoolean(node, "hidden") ||
      (AttributeBoolean(node, "joystick") &&
       !profiles.HasBindingDefinitionJoystick());
  definition.display_index =
      hidden ? -(profiles.GetNumHiddenBindings() + 1)
             : profiles.GetNumBindings();
  const std::string body = TrimBody(
      node.text == nullptr ? std::string{}
                           : std::string(node.text, node.text_size));
  profiles.RegisterBindingDefinition(std::move(definition));
  if (!body.empty()) {
    const int reference =
        ::openwow::game::actions::bindings::adapters::lua::
            CompileBindingScript(state, name, body);
    auto script =
        std::make_shared<CompiledBindingScript>(state, reference);
    profiles.RegisterBindingScript(
        BindingCommand(name),
        [script = std::move(script)](const BindingInvocation& invocation) {
          script->Execute(invocation);
        });
  }

  const std::string default_chord = Attribute(node, "default");
  if (!default_chord.empty()) {
    profiles.AddParsedDefaultBinding(BindingChord(default_chord),
                                     BindingCommand(name));
  }
}

void ParseModifiedClick(
    BindingProfiles& profiles,
    const std::string_view path,
    const openwow::ui::xml::CXMLNode& node,
    openwow::ui::game::UiLoadStatusSink* status) {
  const std::string action = Attribute(node, "action");
  if (action.empty()) {
    Report(status, 1,
           "Found modified click with no action in " + std::string(path));
    return;
  }
  const ModifiedClickAction typed_action(action);
  if (profiles.HasModifiedClickDefinition(typed_action)) {
    Report(status, 1, "Modified click " + action +
                          " is defined more than once in " +
                          std::string(path));
    return;
  }
  const auto state =
      ::openwow::game::actions::bindings::adapters::retail::
          ParseModifiedClickBinding(Attribute(node, "default"), true);
  profiles.RegisterModifiedClickDefinition(
      typed_action, state.value_or(ModifiedClickBindingState{}));
}

[[nodiscard]] bool ParseTree(
    BindingProfiles& profiles,
    lua_State* state,
    const std::string_view path,
    const openwow::ui::xml::CXMLTree& tree,
    openwow::ui::game::UiLoadStatusSink* status) {
  if (tree.root == nullptr) {
    Report(status, 2, "Couldn't parse XML in " + std::string(path));
    return false;
  }
  for (auto* node = tree.root->first_child; node != nullptr;
       node = node->right_sibling) {
    if (openwow::text::EqualsIgnoreCaseAscii(node->tag, "Binding")) {
      ParseBinding(profiles, state, path, *node, status);
    } else if (openwow::text::EqualsIgnoreCaseAscii(
                   node->tag, "ModifiedClick")) {
      ParseModifiedClick(profiles, path, *node, status);
    } else {
      Report(status, 1,
             "Unknown node type " + node->tag + " in " + std::string(path));
    }
  }
  return true;
}

}

bool BindingXmlAdapter::LoadFile(
    BindingProfiles& profiles,
    lua_State* state,
    const openwow::vfs::VirtualFileSystem* vfs,
    const std::string_view path,
    openwow::ui::game::UiLoadStatusSink* status,
    openwow::core::MD5Context* digest) {
  if (vfs == nullptr) {
    Report(status, 2, "Couldn't open " + std::string(path));
    return false;
  }
  auto result =
      openwow::ui::xml::LoadXmlTreeFile(*vfs, std::string(path), digest);
  if (!result.ok()) {
    Report(status, 2, result.error_message);
    return false;
  }
  const bool parsed =
      ParseTree(profiles, state, path, *result.tree, status);
  openwow::ui::xml::XMLTree_Free(result.tree);
  return parsed;
}

bool BindingXmlAdapter::LoadText(
    BindingProfiles& profiles,
    lua_State* state,
    const std::string_view path,
    const std::string_view text,
    openwow::ui::game::UiLoadStatusSink* status) {
  auto* tree = openwow::ui::xml::XMLTree_Parse(text.data(), text.size());
  if (tree == nullptr) {
    Report(status, 2, "Couldn't parse XML in " + std::string(path));
    return false;
  }
  const bool parsed = ParseTree(profiles, state, path, *tree, status);
  openwow::ui::xml::XMLTree_Free(tree);
  return parsed;
}

}
