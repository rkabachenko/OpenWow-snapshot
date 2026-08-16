#pragma once

#include <atomic>
#include <array>
#include <cctype>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

#include "openwow/core/console.h"
#include "openwow/core/decimal_parse.h"
#include "openwow/core/init_subsystems.h"

namespace openwow::debug {

inline std::atomic<std::uint32_t> g_client_error_display_last_value{0};
inline constexpr std::uint32_t kClientErrorDisplayStartupLastValue = 1u;
inline constexpr std::uint32_t kClientErrorLevelStartupMin = 0u;
inline constexpr std::uint32_t kClientErrorLevelMax = 3u;
inline constexpr std::uint32_t kClientErrorLevelTestResetMin = 0xFFFFFFFFu;
inline constexpr std::uint32_t kClientErrorFilterAllMessages = 0xFFFFFFFFu;
inline constexpr std::size_t kClientErrorFilterCategoryCount = 9u;
inline std::mutex g_client_error_level_mutex;
inline std::uint32_t g_client_error_level_min = kClientErrorLevelTestResetMin;
inline std::uint32_t g_client_error_level_max = kClientErrorLevelMax;
inline std::mutex g_client_error_filter_mutex;
inline std::uint32_t g_client_error_filter_mask = kClientErrorFilterAllMessages;
inline constexpr std::array<std::string_view, kClientErrorFilterCategoryCount>
    kClientErrorFilterCategories = {"general",   "world", "ui",
                                    "animation", "models", "objects",
                                    "sound",     "movement", "network"};

inline std::uint32_t ParseClientErrorDisplayCVarValue(
    const std::string& value) {
  return openwow::core::ParseSignedDecimalLikeSub76F0D0(
      std::string_view(value));
}

inline bool IsClientErrorDisplayShown() {
  return openwow::core::RenderBootstrap_FpsOverlayErrorsShown();
}

inline bool IsClientErrorOutputEnabled() {
  return openwow::core::RenderBootstrap_FpsOverlayErrorsEnabled();
}

inline std::uint32_t GetClientErrorDisplayLastValue() {
  return g_client_error_display_last_value.load(std::memory_order_relaxed);
}

inline void InitializeClientErrorDisplayRuntimeState() {
  g_client_error_display_last_value.store(kClientErrorDisplayStartupLastValue,
                                          std::memory_order_relaxed);
  {
    std::lock_guard lock(g_client_error_level_mutex);
    g_client_error_level_min = kClientErrorLevelStartupMin;
    g_client_error_level_max = kClientErrorLevelMax;
  }
  {
    std::lock_guard lock(g_client_error_filter_mutex);
    g_client_error_filter_mask = kClientErrorFilterAllMessages;
  }
}

inline void ResetClientErrorDisplayCVarState() {
  auto& overlay_state = openwow::core::GetRenderBootstrapFpsOverlayState();
  overlay_state.error_output_enabled = false;
  overlay_state.error_display_hidden = false;
  g_client_error_display_last_value.store(0, std::memory_order_relaxed);
}

inline void SetClientErrorLevelMinRuntimeState(std::uint32_t min_level) {
  std::lock_guard lock(g_client_error_level_mutex);
  g_client_error_level_min = min_level;
  if (g_client_error_level_max < min_level) {
    g_client_error_level_max = min_level;
  }
}

inline void SetClientErrorLevelMaxRuntimeState(std::uint32_t max_level) {
  std::lock_guard lock(g_client_error_level_mutex);
  g_client_error_level_max = max_level;
  if (g_client_error_level_min > max_level) {
    g_client_error_level_min = max_level;
  }
}

inline std::pair<std::uint32_t, std::uint32_t> GetClientErrorLevelRange() {
  std::lock_guard lock(g_client_error_level_mutex);
  return {g_client_error_level_min, g_client_error_level_max};
}

inline std::uint32_t GetClientErrorLevelMin() {
  std::lock_guard lock(g_client_error_level_mutex);
  return g_client_error_level_min;
}

inline std::uint32_t GetClientErrorLevelMax() {
  std::lock_guard lock(g_client_error_level_mutex);
  return g_client_error_level_max;
}

inline void ResetClientErrorLevelCVarState() {
  std::lock_guard lock(g_client_error_level_mutex);
  g_client_error_level_min = kClientErrorLevelTestResetMin;
  g_client_error_level_max = kClientErrorLevelMax;
}

inline std::uint32_t GetClientErrorFilterMask() {
  std::lock_guard lock(g_client_error_filter_mutex);
  return g_client_error_filter_mask;
}

inline void ResetClientErrorFilterCVarState() {
  std::lock_guard lock(g_client_error_filter_mutex);
  g_client_error_filter_mask = kClientErrorFilterAllMessages;
}

inline bool ClientErrorFilterTokenEquals(std::string_view lhs,
                                         std::string_view rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }

