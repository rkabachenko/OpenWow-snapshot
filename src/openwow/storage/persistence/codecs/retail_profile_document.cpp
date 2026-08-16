#include "openwow/storage/persistence/profile_document.h"

#include "openwow/foundation/text/ascii.h"

#include <utility>

namespace openwow::storage::persistence {
namespace {

std::string_view ProfileText(std::span<const std::byte> bytes) {
  if (bytes.empty()) {
    return {};
  }

  const auto* text = reinterpret_cast<const char*>(bytes.data());
  std::size_t text_size = 0;
  while (text_size < bytes.size() && text[text_size] != '\0') {
    ++text_size;
  }
  return {text, text_size};
}

std::vector<ProfileValue> ParseValues(std::string_view raw_values) {
  std::vector<ProfileValue> values;
  std::size_t cursor = 0;

  while (cursor < raw_values.size()) {
    const bool quoted = raw_values[cursor] == '"';
    if (quoted) {
      ++cursor;
    }

    const std::size_t value_start = cursor;
    bool terminated = false;
    while (cursor < raw_values.size()) {
      const char terminator = quoted ? '"' : ',';
      if (raw_values[cursor] == terminator) {
        values.emplace_back(
            std::string(raw_values.substr(value_start, cursor - value_start)));
        ++cursor;
        if (quoted && cursor < raw_values.size() &&
            raw_values[cursor] == ',') {
          ++cursor;
        }
        terminated = true;
        break;
      }
      ++cursor;
    }

    if (!terminated) {
      values.emplace_back(
          std::string(raw_values.substr(value_start, cursor - value_start)));
      break;
    }
  }

  return values;
}

void ParseLine(std::string_view line, std::string* current_section,
               bool* has_current_section,
               std::vector<ProfileAssignment>* assignments) {
  if (line.empty() || line.starts_with("//")) {
    return;
  }

  if (line.front() == '[') {
    const std::size_t close = line.find(']');
    if (close != std::string_view::npos) {
      current_section->assign(line.substr(1, close - 1));
      *has_current_section = true;
    }
    return;
  }

  const std::size_t equals = line.find('=');
  if (equals == std::string_view::npos || !*has_current_section) {
    return;
  }

  std::vector<ProfileValue> values = ParseValues(line.substr(equals + 1));
  if (values.empty()) {
    return;
  }

  assignments->push_back(ProfileAssignment{
      .section = ProfileSectionName(*current_section),
      .key = ProfileKeyName(std::string(line.substr(0, equals))),
      .values = std::move(values),
  });
}

}

ProfileSectionName::ProfileSectionName(std::string text)
    : text_(std::move(text)) {}

std::string_view ProfileSectionName::Text() const {
  return text_;
}

const char* ProfileSectionName::CStr() const {
  return text_.c_str();
}

ProfileKeyName::ProfileKeyName(std::string text) : text_(std::move(text)) {}

std::string_view ProfileKeyName::Text() const {
  return text_;
}

const char* ProfileKeyName::CStr() const {
  return text_.c_str();
}

ProfileValue::ProfileValue(std::string text) : text_(std::move(text)) {}

std::string_view ProfileValue::Text() const {
  return text_;
}

ProfileValueView ProfileValue::View() const {
  return ProfileValueView(text_);
}

ProfileDocument ProfileDocument::Parse(std::span<const std::byte> bytes) {
  ProfileDocument document;
  const std::string_view text = ProfileText(bytes);
  std::string current_section;
  bool has_current_section = false;
  std::size_t line_start = 0;

  while (line_start < text.size()) {
    std::size_t line_end = line_start;
    while (line_end < text.size() && text[line_end] != '\r' &&
           text[line_end] != '\n') {
      ++line_end;
    }

    ParseLine(text.substr(line_start, line_end - line_start), &current_section,
              &has_current_section, &document.assignments_);

    if (line_end < text.size() && text[line_end] == '\r') {
      ++line_end;
    }
    if (line_end < text.size() && text[line_end] == '\n') {
      ++line_end;
    }
    line_start = line_end;
  }

  return document;
}

const std::vector<ProfileAssignment>& ProfileDocument::Assignments() const {
  return assignments_;
}

const ProfileValue* ProfileDocument::FindFirstValue(
    const std::string_view section, const std::string_view key) const {
  for (const ProfileAssignment& assignment : assignments_) {
    if (openwow::text::EqualsIgnoreCaseAscii(
            assignment.section.Text(), section) &&
        openwow::text::EqualsIgnoreCaseAscii(
            assignment.key.Text(), key) &&
        !assignment.values.empty()) {
      return &assignment.values.front();
    }
  }
  return nullptr;
}

}
