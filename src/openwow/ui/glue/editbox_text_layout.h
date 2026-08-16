#pragma once

#include "openwow/foundation/text/utf8.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::ui::glue {

struct EditBoxCaretJustify {
  std::string_view horizontal{"LEFT"};
  std::string_view vertical{"TOP"};
};

inline EditBoxCaretJustify ResolveEditBoxCaretJustify(std::string_view widget_horizontal,
                                                      std::string_view widget_vertical,
                                                      std::string_view style_horizontal,
                                                      std::string_view style_vertical) noexcept {
  EditBoxCaretJustify justify;
  justify.horizontal = widget_horizontal.empty() ? style_horizontal : widget_horizontal;
  justify.vertical = widget_vertical.empty() ? style_vertical : widget_vertical;
  if (justify.horizontal.empty()) justify.horizontal = "LEFT";
  if (justify.vertical.empty()) justify.vertical = "TOP";
  return justify;
}

inline int ComputeEditBoxScrollOffsetPx(int old_scroll_offset_px,
                                        int cursor_px,
                                        int visible_width_px,
                                        int full_width_px) noexcept {
  const int visible_width = std::max(1, visible_width_px);
  int scroll_offset = old_scroll_offset_px;

  if (cursor_px - scroll_offset > visible_width) {
    scroll_offset = cursor_px - (visible_width * 3 / 4);
  }
  if (cursor_px - scroll_offset < 0) {
    scroll_offset = cursor_px - (visible_width / 4);
  }

  const int max_scroll = std::max(0, full_width_px - visible_width);
  return std::clamp(scroll_offset, 0, max_scroll);
}

inline bool IsPipeEscapeHexDigit(const char ch) noexcept {
  return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') ||
         (ch >= 'A' && ch <= 'F');
}

inline std::size_t AdvancePipeSafeVisibleUnit(std::string_view text,
                                             std::size_t offset,
                                             bool* consumed_visible = nullptr) noexcept {
  if (consumed_visible != nullptr) {
    *consumed_visible = false;
  }
  if (offset >= text.size()) {
    return text.size();
  }

  if (text[offset] == '|' && offset + 1 < text.size()) {
    const char next = text[offset + 1];
    if (next == '|') {
      if (consumed_visible != nullptr) {
        *consumed_visible = true;
      }
      return offset + 2;
    }
    if ((next == 'c' || next == 'C') && offset + 10 <= text.size()) {
      bool valid = true;
      for (std::size_t index = offset + 2; index < offset + 10; ++index) {
        if (!IsPipeEscapeHexDigit(text[index])) {
          valid = false;
          break;
        }
      }
      if (valid) {
        return offset + 10;
      }
    }
    if (next == 'r' || next == 'R') {
      return offset + 2;
    }
  }

  if (consumed_visible != nullptr) {
    *consumed_visible = true;
  }

  std::size_t next = offset + 1;
  while (next < text.size() &&
         (static_cast<unsigned char>(text[next]) & 0xC0u) == 0x80u) {
    ++next;
  }
  return next;
}

enum class WowTextEscapeType : uint8_t {
  Normal         = 0,
  ColorCode      = 1,
  ColorReset     = 2,
  Newline        = 3,
  EscapedPipe    = 4,
  HyperlinkStart = 5,
  HyperlinkEnd   = 6,
  TextureStart   = 7,
  TextureEnd     = 8,
};

[[nodiscard]] constexpr bool IsVisibleTextEscapeType(WowTextEscapeType t) noexcept {
  return t == WowTextEscapeType::Normal ||
         t == WowTextEscapeType::Newline ||
         t == WowTextEscapeType::EscapedPipe;
}

struct CharInfoRecord {
  static constexpr uint32_t kInHyperlinkBit = 0x80000000u;

  uint32_t packed{0};

  [[nodiscard]] uint16_t byteCount() const noexcept {
    return static_cast<uint16_t>(packed & 0xFFFFu);
  }
  [[nodiscard]] WowTextEscapeType charType() const noexcept {
    return static_cast<WowTextEscapeType>((packed >> 16) & 0xFFu);
  }
  [[nodiscard]] bool inHyperlink() const noexcept {
    return (packed & kInHyperlinkBit) != 0;
  }

