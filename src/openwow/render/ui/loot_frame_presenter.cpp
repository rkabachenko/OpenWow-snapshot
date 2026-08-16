
#include "openwow/ui/game/loot_frame.h"

#include "openwow/game/inventory/loot/adapters/protocol/loot_packet_codec.h"
#include "openwow/game/object_guid.h"
#include "openwow/foundation/diagnostics/logging.h"

#include "openwow/render/ui/ui_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace openwow::ui::game {

using namespace openwow::game;

struct LootFrame::RenderState {
  openwow::render::ui::UiRenderer ui_renderer;
};

LootFrame::LootFrame() : render_(std::make_unique<RenderState>()) {}
LootFrame::~LootFrame() { Shutdown(); }

bool LootFrame::Initialize() {
  if (initialized_) return true;

  if (!render_->ui_renderer.Init()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "LootFrame: shader/texture creation failed");
    Shutdown();
    return false;
  }

  initialized_ = true;
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo, "LootFrame: initialized");
  return true;
}

void LootFrame::Shutdown() {
  render_->ui_renderer.Shutdown();
  initialized_ = false;
  visible_ = false;
  items_.clear();
}

void LootFrame::ShowLoot(std::uint64_t loot_guid,
                         const std::vector<LootFrameItem>& items,
                         std::uint32_t gold) {
  loot_guid_ = loot_guid;
  items_ = items;
  gold_ = gold;
  visible_ = true;
}

void LootFrame::Hide() {
  visible_ = false;
  items_.clear();
  gold_ = 0;
  loot_guid_ = 0;
}

void LootFrame::LootItem(std::uint32_t slot_index) {
  if (!send_fn_) return;
  auto pkt = loot_protocol::EncodeTakeItemRequest(
      static_cast<std::uint8_t>(slot_index));
  send_fn_(pkt);

  for (auto& item : items_) {
    if (item.index == slot_index) {
      item.looted = true;
      break;
    }
  }
}

void LootFrame::LootAll() {
  for (auto& item : items_) {
    if (!item.looted) {
      LootItem(item.index);
    }
  }
}

void LootFrame::Close() {
  if (!send_fn_ || loot_guid_ == 0) return;
  auto pkt = loot_protocol::EncodeReleaseRequest(ObjectGuid(loot_guid_));
  send_fn_(pkt);
  Hide();
}

void LootFrame::RemoveItem(std::uint32_t slot_index) {
  items_.erase(
      std::remove_if(items_.begin(), items_.end(),
                     [slot_index](const LootFrameItem& i) {
                       return i.index == slot_index;
                     }),
      items_.end());

  if (items_.empty() && gold_ == 0) {
    Close();
  }
}

void LootFrame::Update(float ) {

}

void LootFrame::Render(std::uint16_t view, std::uint16_t screen_width,
                       std::uint16_t screen_height) {
  if (!visible_ || screen_width == 0 || screen_height == 0)
    return;

  if (!initialized_ && !Initialize())
    return;

  const float sw = static_cast<float>(screen_width);
  const float sh = static_cast<float>(screen_height);

  render_->ui_renderer.Begin(view, screen_width, screen_height);

  RecalcLayout(sw, sh);

  RenderBackground();
  RenderItems();
  if (gold_ > 0) {
    RenderGold();
  }
  RenderCloseButton();
  render_->ui_renderer.End();
}

bool LootFrame::HandleClick(float mouse_x, float mouse_y) {
  if (!visible_) return false;

  if (HitTestClose(mouse_x, mouse_y)) {
    Close();
    return true;
  }

  int item_idx = HitTestItem(mouse_x, mouse_y);
  if (item_idx >= 0 && item_idx < static_cast<int>(items_.size())) {
    if (!items_[item_idx].looted) {
      LootItem(items_[item_idx].index);
    }
    return true;
  }

  if (mouse_x >= frame_x_ && mouse_x <= frame_x_ + frame_w_ &&
      mouse_y >= frame_y_ && mouse_y <= frame_y_ + frame_h_) {
    return true;
  }

  return false;
}

