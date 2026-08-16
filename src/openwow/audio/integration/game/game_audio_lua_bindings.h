#pragma once

namespace openwow::ui::lua {
struct NativeBindingCatalog;
}

namespace openwow::audio::integration::lua {

[[nodiscard]] openwow::ui::lua::NativeBindingCatalog
SharedSoundVoiceChatNativeBindingCatalog();
[[nodiscard]] openwow::ui::lua::NativeBindingCatalog
WorldMusicVoiceChatNativeBindingCatalog();

}
