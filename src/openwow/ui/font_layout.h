#pragma once

#include "openwow/render/resources/fonts/text_layout.h"
#include "openwow/vfs/virtual_file_system.h"

#include <optional>
#include <string_view>

namespace openwow::ui {

[[nodiscard]] std::optional<openwow::render::text::TextLayout> LayoutFontText(
    const openwow::vfs::VirtualFileSystem* vfs, std::string_view font_path,
    int pixel_height, std::string_view text,
    const openwow::render::text::TextLayoutRequest& request = {},
    float scale = 1.0f,
    openwow::render::text::FontStyle style = {});

}
