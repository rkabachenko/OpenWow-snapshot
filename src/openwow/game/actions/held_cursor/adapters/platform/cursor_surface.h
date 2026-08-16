
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <SDL2/SDL.h>

#include "openwow/game/quest_types.h"
#include "openwow/game/unit_defines.h"
#include "openwow/render/ui/cursor_overlay_renderer.h"
#include "openwow/render/resources/textures/texture_lease.h"

namespace openwow::render {
class TextureManager;
}

namespace openwow::game {

namespace detail {

inline constexpr std::size_t kCursor32x32PixelBytes = 32u * 32u * 4u;

[[nodiscard]] bool CopyOrDownsampleCursorRgba32x32(
    std::span<const std::uint8_t> source_rgba,
    std::uint32_t width,
    std::uint32_t height,
    std::array<std::uint8_t, kCursor32x32PixelBytes>* destination);

void ComposeHeldCursorRgba32x32(
    const std::array<std::uint8_t, kCursor32x32PixelBytes>& item_cursor,
    const std::array<std::uint8_t, kCursor32x32PixelBytes>& runtime_image,
    std::array<std::uint8_t, kCursor32x32PixelBytes>* destination);

}

enum class CursorType : std::uint8_t {
  kDefault = 0,
  kAttack,
  kCast,
  kItem,
  kLoot,
  kMail,
  kMine,
  kSkin,
  kRepair,
  kSpeak,
  kQuest,
  kQuestComplete,
  kTrainer,
  kTaxi,
  kInnkeeper,
  kGather,
  kInspect,
  kPickLock,
  kUnableAttack,
  kBuy,
  kPickUp,
  kInteract,
  kQuestRepeatable,
  kCount
};

struct RetailCursorRequest {
  std::uint32_t retail_type = 0;
  std::string custom_texture_path;

  [[nodiscard]] bool UsesCustomTexture() const noexcept {
    return retail_type == 53u && !custom_texture_path.empty();
  }
};

enum NpcFlags : std::uint32_t {
  UNIT_NPC_FLAG_GOSSIP       = 0x00000001,
  UNIT_NPC_FLAG_QUESTGIVER   = 0x00000002,
  UNIT_NPC_FLAG_TRAINER      = 0x00000010,
  UNIT_NPC_FLAG_VENDOR       = 0x00000080,
  UNIT_NPC_FLAG_REPAIR       = 0x00001000,
  UNIT_NPC_FLAG_FLIGHTMASTER = 0x00002000,
  UNIT_NPC_FLAG_INNKEEPER    = 0x00010000,
  UNIT_NPC_FLAG_BANKER       = 0x00020000,
  UNIT_NPC_FLAG_AUCTIONEER   = 0x00200000,

  UNIT_NPC_FLAG_CIVILIAN     = 0x04000000,
};

inline constexpr std::uint32_t UNIT_DYNFLAG_LOOTABLE =
    kUnitDynFlagLootable;

class CursorSurface {
 public:
  explicit CursorSurface(openwow::render::TextureManager& texture_manager)
      : texture_manager_(texture_manager), software_renderer_(texture_manager) {}

  bool Initialize(SDL_Window* window);

  void Shutdown();

  [[nodiscard]] bool IsInitialized() const { return window_ != nullptr; }

  void ReleaseRendererDeviceResources();

  void SetCursor(CursorType type);

  void SetImmediateCursorType(std::uint32_t retail_type);

  void SetBaseCursor(CursorType type);

  void SetBaseRetailCursorType(std::uint32_t retail_type);

  void ResetCursor();

  void RestoreBaseCursor();

  [[nodiscard]] CursorType GetCursorType() const { return current_; }
  [[nodiscard]] CursorType GetBaseCursorType() const { return base_cursor_; }
  [[nodiscard]] std::uint32_t GetRetailCursorType() const {
    return current_retail_type_;
  }
  [[nodiscard]] std::uint32_t GetBaseRetailCursorType() const {
    return base_retail_type_;
  }

  bool SetCursorFromLua(const std::string& cursor_name);

  void SetCursorFromRetailStem(std::string_view stem, bool unavailable);

  void ShowCursor(bool show);

  void ReassertPresentation();

  using FileReader = std::function<std::optional<std::vector<std::uint8_t>>(const std::string&)>;

  void SetFileReader(FileReader reader);

  [[nodiscard]] bool SetRuntimeCursorTexture(std::string_view texture_path);

  void ClearRuntimeCursorTexture();

  void ResetRuntimeCursorTextureState();

  void PreloadCursorTextures();

  void RenderOverlay(std::uint8_t view_id, float screen_w, float screen_h);

  [[nodiscard]] bool WantsHardwareCursor() const;

  [[nodiscard]] bool ShouldRenderSoftwareCursor() const;

  [[nodiscard]] std::string GetActiveCursorTexturePath() const;

