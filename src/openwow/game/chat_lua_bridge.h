#pragma once

#include <cstdint>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace openwow::game {

class ChatLuaBridge {
 public:
  static ChatLuaBridge& Get();

  struct ChatWindowInfoResult {
    std::string name;
    float fontSize{14.0f};
    float r{1.0f};
    float g{1.0f};
    float b{1.0f};
    float a{1.0f};
    bool shown{true};
    bool locked{false};
    bool docked{true};
    bool uninteractable{false};
  };

  struct ChatTypeInfoResult {
    std::string localizedName;
    float r{1.0f};
    float g{1.0f};
    float b{1.0f};
  };

  struct ChatTypeVisualState {
    std::string token;
    float r{1.0f};
    float g{1.0f};
    float b{1.0f};
    bool colorNameByClass{false};
  };

  struct ChatTypeCacheState {
    std::string token;
    std::uint8_t r{255};
    std::uint8_t g{255};
    std::uint8_t b{255};
    bool colorNameByClass{false};
  };

  struct ChannelEntry {
    std::uint32_t id{0};
    std::string name;
    std::string password;
    std::string header;
    std::uint32_t memberCount{0};
  };

  void SendChatMessage(const std::string& msg, const std::string& chatType,
                       const std::string& language = "",
                       const std::string& channel = "");

  [[nodiscard]] std::uint32_t GetNumChatWindows() const;

  [[nodiscard]] ChatWindowInfoResult GetChatWindowInfo(
      std::uint32_t index) const;

  [[nodiscard]] ChatTypeInfoResult GetChatTypeInfo(
      const std::string& chatType) const;

  [[nodiscard]] std::uint32_t GetChatTypeIndex(
      const std::string& chatType) const;

  std::pair<bool, std::string> JoinChannelByName(
      const std::string& name, const std::string& password = "");

  void LeaveChannelByName(const std::string& name);

  [[nodiscard]] std::pair<std::string, std::string> GetChannelName(
      std::uint32_t channelId) const;

  [[nodiscard]] std::uint32_t GetNumChatGroups() const {
    return numChatGroups_;
  }

  [[nodiscard]] bool IsInGroup(const std::string& chatType) const;

  [[nodiscard]] std::pair<std::string, std::uint32_t> GetDefaultLanguage()
      const;

  [[nodiscard]] std::vector<std::tuple<std::uint32_t, std::string,
                                        std::uint32_t>>
  ListChannels() const;

  [[nodiscard]] std::uint32_t GetNumDisplayMessages(
      std::uint32_t windowIndex) const;

  [[nodiscard]] std::string GetDisplayMessage(std::uint32_t windowIndex,
                                               std::uint32_t msgIndex) const;

  void SetChatWindow(std::uint32_t index, const ChatWindowInfoResult& info);

  void RegisterChatType(const std::string& chatType,
                        const ChatTypeInfoResult& info);

  [[nodiscard]] bool SetChatTypeColor(const std::string& chatType, float r,
                                      float g, float b);

  [[nodiscard]] bool SetChatTypeColorBytes(const std::string& chatType,
                                           std::uint8_t r, std::uint8_t g,
                                           std::uint8_t b);

  void ResetChatColors();

  void ResetChatTypeVisuals();

  [[nodiscard]] std::vector<ChatTypeVisualState> GetChatTypeVisualStates() const;

  [[nodiscard]] std::vector<ChatTypeCacheState> GetChatTypeCacheStates() const;

  [[nodiscard]] bool SetChatColorNameByClass(const std::string& chatType,
                                             bool enabled);

  [[nodiscard]] bool GetChatColorNameByClass(
      const std::string& chatType) const;

  void SetDefaultLanguage(const std::string& name, std::uint32_t id);

  void SetNumChatGroups(std::uint32_t n) { numChatGroups_ = n; }

  void SetInGroup(const std::string& chatType, bool val);

  void AddMessage(std::uint32_t windowIndex, const std::string& msg);

  void Clear();

 private:
  ChatLuaBridge();

  static constexpr std::uint32_t kDefaultNumWindows = 10;

  struct StoredChatType {
    std::string token;
    std::uint8_t r{255};
    std::uint8_t g{255};
    std::uint8_t b{255};
    bool colorNameByClass{false};
  };

  void ResetChatTypeTables();
  [[nodiscard]] static std::string NormalizeChatTypeKey(
      const std::string& chatType);
  [[nodiscard]] const StoredChatType* FindStoredChatType(
      const std::string& chatType) const;
  [[nodiscard]] StoredChatType* FindStoredChatType(
      const std::string& chatType);

  std::unordered_map<std::uint32_t, ChatWindowInfoResult> windows_;
  std::unordered_map<std::string, ChatTypeInfoResult> chatTypes_;
  std::vector<StoredChatType> builtinChatTypes_;
  std::vector<StoredChatType> dynamicChatTypes_;
  std::vector<ChannelEntry> channels_;
  std::uint32_t nextChannelId_{1};
  std::string defaultLangName_{"Common"};
  std::uint32_t defaultLangId_{7};
  std::uint32_t numChatGroups_{0};
  std::unordered_map<std::string, bool> groupFlags_;

  std::unordered_map<std::uint32_t, std::vector<std::string>> messages_;

  std::vector<std::tuple<std::string, std::string, std::string, std::string>>
      sentMessages_;
};

}