void LootFrame::RecalcLayout(float screen_w, float ) {
  frame_w_ = kFrameWidth;

  const float items_height = static_cast<float>(items_.size()) * kItemHeight;
  const float gold_height = gold_ > 0 ? kGoldRowHeight : 0.0f;
  frame_h_ = kHeaderHeight + items_height + gold_height + kPadding * 2.0f;

  frame_x_ = screen_w - frame_w_ - 20.0f;
  frame_y_ = 120.0f;
}

void LootFrame::RenderQuad(float x, float y, float w,
                           float h, std::uint32_t color) {
  openwow::render::ui::Quad quad;
  quad.x = x;
  quad.y = y;
  quad.w = w;
  quad.h = h;
  quad.abgr = color;
  quad.blend = openwow::render::ui::BlendMode::kAlpha;
  static_cast<void>(render_->ui_renderer.SubmitSolid(quad));
}

void LootFrame::RenderBackground() {

  constexpr std::uint32_t kBgColor = 0xD0201008u;
  RenderQuad(frame_x_, frame_y_, frame_w_, frame_h_, kBgColor);

  constexpr std::uint32_t kHeaderColor = 0xE0403020u;
  RenderQuad(frame_x_, frame_y_, frame_w_, kHeaderHeight, kHeaderColor);
}

void LootFrame::RenderItems() {
  float y = frame_y_ + kHeaderHeight + kPadding;

  for (const auto& item : items_) {
    if (item.looted) {

      constexpr std::uint32_t kLootedColor = 0x40404040u;
      RenderQuad(frame_x_ + kPadding, y,
                 frame_w_ - kPadding * 2.0f, kItemHeight - 2.0f, kLootedColor);
    } else {

      const std::uint32_t q_color = quality_color::ForQuality(item.quality);
      RenderQuad(frame_x_ + kPadding, y, 4.0f, kItemHeight - 2.0f,
                 q_color);

      constexpr std::uint32_t kItemBg = 0x60302010u;
      RenderQuad(frame_x_ + kPadding + 4.0f, y,
                 frame_w_ - kPadding * 2.0f - 4.0f, kItemHeight - 2.0f,
                 kItemBg);
    }

    y += kItemHeight;
  }
}

void LootFrame::RenderGold() {
  const float y = frame_y_ + kHeaderHeight + kPadding +
                  static_cast<float>(items_.size()) * kItemHeight;

  constexpr std::uint32_t kGoldColor = 0xFF00C8FFu;
  RenderQuad(frame_x_ + kPadding, y + 2.0f, 14.0f, 14.0f, kGoldColor);

  constexpr std::uint32_t kGoldBg = 0x40302010u;
  RenderQuad(frame_x_ + kPadding + 18.0f, y,
             frame_w_ - kPadding * 2.0f - 18.0f, kGoldRowHeight, kGoldBg);
}

void LootFrame::RenderCloseButton() {

  constexpr std::uint32_t kCloseColor = 0xFF0000CCu;
  const float bx = frame_x_ + frame_w_ - kCloseButtonSize - 3.0f;
  const float by = frame_y_ + 3.0f;
  RenderQuad(bx, by, kCloseButtonSize, kCloseButtonSize, kCloseColor);
}

int LootFrame::HitTestItem(float mouse_x, float mouse_y) const {
  const float items_top = frame_y_ + kHeaderHeight + kPadding;
  const float items_left = frame_x_ + kPadding;
  const float items_right = frame_x_ + frame_w_ - kPadding;

  if (mouse_x < items_left || mouse_x > items_right) return -1;
  if (mouse_y < items_top) return -1;

  const float relative_y = mouse_y - items_top;
  const int idx = static_cast<int>(relative_y / kItemHeight);

  if (idx < 0 || idx >= static_cast<int>(items_.size())) return -1;
  return idx;
}

bool LootFrame::HitTestClose(float mouse_x, float mouse_y) const {
  const float bx = frame_x_ + frame_w_ - kCloseButtonSize - 3.0f;
  const float by = frame_y_ + 3.0f;
  return mouse_x >= bx && mouse_x <= bx + kCloseButtonSize &&
         mouse_y >= by && mouse_y <= by + kCloseButtonSize;
}

}