  [[nodiscard]] std::pair<int, int> GetActiveCursorHotspot() const;

  [[nodiscard]] bool IsVisible() const { return visible_; }

 private:
  SDL_Window* window_ = nullptr;
  FileReader file_reader_;
  CursorType base_cursor_ = CursorType::kDefault;
  CursorType current_ = CursorType::kDefault;
  std::uint32_t base_retail_type_ = 1u;
  std::uint32_t current_retail_type_ = 1u;
  std::string active_cursor_path_{"Interface/Cursor/Point.blp"};
  std::pair<int, int> active_hotspot_{0, 0};
  bool transient_override_active_ = false;
  bool custom_cursor_active_ = false;
  std::string cached_custom_cursor_request_;
  std::array<std::uint8_t, detail::kCursor32x32PixelBytes> custom_cursor_rgba_{};
  bool visible_ = true;

  struct PublishedBuiltInCursor {
    SDL_Cursor* handle = nullptr;
    std::string texture_path;
    std::pair<int, int> hotspot{0, 0};
  };
  PublishedBuiltInCursor published_builtin_cursor_;
  std::unordered_map<std::uint32_t, openwow::render::TextureLease>
      software_cursor_texture_leases_;
  SDL_Cursor* custom_cursor_handle_ = nullptr;
  bool custom_cursor_texture_dirty_ = true;
  bool runtime_cursor_enabled_ = false;
  bool runtime_cursor_pixels_valid_ = false;
  std::string runtime_requested_texture_path_;
  std::string runtime_visible_cursor_path_;
  std::pair<int, int> runtime_visible_hotspot_{0, 0};
  std::array<std::uint8_t, detail::kCursor32x32PixelBytes>
      runtime_item_cursor_rgba_{};
  bool runtime_item_cursor_prepared_ = false;
  std::array<std::uint8_t, detail::kCursor32x32PixelBytes>
      runtime_cursor_scratch_rgba_{};
  std::array<std::uint8_t, detail::kCursor32x32PixelBytes>
      runtime_cursor_rgba_{};
  SDL_Cursor* runtime_cursor_handle_ = nullptr;
  bool runtime_cursor_texture_dirty_ = true;
  openwow::render::TextureManager& texture_manager_;
  openwow::render::CursorOverlayRenderer software_renderer_;

  SDL_Cursor* CreateCursorFromBLP(const std::string& blp_path,
                                  int hotspot_x, int hotspot_y);
  SDL_Cursor* CreateCursorFromCustomPixels(int hotspot_x, int hotspot_y);
  SDL_Cursor* CreateCursorFromPixels(
      std::array<std::uint8_t, detail::kCursor32x32PixelBytes>& pixels,
      int hotspot_x, int hotspot_y);

  void FreeCursorHandle(SDL_Cursor*& handle);

  [[nodiscard]] SDL_Cursor* DetachPublishedBuiltInCursor();

  static std::string CursorTypeToBlpName(CursorType type);

  static CursorType LuaNameToCursorType(const std::string& name);

  static std::pair<int, int> CursorTypeToHotspot(CursorType type);

  static std::string CursorTypeToTexturePath(CursorType type);

  static bool TryResolveLuaCursorToken(std::string_view name,
                                       std::string* texture_path,
                                       std::pair<int, int>* hotspot);

  static bool TryResolveRetailCursorType(std::uint32_t retail_type,
                                         std::string* texture_path,
                                         std::pair<int, int>* hotspot,
                                         bool* uses_blank_pixels);

  void ApplyHardwareCursor();

  void ActivateBuiltInCursor(const std::string& texture_path,
                             std::pair<int, int> hotspot);

  void ActivateBlankCursor(CursorType semantic_type, bool transient_override);

  bool LoadCustomCursorPixels(const std::string& cursor_path,
                              SDL_Cursor** stale_handle);

  bool DecodeCursorPixels(
      std::string_view cursor_path,
      std::array<std::uint8_t, detail::kCursor32x32PixelBytes>* destination,
      bool* image_loaded) const;

  void PrepareRuntimeItemCursor();
  void CaptureRuntimeCursorPresentation();
  void PublishRuntimeCursorScratch();

  void ActivateCustomCursor();

  void DestroyCustomCursorResources();

  void RefreshPresentationMode();
};

void SetActiveCursorSurface(CursorSurface* manager);

[[nodiscard]] CursorSurface* GetActiveCursorSurface();

[[nodiscard]] std::uint32_t FindRetailCursorTypeByStem(std::string_view stem);

[[nodiscard]] RetailCursorRequest ResolveRetailCursorRequestFromStem(
    std::string_view stem, bool unavailable);

[[nodiscard]] std::uint32_t ResolveQuestGiverRetailCursorType(
    QuestGiverStatus status, bool interaction_blocked);

}