  for (std::size_t index = 0; index < lhs.size(); ++index) {
    const auto left = static_cast<unsigned char>(lhs[index]);
    const auto right = static_cast<unsigned char>(rhs[index]);
    if (std::tolower(left) != std::tolower(right)) {
      return false;
    }
  }
  return true;
}

inline std::string_view NextClientErrorFilterToken(std::string_view input,
                                                   std::size_t& cursor) {
  constexpr std::string_view delimiters = "\t\r\n\" ";

  while (cursor < input.size() &&
         delimiters.find(input[cursor]) != std::string_view::npos) {
    ++cursor;
  }

  const std::size_t token_start = cursor;
  while (cursor < input.size() &&
         delimiters.find(input[cursor]) == std::string_view::npos) {
    ++cursor;
  }

  return input.substr(token_start, cursor - token_start);
}

inline void LogClientErrorFilterStatus() {
  const std::uint32_t mask = GetClientErrorFilterMask();
  if (mask == kClientErrorFilterAllMessages) {
    openwow::core::ida::ConsoleAddLine("Now filtering: all messages",
                                       openwow::core::ida::COLOR_DEFAULT);
    return;
  }

  std::string message;
  message.reserve(80);
  for (std::size_t index = 0; index < kClientErrorFilterCategories.size();
       ++index) {
    if ((mask & (1u << index)) == 0u) {
      continue;
    }

    message.append(kClientErrorFilterCategories[index]);
    message.push_back(' ');
  }

  openwow::core::ida::ConsoleLog("Now filtering: %s", message.c_str());
}

inline bool ParseAndStoreClientErrorFilterMask(const std::string& value) {
  std::uint32_t mask = 0;
  bool clear_matching_categories = false;
  std::size_t cursor = 0;
  const std::string_view input(value);

  while (true) {
    const std::string_view token = NextClientErrorFilterToken(input, cursor);
    if (token.empty()) {
      std::lock_guard lock(g_client_error_filter_mutex);
      g_client_error_filter_mask = mask;
      return true;
    }

    bool matched_category = false;
    for (std::size_t index = 0; index < kClientErrorFilterCategories.size();
         ++index) {
      if (!ClientErrorFilterTokenEquals(token,
                                        kClientErrorFilterCategories[index])) {
        continue;
      }

      const std::uint32_t bit = (1u << index);
      if (clear_matching_categories) {
        mask &= ~bit;
      } else {
        mask |= bit;
      }
      matched_category = true;
      break;
    }
    if (matched_category) {
      continue;
    }

    if (ClientErrorFilterTokenEquals(token, "all")) {
      mask = clear_matching_categories ? 0u : kClientErrorFilterAllMessages;
      continue;
    }

    if (ClientErrorFilterTokenEquals(token, "except")) {
      const std::string owned_token(token);
      openwow::core::ida::ConsoleLog("Unknown filter %s", owned_token.c_str());
      openwow::core::ida::ConsoleAddLine(
          "Filters: general world ui animation models objects sound movement all",
          openwow::core::ida::COLOR_DEFAULT);
      openwow::core::ida::ConsoleAddLine(
          "         use \"except\" to invert mask",
          openwow::core::ida::COLOR_DEFAULT);
      openwow::core::ida::ConsoleAddLine("         i.e.: all except objects",
                                         openwow::core::ida::COLOR_DEFAULT);
      return false;
    }

    clear_matching_categories = true;
  }
}

