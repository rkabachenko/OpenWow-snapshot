#pragma once

#include "openwow/core/storm_string.h"
#include "openwow/data/formats/dbc/dbc_entries_extended.h"
#include "openwow/data/formats/dbc/dbc_store.h"
#include "openwow/foundation/text/ascii.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::ui::glue {

struct RandomNameDictionaryToken {
  std::array<char, 4> text{};
  std::uint32_t weight{0};
};

class RandomNameDictionary {
public:
  void Rebuild(const openwow::data::dbc::DbcStore<openwow::data::dbc::NameGenEntry> &name_gen,
               const std::uint32_t race_id, const std::uint32_t sex) {
    tokens_.clear();
    if (tokens_.capacity() < kTokenStorageReserve) {
      tokens_.reserve(kTokenStorageReserve);
    }

    for (const auto &entry : name_gen.entries()) {
      if (entry.race_id != race_id || entry.sex != sex) {
        continue;
      }

      std::string lower_name = "_";
      lower_name.append(entry.name.data(), entry.name.size());
      lower_name = openwow::text::ToLowerAscii(std::move(lower_name));
      if (lower_name.size() <= 1u) {
        continue;
      }

      for (std::size_t offset = 0; offset + 1u < lower_name.size(); ++offset) {
        RandomNameDictionaryToken token{};
        const std::size_t copy_size = std::min<std::size_t>(3u, lower_name.size() - offset);
        std::memcpy(token.text.data(), lower_name.data() + offset, copy_size);
        token.weight = 1;

        auto existing = std::find_if(tokens_.begin(), tokens_.end(), [&](const auto &current) {
          return openwow::core::SStrCmpNoCase(current.text.data(), token.text.data(), 0x7FFFFFFFu) == 0;
        });
        if (existing != tokens_.end()) {
          ++existing->weight;
        } else {
          tokens_.push_back(token);
        }
      }
    }

    std::sort(tokens_.begin(), tokens_.end(), [](const auto &left, const auto &right) {
      return openwow::core::SStrCmpNoCase(left.text.data(), right.text.data(), 0x7FFFFFFFu) < 0;
    });
  }

  [[nodiscard]] bool empty() const noexcept {
    return tokens_.empty();
  }

  [[nodiscard]] const std::vector<RandomNameDictionaryToken> &tokens() const noexcept {
    return tokens_;
  }

  [[nodiscard]] std::string Generate(
      const std::function<std::uint32_t(std::uint32_t)> &select_weight_ordinal,
      const std::size_t buffer_size = 14u) const {
    if (tokens_.empty() || buffer_size == 0u) {
      return {};
    }

    std::string generated;
    generated.reserve(buffer_size > 0u ? buffer_size - 1u : 0u);

    while (true) {
      const auto range = generated.empty() ? FindInitialRange() : FindContinuationRange(generated);
      if (!range.valid || range.total_weight == 0u) {
        return Finalize(generated);
      }

      std::uint32_t roll = select_weight_ordinal(range.total_weight);
      if (roll >= range.total_weight) {
        roll = range.total_weight - 1u;
      }

      const auto *selected = SelectToken(range, roll);
      if (selected == nullptr) {
        return Finalize(generated);
      }

      if (generated.empty()) {
        generated.assign(selected->text.data());
        continue;
      }

      if (selected->text[2] == '\0') {
        return Finalize(generated);
      }

      generated.push_back(selected->text[2]);
      if (generated.size() >= buffer_size - 1u) {
        return Finalize(generated);
      }
    }
  }

private:
  static constexpr std::size_t kTokenStorageReserve = 10000u;

  struct TokenRange {
    std::size_t first{0};
    std::size_t last{0};
    std::uint32_t total_weight{0};
    bool valid{false};
  };

  [[nodiscard]] TokenRange FindInitialRange() const {
    TokenRange range{};
    for (std::size_t index = 0; index < tokens_.size(); ++index) {
      if (tokens_[index].text[0] == '_') {
        if (!range.valid) {
          range.first = index;
          range.valid = true;
        }
        range.last = index;
        range.total_weight += tokens_[index].weight;
      } else if (range.valid) {
        break;
      }
    }
    return range;
  }

  [[nodiscard]] TokenRange FindContinuationRange(const std::string_view generated) const {
    TokenRange range{};
    if (generated.size() < 2u) {
      return range;
    }

    const std::string_view suffix = generated.substr(generated.size() - 2u, 2u);
    for (std::size_t index = 0; index < tokens_.size(); ++index) {
      if (tokens_[index].text[0] == suffix[0] && tokens_[index].text[1] == suffix[1]) {
        if (!range.valid) {
          range.first = index;
          range.valid = true;
        }
        range.last = index;
        range.total_weight += tokens_[index].weight;
      } else if (range.valid) {
        break;
      }
    }
    return range;
  }

  [[nodiscard]] const RandomNameDictionaryToken *SelectToken(const TokenRange &range,
                                                             std::uint32_t roll) const {
    for (std::size_t index = range.first; index <= range.last; ++index) {
      const auto &token = tokens_[index];
      if (roll < token.weight) {
        return &token;
      }
      roll -= token.weight;
    }
    return nullptr;
  }

  [[nodiscard]] static std::string Finalize(std::string generated) {
    if (generated.empty()) {
      return generated;
    }

    generated.erase(generated.begin());
    if (!generated.empty()) {
      generated.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(generated.front())));
    }
    return generated;
  }

  std::vector<RandomNameDictionaryToken> tokens_;
};

}
