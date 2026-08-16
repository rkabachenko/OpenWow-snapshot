
#include "openwow/ui/game/chat_frame.h"

#include "openwow/render/ui/text_renderer.h"
#include "openwow/render/ui/ui_renderer.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <SDL2/SDL_keyboard.h>
#include <SDL2/SDL_keycode.h>

#include <algorithm>
#include <cmath>

namespace openwow::ui::game {

struct ChatFrame::RenderState {
  openwow::render::ui::TextRenderer text_renderer;
  openwow::render::ui::UiRenderer ui_renderer;
};

ChatFrame::ChatFrame() : render_(std::make_unique<RenderState>()) {}

ChatFrame::~ChatFrame() = default;

bool ChatFrame::Initialize() {
  if (initialized_)
    return true;

  if (!render_->text_renderer.Init(14)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, "ChatFrame: text renderer init failed");
    return false;
  }
  if (!render_->ui_renderer.Init()) {
    render_->text_renderer.Shutdown();
    return false;
  }

  initialized_ = true;
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo, "ChatFrame: initialized");
  return true;
}

void ChatFrame::Shutdown() {
  render_->text_renderer.Shutdown();
  render_->ui_renderer.Shutdown();
  lines_.clear();
  input_text_.clear();
  initialized_ = false;
}

void ChatFrame::AddMessage(std::uint8_t type, const std::string &sender, const std::string &message,
                           std::uint32_t , const std::string &channel) {
  ChatLine line;
  line.formatted_text = GetPrefix(type, sender, channel) + message;
  line.color = GetColorForType(type);
  line.timestamp = elapsed_time_;
  line.alpha = 1.0f;

  lines_.push_back(std::move(line));
  if (lines_.size() > kMaxLines) {
    lines_.erase(lines_.begin());
  }
}

void ChatFrame::AddSystemMessage(const std::string &message) {
  ChatLine line;
  line.formatted_text = message;
  line.color = 0xFFFFFF00;
  line.timestamp = elapsed_time_;
  line.alpha = 1.0f;
  lines_.push_back(std::move(line));
  if (lines_.size() > kMaxLines) {
    lines_.erase(lines_.begin());
  }
}

void ChatFrame::AddErrorMessage(const std::string &message) {
  ChatLine line;
  line.formatted_text = message;
  line.color = 0xFFFF0000;
  line.timestamp = elapsed_time_;
  line.alpha = 1.0f;
  lines_.push_back(std::move(line));
  if (lines_.size() > kMaxLines) {
    lines_.erase(lines_.begin());
  }
}

void ChatFrame::AddCombatMessage(const std::string &message) {
  ChatLine line;
  line.formatted_text = message;
  line.color = 0xFFFF8040;
  line.timestamp = elapsed_time_;
  line.alpha = 1.0f;
  lines_.push_back(std::move(line));
  if (lines_.size() > kMaxLines) {
    lines_.erase(lines_.begin());
  }
}

void ChatFrame::ActivateInput() {
  input_active_ = true;
  SDL_StartTextInput();
}

void ChatFrame::DeactivateInput() {
  input_active_ = false;
  input_text_.clear();
  SDL_StopTextInput();
}

void ChatFrame::OnTextInput(const char *text) {
  if (!input_active_ || text == nullptr)
    return;
  input_text_ += text;
}

bool ChatFrame::OnKeyDown(std::uint32_t key) {
  if (!input_active_)
    return false;

  if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
    if (!input_text_.empty()) {
      SendMessage();
    }
    DeactivateInput();
    return true;
  }

  if (key == SDLK_ESCAPE) {
    DeactivateInput();
    return true;
  }

  if (key == SDLK_BACKSPACE) {
    if (!input_text_.empty()) {

      auto it = input_text_.end();
      --it;
      while (it != input_text_.begin() && (*it & 0xC0) == 0x80) {
        --it;
      }
      input_text_.erase(it, input_text_.end());
    }
    return true;
  }

  return false;
}