inline bool CVar_Errors_Callback(const std::string&,
                                 const std::string&,
                                 const std::string& new_value) {
  const std::uint32_t parsed = ParseClientErrorDisplayCVarValue(new_value);
  if (parsed != 0) {
    openwow::core::RenderBootstrap_FpsOverlayEnableErrors();
  } else {
    openwow::core::RenderBootstrap_FpsOverlayDisableErrors();
  }

  g_client_error_display_last_value.store(parsed, std::memory_order_relaxed);

  openwow::core::ida::ConsoleAddLine(
      parsed != 0 ? "Error display enabled" : "Error display disabled",
      openwow::core::ida::COLOR_DEFAULT);
  return true;
}

inline bool CVar_ShowErrors_Callback(const std::string&,
                                     const std::string&,
                                     const std::string& new_value) {
  const std::uint32_t parsed = ParseClientErrorDisplayCVarValue(new_value);
  if (parsed != 0) {
    openwow::core::RenderBootstrap_FpsOverlayShowErrors();
  } else {
    openwow::core::RenderBootstrap_FpsOverlayHideErrors();
  }
  g_client_error_display_last_value.store(parsed, std::memory_order_relaxed);

  openwow::core::ida::ConsoleAddLine(
      parsed != 0 ? "Error display shown" : "Error display hidden",
      openwow::core::ida::COLOR_DEFAULT);
  return true;
}

inline void LogClientErrorLevelRangeStatus() {
  const auto [min_level, max_level] = GetClientErrorLevelRange();

  if (min_level == 0u && max_level == 3u) {
    openwow::core::ida::ConsoleAddLine("Displaying all system messages",
                                       openwow::core::ida::COLOR_DEFAULT);
    return;
  }

  std::string text = "Displaying ";
  if (min_level == max_level) {
    text += "only ";
  }

  switch (min_level) {
    case 0:
      text += "informational messages";
      break;
    case 1:
      text += "warnings";
      break;
    case 2:
      text += "errors";
      break;
    case 3:
      text += "fatal errors";
      break;
    default:
      break;
  }

  if (min_level != max_level) {
    switch (max_level) {
      case 1:
        text += " through warnings";
        break;
      case 2:
        text += " through errors";
        break;
      case 3:
        text += " through fatal errors";
        break;
      default:
        break;
    }
  }

  openwow::core::ida::ConsoleAddLine(text, openwow::core::ida::COLOR_DEFAULT);
}

inline bool CVar_ErrorLevelMin_Callback(const std::string&,
                                        const std::string&,
                                        const std::string& new_value) {
  const std::uint32_t parsed = ParseClientErrorDisplayCVarValue(new_value);
  if (parsed > 3u) {
    openwow::core::ida::ConsoleLogColored(
        "%i is not valid, valid values are 0 - %i",
        openwow::core::ida::COLOR_DEFAULT,
        static_cast<std::int32_t>(parsed),
        3);
    return false;
  }

  SetClientErrorLevelMinRuntimeState(parsed);
  LogClientErrorLevelRangeStatus();
  return true;
}

inline bool CVar_ErrorLevelMax_Callback(const std::string&,
                                        const std::string&,
                                        const std::string& new_value) {
  const std::uint32_t parsed = ParseClientErrorDisplayCVarValue(new_value);
  if (parsed > 3u) {
    openwow::core::ida::ConsoleLogColored(
        "%i is not valid, valid values are 0 - %i",
        openwow::core::ida::COLOR_DEFAULT,
        static_cast<std::int32_t>(parsed),
        3);
    return false;
  }

  SetClientErrorLevelMaxRuntimeState(parsed);
  LogClientErrorLevelRangeStatus();
  return true;
}

inline bool CVar_ErrorFilter_Callback(const std::string&,
                                      const std::string&,
                                      const std::string& new_value) {
  if (!ParseAndStoreClientErrorFilterMask(new_value)) {
    return false;
  }

  LogClientErrorFilterStatus();
  return true;
}

}
