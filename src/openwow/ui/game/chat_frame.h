#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace openwow::game {
class ChatManager;
struct ChatMessage;
}

namespace openwow::ui::game {

enum class ChatMode : std::uint8_t {
  kSay     = 0,
  kParty   = 1,
  kGuild   = 2,
  kWhisper = 3,
  kYell    = 4,
  kRaid    = 5,
};

class ChatFrame {
 public:
  ChatFrame();
  ~ChatFrame();

  ChatFrame(const ChatFrame&) = delete;
  ChatFrame& operator=(const ChatFrame&) = delete;

  bool Initialize();

  void Shutdown();

  void AddMessage(std::uint8_t type, const std::string& sender,
                  const std::string& message, std::uint32_t language = 0,
                  const std::string& channel = "");

  void AddSystemMessage(const std::string& message);

  void AddErrorMessage(const std::string& message);

  void AddCombatMessage(const std::string& message);

  void ActivateInput();

  void DeactivateInput();

  [[nodiscard]] bool IsInputActive() const { return input_active_; }

  void OnTextInput(const char* text);

  bool OnKeyDown(std::uint32_t key);

  void SendMessage();

  void SetChatMode(ChatMode mode) { chat_mode_ = mode; }

  [[nodiscard]] ChatMode chat_mode() const { return chat_mode_; }

  void SetWhisperTarget(const std::string& name) { whisper_target_ = name; }

  using SendFn = std::function<void(std::uint8_t, const std::string&, const std::string&)>;
  void SetSendCallback(SendFn fn) { send_fn_ = std::move(fn); }

  void Render(std::uint8_t view_id, float screen_w, float screen_h);

  void Update(float dt);

 private:

  struct ChatLine {
    std::string formatted_text;
    std::uint32_t color;
    float timestamp;
    float alpha;
  };

  std::vector<ChatLine> lines_;
  std::string input_text_;
  bool input_active_{false};
  ChatMode chat_mode_{ChatMode::kSay};
  std::string whisper_target_;
  float scroll_offset_{0.0f};
  float elapsed_time_{0.0f};

  SendFn send_fn_;

  struct RenderState;
  std::unique_ptr<RenderState> render_;
  bool initialized_{false};

  static constexpr std::size_t kMaxLines = 200;
  static constexpr float kFadeStartTime = 10.0f;
  static constexpr float kFadeDuration = 5.0f;
  static constexpr float kChatBoxHeight = 120.0f;

  static constexpr float kChatBoxWidth = 430.0f;

  static constexpr float kMargin = 10.0f;
  static constexpr float kInputBarHeight = 24.0f;
  static constexpr float kLineSpacing = 2.0f;

  [[nodiscard]] static std::uint32_t GetColorForType(std::uint8_t type);

  [[nodiscard]] static std::string GetPrefix(std::uint8_t type,
                                              const std::string& sender,
                                              const std::string& channel);

  [[nodiscard]] std::string GetModeLabel() const;

  void DrawQuad(float x, float y, float w, float h, std::uint32_t abgr);

};

}