  static uint32_t Pack(uint16_t bytes, WowTextEscapeType type,
                       bool in_hyperlink) noexcept {
    return static_cast<uint32_t>(bytes) |
           (static_cast<uint32_t>(type) << 16) |
           (in_hyperlink ? kInHyperlinkBit : 0u);
  }
};

[[nodiscard]] constexpr bool IsDisplayUnitPrefix(
    WowTextEscapeType type) noexcept {
  return type == WowTextEscapeType::ColorCode ||
         type == WowTextEscapeType::HyperlinkStart ||
         type == WowTextEscapeType::TextureStart;
}

[[nodiscard]] constexpr bool IsDisplayUnitSuffix(
    WowTextEscapeType type) noexcept {
  return type == WowTextEscapeType::ColorReset ||
         type == WowTextEscapeType::HyperlinkEnd ||
         type == WowTextEscapeType::TextureEnd;
}

[[nodiscard]] inline std::uint32_t MeasureDisplayUnitSpan(
    std::span<const std::uint32_t> records, std::size_t start_offset,
    int unit_count, bool atomic_hyperlinks = false) noexcept {
  if (unit_count == 0 || start_offset > records.size()) {
    return 0;
  }

  std::size_t cursor = start_offset;
  std::uint32_t distance = 0;

  const auto consume_forward = [&]() -> std::optional<CharInfoRecord> {
    if (cursor >= records.size() || records[cursor] == 0) {
      return std::nullopt;
    }
    const CharInfoRecord record{records[cursor]};
    const auto byte_count = record.byteCount();
    if (byte_count == 0 || byte_count > records.size() - cursor) {
      return std::nullopt;
    }
    cursor += byte_count;
    distance += byte_count;
    return record;
  };

  if (unit_count > 0) {
    for (int unit = 0; unit < unit_count; ++unit) {
      auto record = consume_forward();
      if (!record.has_value()) {
        break;
      }

      while (IsDisplayUnitPrefix(record->charType()) ||
             (atomic_hyperlinks && record->inHyperlink() &&
              record->charType() != WowTextEscapeType::HyperlinkEnd)) {
        record = consume_forward();
        if (!record.has_value()) {
          return distance;
        }
      }

      while (cursor < records.size() && records[cursor] != 0) {
        const CharInfoRecord next{records[cursor]};
        if (!IsDisplayUnitSuffix(next.charType())) {
          break;
        }
        if (!consume_forward().has_value()) {
          return distance;
        }
      }
    }
    return distance;
  }

  const auto previous_record_offset = [&]() -> std::optional<std::size_t> {
    if (cursor == 0) {
      return std::nullopt;
    }
    std::size_t offset = cursor;
    do {
      --offset;
      if (records[offset] != 0) {
        return offset;
      }
    } while (offset != 0);
    return std::nullopt;
  };

  const auto backward_units = -static_cast<std::int64_t>(unit_count);
  for (std::int64_t unit = 0; unit < backward_units; ++unit) {
    auto offset = previous_record_offset();
    if (!offset.has_value()) {
      break;
    }

    CharInfoRecord record{records[*offset]};
    cursor = *offset;
    distance += record.byteCount();

    while (IsDisplayUnitSuffix(record.charType()) ||
           (atomic_hyperlinks && record.inHyperlink() &&
            record.charType() != WowTextEscapeType::HyperlinkStart)) {
      offset = previous_record_offset();
      if (!offset.has_value()) {
        return distance;
      }
      record = CharInfoRecord{records[*offset]};
      cursor = *offset;
      distance += record.byteCount();
    }

    while (true) {
      const auto prefix_offset = previous_record_offset();
      if (!prefix_offset.has_value()) {
        break;
      }
      const CharInfoRecord prefix{records[*prefix_offset]};
      if (!IsDisplayUnitPrefix(prefix.charType())) {
        break;
      }
      cursor = *prefix_offset;
      distance += prefix.byteCount();
    }
  }

  return distance;
}

struct WowTextElementInfo {
  std::size_t       next_offset;
  uint16_t          byte_count;
  WowTextEscapeType type;
};

