
#include "openwow/game/emote_manager.h"

#include <algorithm>
#include <cctype>

namespace openwow::game {

namespace {

std::string ToLower(std::string_view sv) {
  std::string result;
  result.reserve(sv.size());
  for (char c : sv) {
    result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  return result;
}

constexpr float kOneshotDuration = 2.0f;

std::string ApplyTextEmoteTemplate(std::string_view format,
                                   const std::vector<std::string_view>& args) {
  std::string result;
  result.reserve(format.size() + 32);

  std::size_t arg_index = 0;
  for (std::size_t i = 0; i < format.size(); ++i) {
    if (format[i] == '%' && i + 1 < format.size() && format[i + 1] == 's') {
      if (arg_index < args.size()) {
        result.append(args[arg_index].data(), args[arg_index].size());
      }
      ++arg_index;
      ++i;
      continue;
    }
    result.push_back(format[i]);
  }

  return result;
}

}

std::string FormatTextEmoteText(
    const openwow::data::dbc::EmotesTextEntry& entry,
    const TextEmoteFormatContext& context,
    std::string_view source_name,
    std::string_view target_name,
    const TextDataResolver& resolve_text_data) {
  std::size_t variant = 0;
  if (context.target_is_active_player) {
    variant |= 0x1u;
  }
  if (context.source_is_active_player) {
    variant |= 0x2u;
  }
  if (!context.has_target) {
    variant |= 0x4u;
  }
  if (context.use_alternate_gender_variant) {
    variant |= 0x8u;
  }

  auto lookup_variant = [&entry, &resolve_text_data](const std::size_t index) -> std::string_view {
    const auto id = entry.TextDataId(index);
    return (id != 0 && resolve_text_data) ? resolve_text_data(id) : std::string_view{};
  };

  std::string_view format = lookup_variant(variant);
  if (format.empty() && context.use_alternate_gender_variant) {
    format = lookup_variant(variant & ~std::size_t{0x8u});
  }
  if (format.empty()) {
    const std::size_t no_target_variant = (variant & ~std::size_t{0x1u}) | 0x4u;
    format = lookup_variant(no_target_variant);
    if (format.empty() && context.use_alternate_gender_variant) {
      format = lookup_variant(no_target_variant & ~std::size_t{0x8u});
    }
  }
  if (format.empty()) {
    return {};
  }

  std::vector<std::string_view> args;
  args.reserve(2);
  if (!context.source_is_active_player) {
    args.push_back(source_name);
    if (!context.target_is_active_player) {
      args.push_back(target_name);
    }
  } else if (!context.target_is_active_player) {
    args.push_back(target_name);
  }

  return ApplyTextEmoteTemplate(format, args);
}

void EmoteManager::LoadEmotes(
    const std::vector<openwow::data::dbc::EmotesEntry>& emotes,
    const std::vector<openwow::data::dbc::EmotesTextEntry>& emote_texts,
    const std::vector<openwow::data::dbc::EmotesTextDataEntry>& emote_text_data) {
  emotes_.clear();
  emote_texts_.clear();
  emote_text_data_.clear();
  name_index_.clear();

  emotes_.reserve(emotes.size());
  for (const auto& e : emotes) {
    emotes_[e.id] = e;
    if (!e.name.empty()) {
      name_index_[ToLower(e.name)] = e.id;
    }
  }

  emote_texts_.reserve(emote_texts.size());
  for (const auto& et : emote_texts) {
    emote_texts_[et.id] = et;

    if (!et.name.empty()) {
      name_index_[ToLower(et.name)] = et.emote_id;
    }
  }

  emote_text_data_.reserve(emote_text_data.size());
  for (const auto& etd : emote_text_data) {
    emote_text_data_[etd.id] = etd;
  }
}

std::optional<openwow::data::dbc::EmotesEntry>
EmoteManager::GetEmote(std::uint32_t emote_id) const {
  auto it = emotes_.find(emote_id);
  if (it != emotes_.end()) return it->second;
  return std::nullopt;
}

std::optional<openwow::data::dbc::EmotesTextEntry>
EmoteManager::GetEmoteText(std::uint32_t text_id) const {
  auto it = emote_texts_.find(text_id);
  if (it != emote_texts_.end()) return it->second;
  return std::nullopt;
}

std::optional<openwow::data::dbc::EmotesEntry>
EmoteManager::GetEmoteByName(const std::string& name) const {
  auto it = name_index_.find(ToLower(name));
  if (it != name_index_.end()) {
    return GetEmote(it->second);
  }
  return std::nullopt;
}

void EmoteManager::PlayEmote(std::uint64_t guid, std::uint32_t emote_id) {
  auto emote_it = emotes_.find(emote_id);

  ActiveEmote ae;
  ae.guid = guid;
  ae.emote_id = emote_id;
  ae.elapsed = 0.0f;

  if (emote_it != emotes_.end()) {

    ae.is_oneshot = (emote_it->second.spec != 1);
    ae.duration = ae.is_oneshot ? kOneshotDuration : 0.0f;
  } else {

    ae.is_oneshot = true;
    ae.duration = kOneshotDuration;
  }

  active_[guid] = ae;
}

void EmoteManager::StopEmote(std::uint64_t guid) {
  active_.erase(guid);
}

std::uint32_t EmoteManager::GetActiveEmote(std::uint64_t guid) const {
  auto it = active_.find(guid);
  return (it != active_.end()) ? it->second.emote_id : 0;
}

bool EmoteManager::IsPlayingEmote(std::uint64_t guid) const {
  return active_.find(guid) != active_.end();
}

std::string EmoteManager::FormatEmoteText(std::uint32_t text_id,
                                           const std::string& source,
                                           const std::string& target,
                                           bool is_self) const {
  auto it = emote_texts_.find(text_id);
  if (it == emote_texts_.end()) {
    return {};
  }

  TextEmoteFormatContext context;
  context.source_is_active_player = is_self;
  context.has_target = !target.empty();

  auto resolver = [this](std::uint32_t text_data_id) -> std::string_view {
    auto td_it = emote_text_data_.find(text_data_id);
    return (td_it != emote_text_data_.end()) ? td_it->second.text : std::string_view{};
  };

  return FormatTextEmoteText(it->second, context, source, target, resolver);
}

std::vector<std::string> EmoteManager::GetAllEmoteNames() const {
  std::vector<std::string> names;
  names.reserve(emotes_.size());
  for (const auto& [id, emote] : emotes_) {
    if (!emote.name.empty()) {
      names.emplace_back(emote.name);
    }
  }
  std::sort(names.begin(), names.end());
  return names;
}

void EmoteManager::Update(float dt) {
  for (auto it = active_.begin(); it != active_.end();) {
    auto& ae = it->second;
    ae.elapsed += dt;

    if (ae.is_oneshot && ae.duration > 0.0f && ae.elapsed >= ae.duration) {
      it = active_.erase(it);
    } else {
      ++it;
    }
  }
}

}
