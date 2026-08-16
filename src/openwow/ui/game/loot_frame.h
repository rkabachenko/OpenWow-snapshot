#pragma once

#include "openwow/network/protocol/wotlk/world_packet.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace openwow::ui::game {

struct LootFrameItem {
  std::uint32_t index{0};
  std::uint32_t item_id{0};
  std::uint32_t count{0};
  std::uint32_t display_id{0};
  std::string name;
  std::uint8_t quality{0};
  bool looted{false};
};

namespace quality_color {
constexpr std::uint32_t kPoor      = 0xFF808080u;
constexpr std::uint32_t kCommon    = 0xFFFFFFFFu;
constexpr std::uint32_t kUncommon  = 0xFF00FF1Eu;
constexpr std::uint32_t kRare      = 0xFFFF4400u;
constexpr std::uint32_t kEpic      = 0xFFBE00A3u;
constexpr std::uint32_t kLegendary = 0xFF0070FFu;
constexpr std::uint32_t kArtifact  = 0xFF00CCE5u;

inline std::uint32_t ForQuality(std::uint8_t q) {
  switch (q) {
    case 0: return kPoor;
    case 1: return kCommon;
    case 2: return kUncommon;
    case 3: return kRare;
    case 4: return kEpic;
    case 5: return kLegendary;
    case 6: return kArtifact;
    default: return kCommon;
  }
}
}

class LootFrame {
 public:
  LootFrame();
  ~LootFrame();

  LootFrame(const LootFrame&) = delete;
  LootFrame& operator=(const LootFrame&) = delete;

  bool Initialize();

  void Shutdown();

  void ShowLoot(std::uint64_t loot_guid,
                const std::vector<LootFrameItem>& items,
                std::uint32_t gold);

  void Hide();

  void LootItem(std::uint32_t slot_index);

  void LootAll();

  void Close();

  void Update(float dt);
  void Render(std::uint16_t view, std::uint16_t screen_width,
              std::uint16_t screen_height);

  [[nodiscard]] bool IsVisible() const { return visible_; }

  bool HandleClick(float mouse_x, float mouse_y);

  void SetSendFn(
      std::function<void(const openwow::net::wotlk::WorldPacket&)> fn) {
    send_fn_ = std::move(fn);
  }

  void RemoveItem(std::uint32_t slot_index);

 private:

  void RenderBackground();
  void RenderItems();
  void RenderGold();
  void RenderCloseButton();

  void RenderQuad(float x, float y, float w, float h, std::uint32_t color);

  void RecalcLayout(float screen_w, float screen_h);
  int HitTestItem(float mouse_x, float mouse_y) const;
  bool HitTestClose(float mouse_x, float mouse_y) const;

  bool visible_{false};
  std::uint64_t loot_guid_{0};
  std::uint32_t gold_{0};
  std::vector<LootFrameItem> items_;

  float frame_x_{0.0f};
  float frame_y_{0.0f};
  float frame_w_{200.0f};
  float frame_h_{0.0f};

  static constexpr float kItemHeight = 28.0f;
  static constexpr float kHeaderHeight = 24.0f;
  static constexpr float kGoldRowHeight = 22.0f;
  static constexpr float kCloseButtonSize = 18.0f;
  static constexpr float kPadding = 6.0f;
  static constexpr float kFrameWidth = 200.0f;

  struct RenderState;
  std::unique_ptr<RenderState> render_;
  bool initialized_{false};

  std::function<void(const openwow::net::wotlk::WorldPacket&)> send_fn_;
};

}