inline WowTextElementInfo ClassifyWowTextElement(std::string_view text,
                                                  std::size_t offset) noexcept {
  if (offset >= text.size()) {
    return {text.size(), 0, WowTextEscapeType::Normal};
  }

  const char ch = text[offset];

  if (ch == '\r') {
    if (offset + 1 < text.size() && text[offset + 1] == '\n') {
      return {offset + 2, 2, WowTextEscapeType::Newline};
    }
    return {offset + 1, 1, WowTextEscapeType::Newline};
  }
  if (ch == '\n') {
    return {offset + 1, 1, WowTextEscapeType::Newline};
  }

  if (ch == '|' && offset + 1 < text.size()) {
    const char next = text[offset + 1];

    if (next == '|') {
      return {offset + 2, 2, WowTextEscapeType::EscapedPipe};
    }

    if (next == 'n' || next == 'N') {
      return {offset + 2, 2, WowTextEscapeType::Newline};
    }

    if ((next == 'c' || next == 'C') && offset + 10 <= text.size()) {
      bool valid_hex = true;
      for (std::size_t i = offset + 2; i < offset + 10; ++i) {
        if (!IsPipeEscapeHexDigit(text[i])) {
          valid_hex = false;
          break;
        }
      }
      if (valid_hex) {
        return {offset + 10, 10, WowTextEscapeType::ColorCode};
      }
    }

    if (next == 'r' || next == 'R') {
      return {offset + 2, 2, WowTextEscapeType::ColorReset};
    }

    if (next == 'H') {
      std::size_t scan = offset + 2;
      while (scan + 1 < text.size()) {
        if (text[scan] == '|' && text[scan + 1] == 'h') {
          const std::size_t end = scan + 2;
          if (end - offset > 4) {
            return {end, static_cast<uint16_t>(end - offset),
                    WowTextEscapeType::HyperlinkStart};
          }
          break;
        }
        ++scan;
      }

    }

    if (next == 'h') {
      return {offset + 2, 2, WowTextEscapeType::HyperlinkEnd};
    }

    if (next == 'T') {
      std::size_t scan = offset + 2;
      while (scan + 1 < text.size()) {
        if (text[scan] == '|' && text[scan + 1] == 't') {
          const std::size_t end = scan + 2;
          if (end - offset > 4) {
            return {end, static_cast<uint16_t>(end - offset),
                    WowTextEscapeType::TextureStart};
          }
          break;
        }
        ++scan;
      }

    }

    if (next == 't') {
      return {offset + 2, 2, WowTextEscapeType::TextureEnd};
    }

  }

  std::size_t next_offset = offset + 1;
  while (next_offset < text.size() &&
         (static_cast<unsigned char>(text[next_offset]) & 0xC0u) == 0x80u) {
    ++next_offset;
  }
  return {next_offset, static_cast<uint16_t>(next_offset - offset),
          WowTextEscapeType::Normal};
}

inline std::size_t AdvanceWowTextElement(std::string_view text,
                                         std::size_t offset,
                                         bool* is_visible = nullptr) noexcept {
  if (is_visible != nullptr) {
    *is_visible = false;
  }
  if (offset >= text.size()) {
    return text.size();
  }

  const char ch = text[offset];

  if (ch == '\r') {
    if (is_visible != nullptr) {
      *is_visible = true;
    }

    if (offset + 1 < text.size() && text[offset + 1] == '\n') {
      return offset + 2;
    }
    return offset + 1;
  }
  if (ch == '\n') {
    if (is_visible != nullptr) {
      *is_visible = true;
    }
    return offset + 1;
  }

  if (ch == '|' && offset + 1 < text.size()) {
    const char next = text[offset + 1];

    if (next == '|') {
      if (is_visible != nullptr) {
        *is_visible = true;
      }
      return offset + 2;
    }

    if (next == 'n' || next == 'N') {
      if (is_visible != nullptr) {
        *is_visible = true;
      }
      return offset + 2;
    }

    if ((next == 'c' || next == 'C') && offset + 10 <= text.size()) {
      bool valid_hex = true;
      for (std::size_t i = offset + 2; i < offset + 10; ++i) {
        if (!IsPipeEscapeHexDigit(text[i])) {
          valid_hex = false;
          break;
        }
      }
      if (valid_hex) {
        return offset + 10;
      }
    }

    if (next == 'r' || next == 'R') {
      return offset + 2;
    }

    if (next == 'H') {
      std::size_t scan = offset + 2;
      while (scan + 1 < text.size()) {
        if (text[scan] == '|' && text[scan + 1] == 'h') {
          const std::size_t end = scan + 2;
          if (end - offset > 4) {
            return end;
          }
          break;
        }
        ++scan;
      }

    }

    if (next == 'h') {
      return offset + 2;
    }

    if (next == 'T') {
      std::size_t scan = offset + 2;
      while (scan + 1 < text.size()) {
        if (text[scan] == '|' && text[scan + 1] == 't') {
          const std::size_t end = scan + 2;
          if (end - offset > 4) {
            return end;
          }
          break;
        }
        ++scan;
      }

    }

    if (next == 't') {
      return offset + 2;
    }

  }

  if (is_visible != nullptr) {
    *is_visible = true;
  }
  std::size_t next_offset = offset + 1;
  while (next_offset < text.size() &&
         (static_cast<unsigned char>(text[next_offset]) & 0xC0u) == 0x80u) {
    ++next_offset;
  }
  return next_offset;
}

