#pragma once

#include "openwow/ui/framexml/ui_frame.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>

struct lua_State;

namespace openwow::ui::game {
namespace runtime {
class FrameStore;
class FrameTraversalIndex;
class RetainedLayout;
}

struct EditBoxRegionPorts {
  runtime::FrameStore *frames{nullptr};
  runtime::RetainedLayout *layout{nullptr};
  runtime::FrameTraversalIndex *traversal{nullptr};

  bool focused{false};

  bool application_active{true};
};

void QueueEditBoxDirtyState(lua_State *state, int edit_box_index,
                            bool text_changed, bool user_input,
                            bool cursor_changed);

void FlushEditBoxDirtyState(lua_State *state, int edit_box_index,
                            std::string_view edit_box_key = {},
                            const EditBoxRegionPorts &ports = {});

void UpdateEditBoxCaretBlink(lua_State *state, int edit_box_index,
                             std::string_view edit_box_key, float dt,
                             const EditBoxRegionPorts &ports);

void CreateEditBoxCaretRegions(std::string_view edit_box_key,
                               runtime::FrameStore &frames,
                               runtime::RetainedLayout &layout);

[[nodiscard]] std::string EditBoxCaretRegionKey(std::string_view edit_box_key);
[[nodiscard]] std::string EditBoxHighlightRegionKey(
    std::string_view edit_box_key, std::size_t index);
inline constexpr std::size_t kEditBoxHighlightRegionCount = 3;

[[nodiscard]] std::optional<int> ResolveEditBoxCharacterIndexAtPoint(
    lua_State *state, int edit_box_index, std::string_view edit_box_key,
    float device_x, float device_y, runtime::RetainedLayout &layout);

void SynchronizeEditBoxRetainedTextInsets(
    lua_State *state, int edit_box_index, const std::string &edit_box_name,
    openwow::ui::framexml::UiFrame *edit_box,
    runtime::FrameStore *frames);

}