void ChatFrame::SendMessage() {
  if (input_text_.empty())
    return;

  std::uint8_t type = 0x01;
  std::string target;
  switch (chat_mode_) {
  case ChatMode::kSay:
    type = 0x01;
    break;
  case ChatMode::kParty:
    type = 0x02;
    break;
  case ChatMode::kGuild:
    type = 0x04;
    break;
  case ChatMode::kWhisper:
    type = 0x07;
    target = whisper_target_;
    break;
  case ChatMode::kYell:
    type = 0x06;
    break;
  case ChatMode::kRaid:
    type = 0x03;
    break;
  }

  if (input_text_.size() > 1 && input_text_[0] == '/') {
    const auto space = input_text_.find(' ');
    const auto cmd =
        input_text_.substr(1, space != std::string::npos ? space - 1 : std::string::npos);
    if (cmd == "s" || cmd == "say") {
      type = 0x01;
      if (space != std::string::npos)
        input_text_ = input_text_.substr(space + 1);
      else {
        input_text_.clear();
        return;
      }
    } else if (cmd == "p" || cmd == "party") {
      type = 0x02;
      if (space != std::string::npos)
        input_text_ = input_text_.substr(space + 1);
      else {
        input_text_.clear();
        return;
      }
    } else if (cmd == "g" || cmd == "guild") {
      type = 0x04;
      if (space != std::string::npos)
        input_text_ = input_text_.substr(space + 1);
      else {
        input_text_.clear();
        return;
      }
    } else if (cmd == "y" || cmd == "yell") {
      type = 0x06;
      if (space != std::string::npos)
        input_text_ = input_text_.substr(space + 1);
      else {
        input_text_.clear();
        return;
      }
    } else if (cmd == "w" || cmd == "whisper" || cmd == "tell") {
      type = 0x07;
      if (space != std::string::npos) {
        const auto rest = input_text_.substr(space + 1);
        const auto name_end = rest.find(' ');
        target = rest.substr(0, name_end);
        if (name_end != std::string::npos) {
          input_text_ = rest.substr(name_end + 1);
        } else {
          input_text_.clear();
          return;
        }
      } else {
        input_text_.clear();
        return;
      }
    } else if (cmd == "r" || cmd == "raid") {
      type = 0x03;
      if (space != std::string::npos)
        input_text_ = input_text_.substr(space + 1);
      else {
        input_text_.clear();
        return;
      }
    } else if (cmd == "e" || cmd == "emote") {
      type = 0x0A;
      if (space != std::string::npos)
        input_text_ = input_text_.substr(space + 1);
      else {
        input_text_.clear();
        return;
      }
    }
  }

  if (send_fn_ && !input_text_.empty()) {
    send_fn_(type, input_text_, target);
  }
  input_text_.clear();
}

void ChatFrame::Update(float dt) {
  elapsed_time_ += dt;

  for (auto &line : lines_) {
    const float age = elapsed_time_ - line.timestamp;
    if (age < kFadeStartTime) {
      line.alpha = 1.0f;
    } else if (age < kFadeStartTime + kFadeDuration) {
      line.alpha = 1.0f - (age - kFadeStartTime) / kFadeDuration;
    } else {
      line.alpha = 0.0f;
    }
  }

  if (input_active_) {
    for (auto &line : lines_) {
      line.alpha = std::max(line.alpha, 0.7f);
    }
  }
}

void ChatFrame::Render(std::uint8_t view_id, float screen_w, float screen_h) {

  if (!initialized_ && !Initialize())
    return;

  const float box_x = kMargin;
  const float box_y =
      screen_h - kChatBoxHeight - kMargin - (input_active_ ? kInputBarHeight : 0.0f);
  const float box_w = kChatBoxWidth;
  const float box_h = kChatBoxHeight;

  render_->text_renderer.BeginFrame(view_id, screen_w, screen_h);
  render_->ui_renderer.Begin(view_id, static_cast<int>(screen_w),
                             static_cast<int>(screen_h));

  if (input_active_) {
    DrawQuad(box_x, box_y, box_w, box_h + kInputBarHeight, 0x80000000);
  }

  const float line_h = render_->text_renderer.line_height() + kLineSpacing;
  const float text_x = box_x + 4.0f;
  float text_y = box_y + box_h - line_h;

  const int max_visible = std::max(1, static_cast<int>(box_h / line_h));
  const int start_idx =
      std::max(0, static_cast<int>(lines_.size()) - max_visible - static_cast<int>(scroll_offset_));
  const int end_idx = std::min(static_cast<int>(lines_.size()), start_idx + max_visible);

  for (int i = end_idx - 1; i >= start_idx; --i) {
    const auto &line = lines_[static_cast<std::size_t>(i)];
    if (line.alpha <= 0.01f) {
      text_y -= line_h;
      continue;
    }
    render_->text_renderer.DrawTextAlpha(view_id, text_x, text_y, line.formatted_text, line.color,
                                         line.alpha);
    text_y -= line_h;
  }

  if (input_active_) {
    const float input_y = box_y + box_h;
    DrawQuad(box_x, input_y, box_w, kInputBarHeight, 0xC0000000);

    const std::string label = GetModeLabel();
    const std::string display = label + input_text_ + "_";
    render_->text_renderer.DrawText(view_id, text_x, input_y + 3.0f, display, 0xFFFFFFFF);
  }
  render_->ui_renderer.End();
}