inline int CountEditBoxVisibleLetters(std::string_view text) noexcept {
  int count = 0;
  for (std::size_t offset = 0; offset < text.size();) {
    bool visible = false;
    offset = AdvanceWowTextElement(text, offset, &visible);
    if (visible) {
      ++count;
    }
  }
  return count;
}

inline int CountEditBoxVisibleLettersInRange(std::string_view text,
                                             int start_byte,
                                             int byte_count) noexcept {
  if (byte_count == 0 || text.empty()) {
    return 0;
  }

  std::size_t from;
  std::size_t len;
  if (byte_count > 0) {
    from = static_cast<std::size_t>(std::max(0, start_byte));
    len = static_cast<std::size_t>(byte_count);
  } else {
    const auto end = static_cast<std::size_t>(std::max(0, start_byte));
    const auto back = static_cast<std::size_t>(-byte_count);
    from = end > back ? end - back : 0;
    len = end - from;
  }

  if (from >= text.size()) {
    return 0;
  }
  len = std::min(len, text.size() - from);
  return CountEditBoxVisibleLetters(text.substr(from, len));
}

inline int CountPipeSafeVisibleCodepoints(std::string_view text) noexcept {
  int count = 0;
  for (std::size_t offset = 0; offset < text.size();) {
    bool consumed_visible = false;
    offset = AdvancePipeSafeVisibleUnit(text, offset, &consumed_visible);
    if (consumed_visible) {
      ++count;
    }
  }
  return count;
}

inline int PipeSafeUtf8ByteCountForVisibleCodepoints(std::string_view text,
                                                     int max_codepoints) noexcept {
  if (max_codepoints <= 0 || text.empty()) {
    return 0;
  }

  int remaining = max_codepoints;
  std::size_t offset = 0;
  while (offset < text.size() && remaining > 0) {
    bool consumed_visible = false;
    offset = AdvancePipeSafeVisibleUnit(text, offset, &consumed_visible);
    if (consumed_visible) {
      --remaining;
    }
  }
  return static_cast<int>(std::min(offset, text.size()));
}

inline std::string PipeSafeUtf8TakeCodepoints(std::string_view text,
                                              int max_codepoints) {
  return std::string(
      text.substr(0, static_cast<std::size_t>(
                         PipeSafeUtf8ByteCountForVisibleCodepoints(
                             text, max_codepoints))));
}

inline int VisibleCodepointCountBeforeByteOffset(std::string_view text,
                                                 const int byte_offset,
                                                 const bool password_masked) noexcept {
  const int clamped = std::clamp(byte_offset, 0, static_cast<int>(text.size()));
  const auto prefix = text.substr(0, static_cast<std::size_t>(clamped));
  if (password_masked) {
    return openwow::text::Utf8CodepointCount(prefix);
  }
  return CountPipeSafeVisibleCodepoints(prefix);
}

inline int ByteOffsetForVisibleCodepoints(std::string_view text,
                                          const int visible_codepoints,
                                          const bool password_masked) noexcept {
  if (password_masked) {
    return static_cast<int>(
        openwow::text::Utf8TakeCodepoints(text, visible_codepoints).size());
  }
  return PipeSafeUtf8ByteCountForVisibleCodepoints(text, visible_codepoints);
}

