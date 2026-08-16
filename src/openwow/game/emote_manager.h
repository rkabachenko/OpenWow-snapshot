#pragma once

#include "openwow/data/formats/dbc/dbc_entries_extended.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace openwow::game {

struct ActiveEmote {
  std::uint64_t guid = 0;
  std::uint32_t emote_id = 0;
  float elapsed = 0.0f;
  float duration = 0.0f;
  bool is_oneshot = false;
};

struct TextEmoteFormatContext {
  bool source_is_active_player = false;
  bool target_is_active_player = false;
  bool has_target = false;
  bool use_alternate_gender_variant = false;
};

using TextDataResolver = std::function<std::string_view(std::uint32_t)>;

[[nodiscard]] std::string FormatTextEmoteText(
    const openwow::data::dbc::EmotesTextEntry& entry,
    const TextEmoteFormatContext& context,
    std::string_view source_name,
    std::string_view target_name,
    const TextDataResolver& resolve_text_data);

class EmoteManager {
 public:

  void LoadEmotes(
      const std::vector<openwow::data::dbc::EmotesEntry>& emotes,
      const std::vector<openwow::data::dbc::EmotesTextEntry>& emote_texts,
      const std::vector<openwow::data::dbc::EmotesTextDataEntry>& emote_text_data = {});

  [[nodiscard]] std::optional<openwow::data::dbc::EmotesEntry>
  GetEmote(std::uint32_t emote_id) const;

  [[nodiscard]] std::optional<openwow::data::dbc::EmotesTextEntry>
  GetEmoteText(std::uint32_t text_id) const;

  [[nodiscard]] std::optional<openwow::data::dbc::EmotesEntry>
  GetEmoteByName(const std::string& name) const;

  void PlayEmote(std::uint64_t guid, std::uint32_t emote_id);

  void StopEmote(std::uint64_t guid);

  [[nodiscard]] std::uint32_t GetActiveEmote(std::uint64_t guid) const;

  [[nodiscard]] bool IsPlayingEmote(std::uint64_t guid) const;

  [[nodiscard]] std::string FormatEmoteText(std::uint32_t text_id,
                                             const std::string& source,
                                             const std::string& target,
                                             bool is_self) const;

  [[nodiscard]] std::vector<std::string> GetAllEmoteNames() const;

  void Update(float dt);

 private:
  std::unordered_map<std::uint32_t, openwow::data::dbc::EmotesEntry> emotes_;
  std::unordered_map<std::uint32_t, openwow::data::dbc::EmotesTextEntry> emote_texts_;

  std::unordered_map<std::uint32_t, openwow::data::dbc::EmotesTextDataEntry> emote_text_data_;

  std::unordered_map<std::string, std::uint32_t> name_index_;

  std::unordered_map<std::uint64_t, ActiveEmote> active_;
};

}