std::uint32_t ChatFrame::GetColorForType(std::uint8_t type) {

  switch (type) {
  case 0x00:
    return 0xFFFFFF00;
  case 0x01:
    return 0xFFFFFFFF;
  case 0x02:
    return 0xFFAAAAFF;
  case 0x03:
    return 0xFFFF7F00;
  case 0x04:
    return 0xFF40FF40;
  case 0x05:
    return 0xFF40FF40;
  case 0x06:
    return 0xFFFF4040;
  case 0x07:
    return 0xFFFF80FF;
  case 0x09:
    return 0xFFFF80FF;
  case 0x0A:
    return 0xFFFF8040;
  case 0x0B:
    return 0xFFFF8040;
  case 0x0C:
    return 0xFFFFFF00;
  case 0x0E:
    return 0xFFFF4040;
  case 0x11:
    return 0xFFFFB0C0;
  case 0x1A:
    return 0xFFFFFF00;
  case 0x1B:
    return 0xFFFFFF00;
  case 0x20:
    return 0xFFFF8040;
  case 0x21:
    return 0xFFFF8040;
  case 0x24:
    return 0xFFFFFF00;
  case 0x25:
    return 0xFF4040FF;
  case 0x26:
    return 0xFFFF4040;
  case 0x27:
    return 0xFFFF7F00;
  case 0x28:
    return 0xFFFF4848;
  case 0x30:
    return 0xFFFFFF00;
  case 0x33:
    return 0xFFAAAAFF;
  default:
    return 0xFFCCCCCC;
  }
}

std::string ChatFrame::GetPrefix(std::uint8_t type, const std::string &sender,
                                 const std::string &channel) {
  switch (type) {
  case 0x11: {
    if (channel.empty()) {
      break;
    }
    if (!sender.empty()) {
      return "[" + channel + "] [" + sender + "]: ";
    }
    return "[" + channel + "] ";
  }
  case 0x01:
    return "[" + sender + "] says: ";
  case 0x02:
    return "[" + sender + "]: ";
  case 0x03:
    return "[" + sender + "]: ";
  case 0x04:
    return "[" + sender + "]: ";
  case 0x05:
    return "[" + sender + "]: ";
  case 0x06:
    return "[" + sender + "] yells: ";
  case 0x07:
    return "[" + sender + "] whispers: ";
  case 0x09:
    return "To [" + sender + "]: ";
  case 0x0A:
    return sender + " ";
  case 0x0B:
    return sender + " ";
  case 0x0C:
    return sender + " says: ";
  case 0x0E:
    return sender + " yells: ";
  case 0x27:
    return "[" + sender + "]: ";
  case 0x28:
    return "[" + sender + "]: ";
  case 0x33:
    return "[" + sender + "]: ";
  default:
    break;
  }

  if (!sender.empty()) {
    return "[" + sender + "]: ";
  }
  return "";
}

std::string ChatFrame::GetModeLabel() const {
  switch (chat_mode_) {
  case ChatMode::kSay:
    return "[Say]: ";
  case ChatMode::kParty:
    return "[Party]: ";
  case ChatMode::kGuild:
    return "[Guild]: ";
  case ChatMode::kWhisper:
    return "[Whisper " + (whisper_target_.empty() ? "?" : whisper_target_) + "]: ";
  case ChatMode::kYell:
    return "[Yell]: ";
  case ChatMode::kRaid:
    return "[Raid]: ";
  default:
    return "[Say]: ";
  }
}

void ChatFrame::DrawQuad(float x, float y, float w, float h,
                         std::uint32_t abgr) {
  if (w <= 0.0f || h <= 0.0f)
    return;
  openwow::render::ui::Quad quad;
  quad.x = x;
  quad.y = y;
  quad.w = w;
  quad.h = h;
  quad.abgr = abgr;
  quad.blend =
      openwow::render::ui::BlendMode::kCoveragePremultiplied;
  static_cast<void>(render_->ui_renderer.SubmitSolid(quad));
}

}