template <typename MeasureWidthFn>
int CountLeadingVisibleCodepointsWithinWidthPx(std::string_view display_text,
                                               const float width_px,
                                               MeasureWidthFn&& measure_width) {
  if (width_px <= 0.0f || display_text.empty()) {
    return 0;
  }

  std::vector<std::size_t> boundaries;
  boundaries.push_back(0);
  for (std::size_t offset = 0; offset < display_text.size();) {
    bool consumed_visible = false;
    offset = AdvancePipeSafeVisibleUnit(display_text, offset, &consumed_visible);
    if (consumed_visible) {
      boundaries.push_back(offset);
    }
  }

  int low = 0;
  int high = static_cast<int>(boundaries.size()) - 1;
  int best = 0;
  while (low <= high) {
    const int mid = low + ((high - low) / 2);
    const float measured_width = static_cast<float>(
        measure_width(
            display_text.substr(0, boundaries[static_cast<std::size_t>(mid)])));
    if (measured_width <= width_px) {
      best = mid;
      low = mid + 1;
    } else {
      high = mid - 1;
    }
  }
  return best;
}

template <typename MeasureWidthFn>
int CountTrailingVisibleCodepointsWithinWidthPx(std::string_view display_text,
                                                const float width_px,
                                                MeasureWidthFn&& measure_width) {
  if (width_px <= 0.0f || display_text.empty()) {
    return 0;
  }

  std::vector<std::size_t> boundaries;
  boundaries.push_back(0);
  for (std::size_t offset = 0; offset < display_text.size();) {
    bool consumed_visible = false;
    offset = AdvancePipeSafeVisibleUnit(display_text, offset, &consumed_visible);
    if (consumed_visible) {
      boundaries.push_back(offset);
    }
  }

  const int total_visible = static_cast<int>(boundaries.size()) - 1;
  int low = 0;
  int high = total_visible;
  int best = 0;
  while (low <= high) {
    const int mid = low + ((high - low) / 2);
    const std::size_t start =
        boundaries[static_cast<std::size_t>(total_visible - mid)];
    const float measured_width =
        static_cast<float>(measure_width(display_text.substr(start)));
    if (measured_width <= width_px) {
      best = mid;
      low = mid + 1;
    } else {
      high = mid - 1;
    }
  }
  return best;
}

struct EditBoxVisibleWindow {
  int start_visible_codepoints{0};
  int visible_codepoints{0};
  int start_display_byte{0};
  int end_display_byte{0};
  int start_real_byte{0};
  int end_real_byte{0};
};

template <typename MeasureWidthFn>
EditBoxVisibleWindow ResolveEditBoxVisibleWindow(
    std::string_view real_text,
    std::string_view display_text,
    const bool password_masked,
    const int current_start_visible_codepoints,
    const int cursor_byte,
    const float visible_width_px,
    MeasureWidthFn&& measure_width) {
  const int total_visible = password_masked
      ? openwow::text::Utf8CodepointCount(real_text)
      : CountPipeSafeVisibleCodepoints(real_text);

  EditBoxVisibleWindow window;
  window.start_visible_codepoints =
      std::clamp(current_start_visible_codepoints, 0, total_visible);
  if (visible_width_px <= 0.0f || total_visible <= 0) {
    window.start_display_byte = ByteOffsetForVisibleCodepoints(
        display_text, window.start_visible_codepoints, false);
    window.end_display_byte = window.start_display_byte;
    window.start_real_byte = ByteOffsetForVisibleCodepoints(
        real_text, window.start_visible_codepoints, password_masked);
    window.end_real_byte = window.start_real_byte;
    return window;
  }

  const auto count_visible_from = [&](const int start_visible_codepoints) {
    const int start_display_byte = ByteOffsetForVisibleCodepoints(
        display_text, start_visible_codepoints, false);
    return CountLeadingVisibleCodepointsWithinWidthPx(
        display_text.substr(static_cast<std::size_t>(start_display_byte)),
        visible_width_px, measure_width);
  };

  const int cursor_visible_codepoints = VisibleCodepointCountBeforeByteOffset(
      real_text, cursor_byte, password_masked);
  int visible_codepoints =
      count_visible_from(window.start_visible_codepoints);

  if (cursor_visible_codepoints < window.start_visible_codepoints) {
    const int cursor_display_byte = ByteOffsetForVisibleCodepoints(
        display_text, cursor_visible_codepoints, false);
    const int backtracked_codepoints =
        CountTrailingVisibleCodepointsWithinWidthPx(
            display_text.substr(0, static_cast<std::size_t>(cursor_display_byte)),
            visible_width_px * 0.25f, measure_width);
    window.start_visible_codepoints =
        std::max(0, cursor_visible_codepoints - backtracked_codepoints);
    visible_codepoints =
        count_visible_from(window.start_visible_codepoints);
  } else if (cursor_visible_codepoints >
             window.start_visible_codepoints + visible_codepoints) {
    const int cursor_display_byte = ByteOffsetForVisibleCodepoints(
        display_text, cursor_visible_codepoints, false);
    const int backtracked_codepoints =
        CountTrailingVisibleCodepointsWithinWidthPx(
            display_text.substr(0, static_cast<std::size_t>(cursor_display_byte)),
            visible_width_px * 0.75f, measure_width);
    window.start_visible_codepoints =
        std::max(0, cursor_visible_codepoints - backtracked_codepoints);
    visible_codepoints =
        count_visible_from(window.start_visible_codepoints);
  }

  window.visible_codepoints = std::clamp(
      visible_codepoints, 0, total_visible - window.start_visible_codepoints);
  window.start_display_byte = ByteOffsetForVisibleCodepoints(
      display_text, window.start_visible_codepoints, false);
  window.end_display_byte = ByteOffsetForVisibleCodepoints(
      display_text,
      window.start_visible_codepoints + window.visible_codepoints, false);
  window.start_real_byte = ByteOffsetForVisibleCodepoints(
      real_text, window.start_visible_codepoints, password_masked);
  window.end_real_byte = ByteOffsetForVisibleCodepoints(
      real_text,
      window.start_visible_codepoints + window.visible_codepoints,
      password_masked);
  return window;
}

