#pragma once

#include <cstdint>
#include <string_view>

struct lua_State;

namespace openwow::core {
struct MD5Context;
}
namespace openwow::game {
class BindingProfiles;
}
namespace openwow::ui::game {
class UiLoadStatusSink;
}
namespace openwow::vfs {
class VirtualFileSystem;
}

namespace openwow::game::actions::bindings::adapters::xml {

class BindingXmlAdapter {
 public:
  [[nodiscard]] static bool LoadFile(
      BindingProfiles& profiles,
      lua_State* state,
      const openwow::vfs::VirtualFileSystem* vfs,
      std::string_view path,
      openwow::ui::game::UiLoadStatusSink* status = nullptr,
      openwow::core::MD5Context* digest = nullptr);

  [[nodiscard]] static bool LoadText(
      BindingProfiles& profiles,
      lua_State* state,
      std::string_view path,
      std::string_view text,
      openwow::ui::game::UiLoadStatusSink* status = nullptr);
};

}
