#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace openwow::render {

[[nodiscard]] constexpr std::string_view ResolveQuestOverlayModelPath(
    const std::uint32_t model_cache_index) noexcept {

  constexpr std::array<std::string_view, 12> kModels{
      "",
      "Interface\\Buttons\\TalkToMe.mdx",
      "Interface\\Buttons\\TalkToMeQuestion_LTBlue.mdx",
      "Interface\\Buttons\\TalkToMeBlue.mdx",
      "Interface\\Buttons\\TalkToMe.mdx",
      "Interface\\Buttons\\TalkToMeQuestion_Grey.mdx",
      "Interface\\Buttons\\TalkToMeGrey.mdx",
      "Interface\\Buttons\\TalkToMeGreen.mdx",
      "",
      "Interface\\Buttons\\TalkToMeQuestionMark.mdx",
      "Interface\\Buttons\\TalkToMeQuestion_LTBlue.mdx",
      "Interface\\Buttons\\TalkToMeBlue.mdx",
  };
  return model_cache_index < kModels.size() ? kModels[model_cache_index]
                                            : std::string_view{};
}

}
