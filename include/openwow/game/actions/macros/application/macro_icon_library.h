#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game::actions::macros {

class MacroIconLibrary {
 public:
  using Loader = std::function<void(std::vector<std::string>& macro_icons,
                                    std::vector<std::string>& item_icons)>;

  void LoadIfNeeded(const Loader& loader);
  void Reset();

  [[nodiscard]] std::uint32_t MacroIconCount() const;
  [[nodiscard]] std::uint32_t ItemIconCount() const;
  [[nodiscard]] std::optional<std::string> MacroIcon(
      std::uint32_t one_based_index) const;
  [[nodiscard]] std::optional<std::string> ItemIcon(
      std::uint32_t one_based_index) const;
  [[nodiscard]] std::vector<std::string> MacroIcons() const;

 private:
  mutable std::mutex mutex_;
  std::vector<std::string> macro_icons_;
  std::vector<std::string> item_icons_;
  bool loaded_{false};
};

}
