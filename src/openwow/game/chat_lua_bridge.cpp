
#include "openwow/game/chat_lua_bridge.h"
#include "openwow/game/chat_color_defaults.h"

#include <algorithm>
#include <cctype>

namespace openwow::game {

namespace {

constexpr float kChatColorComponentScale = 255.0f;

std::uint8_t EncodeChatColorComponent(float value) {
  return static_cast<std::uint8_t>(
      static_cast<int>(value * kChatColorComponentScale));
}

float DecodeChatColorComponent(std::uint8_t value) {
  return static_cast<float>(value) / kChatColorComponentScale;
}

}

ChatLuaBridge& ChatLuaBridge::Get() {
  static ChatLuaBridge instance;
  return instance;
}

ChatLuaBridge::ChatLuaBridge() {
  ResetChatTypeTables();
}

void ChatLuaBridge::SendChatMessage(const std::string& msg,
                                     const std::string& chatType,
                                     const std::string& language,
                                     const std::string& channel) {
  sentMessages_.emplace_back(msg, chatType, language, channel);
}

std::uint32_t ChatLuaBridge::GetNumChatWindows() const {
  return kDefaultNumWindows;
}

ChatLuaBridge::ChatWindowInfoResult ChatLuaBridge::GetChatWindowInfo(
    std::uint32_t index) const {
  auto it = windows_.find(index);
  if (it != windows_.end()) return it->second;

  ChatWindowInfoResult def;
  if (index == 1) {
    def.name = "General";
  } else if (index == 2) {
    def.name = "Combat Log";
  } else {
    def.name = "Chat Window " + std::to_string(index);
    def.shown = false;
  }
  return def;
}

ChatLuaBridge::ChatTypeInfoResult ChatLuaBridge::GetChatTypeInfo(
    const std::string& chatType) const {
  const auto normalized = NormalizeChatTypeKey(chatType);
  auto it = chatTypes_.find(normalized);
  if (it != chatTypes_.end()) return it->second;
  if (const auto* entry = FindStoredChatType(chatType)) {
    return {entry->token, DecodeChatColorComponent(entry->r),
            DecodeChatColorComponent(entry->g),
            DecodeChatColorComponent(entry->b)};
  }
  return {chatType, 1.0f, 1.0f, 1.0f};
}

std::uint32_t ChatLuaBridge::GetChatTypeIndex(const std::string& chatType) const {
  const auto normalized = NormalizeChatTypeKey(chatType);

  for (std::size_t i = 0; i < builtinChatTypes_.size(); ++i) {
    if (NormalizeChatTypeKey(builtinChatTypes_[i].token) == normalized) {
      return static_cast<std::uint32_t>(i + 1u);
    }
  }

  for (std::size_t i = 0; i < dynamicChatTypes_.size(); ++i) {
    if (NormalizeChatTypeKey(dynamicChatTypes_[i].token) == normalized) {
      return static_cast<std::uint32_t>(builtinChatTypes_.size() + i + 1u);
    }
  }

  return 0;
}

std::pair<bool, std::string> ChatLuaBridge::JoinChannelByName(
    const std::string& name, const std::string& password) {

  for (const auto& ch : channels_) {
    if (ch.name == name) return {true, ch.name};
  }

  ChannelEntry entry;
  entry.id = nextChannelId_++;
  entry.name = name;
  entry.password = password;
  entry.memberCount = 1;
  channels_.push_back(std::move(entry));
  return {true, name};
}

void ChatLuaBridge::LeaveChannelByName(const std::string& name) {
  channels_.erase(
      std::remove_if(channels_.begin(), channels_.end(),
                     [&](const ChannelEntry& ch) { return ch.name == name; }),
      channels_.end());
}

std::pair<std::string, std::string> ChatLuaBridge::GetChannelName(
    std::uint32_t channelId) const {
  for (const auto& ch : channels_) {
    if (ch.id == channelId) return {ch.name, ch.header};
  }
  return {};
}

bool ChatLuaBridge::IsInGroup(const std::string& chatType) const {
  auto it = groupFlags_.find(chatType);
  if (it != groupFlags_.end()) return it->second;
  return false;
}

std::pair<std::string, std::uint32_t> ChatLuaBridge::GetDefaultLanguage()
    const {
  return {defaultLangName_, defaultLangId_};
}

std::vector<std::tuple<std::uint32_t, std::string, std::uint32_t>>
ChatLuaBridge::ListChannels() const {
  std::vector<std::tuple<std::uint32_t, std::string, std::uint32_t>> result;
  result.reserve(channels_.size());
  for (const auto& ch : channels_) {
    result.emplace_back(ch.id, ch.name, ch.memberCount);
  }
  return result;
}

std::uint32_t ChatLuaBridge::GetNumDisplayMessages(
    std::uint32_t windowIndex) const {
  auto it = messages_.find(windowIndex);
  if (it == messages_.end()) return 0;
  return static_cast<std::uint32_t>(it->second.size());
}

std::string ChatLuaBridge::GetDisplayMessage(std::uint32_t windowIndex,
                                              std::uint32_t msgIndex) const {
  auto it = messages_.find(windowIndex);
  if (it == messages_.end()) return {};
  if (msgIndex == 0 || msgIndex > it->second.size()) return {};
  return it->second[msgIndex - 1];
}

void ChatLuaBridge::SetChatWindow(std::uint32_t index,
                                   const ChatWindowInfoResult& info) {
  windows_[index] = info;
}

void ChatLuaBridge::RegisterChatType(const std::string& chatType,
                                      const ChatTypeInfoResult& info) {
  chatTypes_[NormalizeChatTypeKey(chatType)] = info;
}

bool ChatLuaBridge::SetChatTypeColor(const std::string& chatType, float r, float g,
                                     float b) {
  return SetChatTypeColorBytes(chatType, EncodeChatColorComponent(r),
                               EncodeChatColorComponent(g),
                               EncodeChatColorComponent(b));
}

bool ChatLuaBridge::SetChatTypeColorBytes(const std::string& chatType,
                                          std::uint8_t r, std::uint8_t g,
                                          std::uint8_t b) {
  auto* entry = FindStoredChatType(chatType);
  if (!entry) {
    return false;
  }

  entry->r = r;
  entry->g = g;
  entry->b = b;
  return true;
}

void ChatLuaBridge::ResetChatColors() {
  for (std::size_t i = 0; i < builtinChatTypes_.size(); ++i) {
    builtinChatTypes_[i].r = kBuiltinChatColorDefaults[i].r;
    builtinChatTypes_[i].g = kBuiltinChatColorDefaults[i].g;
    builtinChatTypes_[i].b = kBuiltinChatColorDefaults[i].b;
  }

  for (auto& entry : dynamicChatTypes_) {
    entry.r = kDynamicChatDefaultColor;
    entry.g = kDynamicChatDefaultColor;
    entry.b = kDynamicChatDefaultColor;
  }
}

void ChatLuaBridge::ResetChatTypeVisuals() {
  chatTypes_.clear();
  ResetChatTypeTables();
}

std::vector<ChatLuaBridge::ChatTypeVisualState> ChatLuaBridge::GetChatTypeVisualStates() const {
  std::vector<ChatTypeVisualState> states;
  states.reserve(builtinChatTypes_.size() + dynamicChatTypes_.size());

  const auto append_states = [&states](const auto& entries) {
    for (const auto& entry : entries) {
      states.push_back({entry.token, DecodeChatColorComponent(entry.r),
                        DecodeChatColorComponent(entry.g), DecodeChatColorComponent(entry.b),
                        entry.colorNameByClass});
    }
  };

  append_states(builtinChatTypes_);
  append_states(dynamicChatTypes_);
  return states;
}

std::vector<ChatLuaBridge::ChatTypeCacheState> ChatLuaBridge::GetChatTypeCacheStates() const {
  std::vector<ChatTypeCacheState> states;
  states.reserve(builtinChatTypes_.size() + dynamicChatTypes_.size());

  const auto append_states = [&states](const auto& entries) {
    for (const auto& entry : entries) {
      states.push_back({entry.token, entry.r, entry.g, entry.b,
                        entry.colorNameByClass});
    }
  };

  append_states(builtinChatTypes_);
  append_states(dynamicChatTypes_);
  return states;
}

bool ChatLuaBridge::SetChatColorNameByClass(const std::string& chatType,
                                            bool enabled) {
  auto* entry = FindStoredChatType(chatType);
  if (!entry) {
    return false;
  }

  entry->colorNameByClass = enabled;
  return true;
}

bool ChatLuaBridge::GetChatColorNameByClass(const std::string& chatType) const {
  if (const auto* entry = FindStoredChatType(chatType)) {
    return entry->colorNameByClass;
  }
  return false;
}

void ChatLuaBridge::SetDefaultLanguage(const std::string& name,
                                        std::uint32_t id) {
  defaultLangName_ = name;
  defaultLangId_ = id;
}

void ChatLuaBridge::SetInGroup(const std::string& chatType, bool val) {
  groupFlags_[chatType] = val;
}

void ChatLuaBridge::AddMessage(std::uint32_t windowIndex,
                                const std::string& msg) {
  messages_[windowIndex].push_back(msg);
}

void ChatLuaBridge::Clear() {
  windows_.clear();
  chatTypes_.clear();
  ResetChatTypeTables();
  channels_.clear();
  nextChannelId_ = 1;
  defaultLangName_ = "Common";
  defaultLangId_ = 7;
  numChatGroups_ = 0;
  groupFlags_.clear();
  messages_.clear();
  sentMessages_.clear();
}

void ChatLuaBridge::ResetChatTypeTables() {
  builtinChatTypes_.clear();
  builtinChatTypes_.reserve(kBuiltinChatColorDefaults.size());
  for (const auto& entry : kBuiltinChatColorDefaults) {
    builtinChatTypes_.push_back(
        {std::string(entry.token), entry.r, entry.g, entry.b, entry.color_name_by_class});
  }

  dynamicChatTypes_.clear();
  dynamicChatTypes_.reserve(kDynamicChatTypeCount);
  for (std::size_t i = 0; i < kDynamicChatTypeCount; ++i) {
    dynamicChatTypes_.push_back({"CHANNEL" + std::to_string(i + 1u),
                                 kDynamicChatDefaultColor,
                                 kDynamicChatDefaultColor,
                                 kDynamicChatDefaultColor,
                                 kDynamicChatDefaultColorNameByClass});
  }
}

std::string ChatLuaBridge::NormalizeChatTypeKey(const std::string& chatType) {
  std::string normalized;
  normalized.reserve(chatType.size());
  for (unsigned char ch : chatType) {
    normalized.push_back(static_cast<char>(std::toupper(ch)));
  }
  return normalized;
}

const ChatLuaBridge::StoredChatType* ChatLuaBridge::FindStoredChatType(
    const std::string& chatType) const {
  const auto normalized = NormalizeChatTypeKey(chatType);

  for (const auto& entry : builtinChatTypes_) {
    if (NormalizeChatTypeKey(entry.token) == normalized) {
      return &entry;
    }
  }

  for (const auto& entry : dynamicChatTypes_) {
    if (NormalizeChatTypeKey(entry.token) == normalized) {
      return &entry;
    }
  }

  return nullptr;
}

ChatLuaBridge::StoredChatType* ChatLuaBridge::FindStoredChatType(
    const std::string& chatType) {
  const auto normalized = NormalizeChatTypeKey(chatType);

  for (auto& entry : builtinChatTypes_) {
    if (NormalizeChatTypeKey(entry.token) == normalized) {
      return &entry;
    }
  }

  for (auto& entry : dynamicChatTypes_) {
    if (NormalizeChatTypeKey(entry.token) == normalized) {
      return &entry;
    }
  }

  return nullptr;
}

}
