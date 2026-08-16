#pragma once

#include <SDL2/SDL.h>

#include "openwow/ui/glue/glue_widget_runtime.h"
#include "openwow/ui/glue/glue_font_registry.h"
#include "openwow/vfs/virtual_file_system.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::ui::screens {

struct LoginSceneState {
  std::string status_line;
  bool show_error{false};
};

class LoginScene {
 public:
  ~LoginScene();
  void BindWidgetRuntime(const openwow::ui::glue::GlueWidgetRuntime* runtime);
  void Initialize(const openwow::vfs::VirtualFileSystem& vfs, int width, int height);
  void SetState(const LoginSceneState& state);
  void Render(SDL_Renderer* renderer, int width, int height);

 private:
  SDL_Texture* LoadTexture(SDL_Renderer* renderer, const std::string& path);
  struct FontEntry {
    std::shared_ptr<std::vector<std::uint8_t>> bytes;
    void* face{nullptr};
    std::string path;
    int height_px{0};
    int ascent_px{0};
    int line_height_px{0};
    FontEntry() = default;
    FontEntry(const FontEntry&) = delete;
    FontEntry& operator=(const FontEntry&) = delete;
    FontEntry(FontEntry&& other) noexcept;
    FontEntry& operator=(FontEntry&& other) noexcept;
    ~FontEntry();
  };
  FontEntry* LoadFont(const std::string& font_path, int height_px);
  SDL_Texture* RenderText(SDL_Renderer* renderer,
                          const std::string& cache_key,
                          FontEntry* font,
                          const std::string& text,
                          SDL_Color color,
                          int wrap_px,
                          bool word_wrap,
                          bool non_space_wrap,
                          int* out_w,
                          int* out_h);

  std::vector<SDL_Texture*> loaded_textures_;
  std::vector<std::string> loaded_texture_paths_;
  std::unordered_map<std::string, FontEntry> font_cache_;
  struct TextEntry {
    SDL_Texture* texture{nullptr};
    int w{0};
    int h{0};
    std::string text;
    std::string font_key;
    std::uint32_t rgba{0};
    int wrap_px{0};
    bool word_wrap{true};
    bool non_space_wrap{false};
  };
  std::unordered_map<std::string, TextEntry> text_cache_;
  std::optional<openwow::ui::glue::GlueFontRegistry> font_registry_;
  const openwow::vfs::VirtualFileSystem* vfs_{nullptr};
  const openwow::ui::glue::GlueWidgetRuntime* widget_runtime_{nullptr};
  LoginSceneState state_{};
  bool logged_runtime_empty_draw_{false};
  bool logged_draw_dump_{false};
};

}
