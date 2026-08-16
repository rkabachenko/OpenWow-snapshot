#include "openwow/game/actions/macros/adapters/persistence/macro_account_data_adapter.h"

#include "openwow/game/account_data.h"
#include "openwow/game/actions/macros/application/macro_catalog.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <optional>
#include <sstream>

namespace openwow::game::actions::macros::persistence {
namespace {

[[nodiscard]] bool EqualsAsciiNoCase(std::string_view lhs,
                                     std::string_view rhs) {
  return lhs.size() == rhs.size() &&
         std::equal(lhs.begin(), lhs.end(), rhs.begin(), [](char a, char b) {
           return std::tolower(static_cast<unsigned char>(a)) ==
                  std::tolower(static_cast<unsigned char>(b));
         });
}

[[nodiscard]] std::string_view ConsumeToken(std::string_view line,
                                            std::size_t& cursor) {
  while (cursor < line.size() &&
         (std::isspace(static_cast<unsigned char>(line[cursor])) ||
          line[cursor] == '"')) {
    ++cursor;
  }
  const std::size_t begin = cursor;
  while (cursor < line.size() &&
         !std::isspace(static_cast<unsigned char>(line[cursor])) &&
         line[cursor] != '"') {
    ++cursor;
  }
  return line.substr(begin, cursor - begin);
}

[[nodiscard]] std::optional<MacroDocument> ParseHeader(
    MacroScope scope, std::string_view line) {
  if (line.size() <= 5 || !EqualsAsciiNoCase(line.substr(0, 5), "MACRO") ||
      !std::isspace(static_cast<unsigned char>(line[5]))) {
    return std::nullopt;
  }

  std::size_t cursor = 5;
  const auto id_token = ConsumeToken(line, cursor);
  const auto name = ConsumeToken(line, cursor);
  const auto icon = ConsumeToken(line, cursor);
  std::uint32_t raw_id = 0;
  const auto [end, error] =
      std::from_chars(id_token.data(), id_token.data() + id_token.size(), raw_id);
  if (id_token.empty() || name.empty() || icon.empty() ||
      error != std::errc{} || end != id_token.data() + id_token.size() ||
      raw_id == 0) {
    return std::nullopt;
  }

  MacroDocument macro;
  macro.id = MacroId(raw_id);
  macro.name.assign(name.substr(0, MacroCatalog::kMaxNameLength));
  macro.icon_name.assign(icon.substr(0, 255));
  macro.scope = scope;
  return macro;
}

}

std::vector<MacroDocument> MacroAccountDataAdapter::Decode(
    MacroScope scope, std::string_view text) {
  std::vector<MacroDocument> macros;
  const std::size_t capacity = scope == MacroScope::kCharacter
                                   ? MacroCatalog::kMaxCharacterMacros
                                   : MacroCatalog::kMaxAccountMacros;
  std::istringstream stream{std::string(text)};
  std::string line;
  std::optional<MacroDocument> current;

  while (macros.size() < capacity && std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty()) {
      continue;
    }
    if (!current) {
      current = ParseHeader(scope, line);
      continue;
    }
    if (EqualsAsciiNoCase(line, "END")) {
      if (std::none_of(macros.begin(), macros.end(), [&](const auto& macro) {
            return macro.id == current->id;
          })) {
        macros.push_back(std::move(*current));
      }
      current.reset();
      continue;
    }
    current->body.append(line);
    current->body.push_back('\n');
  }
  return macros;
}

std::string MacroAccountDataAdapter::Encode(
    const std::vector<MacroDocument>& macros) {
  std::string output;
  for (const auto& macro : macros) {
    output.append("MACRO ");
    output.append(std::to_string(macro.id.value()));
    output.append(" \"");
    output.append(macro.name);
    output.append("\" ");
    output.append(macro.icon_name);
    output.append("\r\n");

    std::istringstream body(macro.body);
    std::string line;
    while (std::getline(body, line)) {
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      if (!line.empty()) {
        output.append(line);
        output.append("\r\n");
      }
    }
    output.append("END\r\n");
  }
  return output;
}

void MacroAccountDataAdapter::Load(MacroCatalog& catalog, MacroScope scope,
                                   std::string_view text) {
  catalog.ReplaceMacros(scope, Decode(scope, text));
}

void MacroAccountDataAdapter::LoadAll(MacroCatalog& catalog,
                                      const AccountData& account_data) {
  Load(catalog, MacroScope::kAccount, account_data.GetMacros(true));
  Load(catalog, MacroScope::kCharacter, account_data.GetMacros(false));
}

void MacroAccountDataAdapter::SaveIfDirty(MacroCatalog& catalog,
                                          AccountData& account_data) {
  if (!catalog.ConsumeDirty()) {
    return;
  }
  account_data.SaveMacros(
      Encode(catalog.SnapshotMacros(MacroScope::kAccount)), true);
  account_data.SaveMacros(
      Encode(catalog.SnapshotMacros(MacroScope::kCharacter)), false);
}

}