template <typename MeasureWidthFn>
int ResolveEditBoxCursorByteFromDisplayWidthPx(std::string_view real_text,
                                               std::string_view display_text,
                                               const bool password_masked,
                                               const float width_px,
                                               MeasureWidthFn&& measure_width) {
  const int visible_codepoints = CountLeadingVisibleCodepointsWithinWidthPx(
      display_text, width_px, measure_width);
  if (password_masked) {
    return static_cast<int>(
        openwow::text::Utf8TakeCodepoints(real_text, visible_codepoints).size());
  }
  return PipeSafeUtf8ByteCountForVisibleCodepoints(real_text, visible_codepoints);
}

enum WowEscapeStripFlag : uint32_t {
  kStripColor     = 0x0100,
  kStripNewline   = 0x0200,
  kStripHyperlink = 0x0400,
  kStripPipe      = 0x0800,
  kStripTexture   = 0x1000,
};

inline constexpr uint32_t kClipboardStripFlags =
    kStripColor | kStripHyperlink | kStripTexture;

[[nodiscard]] inline std::string StripWowTextEscapes(
    std::string_view input, uint32_t flags) {
  if (input.empty()) {
    return std::string(input);
  }

  struct EscMeta {
    uint32_t mask;
    char     repl;
    bool     pipe;
  };
  static constexpr EscMeta kTable[9] = {
       {0x0000, '-',  true },
       {0x0100, 'C',  true },
       {0x0100, 'R',  true },
       {0x0200, '\n', false},
       {0x0800, '|',  false},
       {0x0400, 'H',  true },
       {0x0400, 'h',  true },
       {0x1000, 'T',  true },
       {0x1000, 't',  true },
  };

  std::string out;
  out.reserve(input.size());

  std::size_t offset = 0;
  while (offset < input.size()) {
    const auto elem = ClassifyWowTextElement(input, offset);
    const auto idx = static_cast<unsigned>(elem.type);

    if (idx == 0) {

      out.append(input.data() + offset, elem.byte_count);
    } else if (idx <= 8) {
      if ((flags & kTable[idx].mask) != 0) {

      } else {

        switch (idx) {
          case 1: case 2: case 5: case 7:
            out.append(input.data() + offset, elem.byte_count);
            break;
          case 3: case 4: case 6: case 8:
            if (kTable[idx].pipe) out += '|';
            out += kTable[idx].repl;
            break;
          default:
            break;
        }
      }
    }

    offset = elem.next_offset;
  }

  return out;
}

}
