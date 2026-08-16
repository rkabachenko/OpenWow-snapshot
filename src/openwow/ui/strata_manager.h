#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::ui {

enum class RenderStrata : uint8_t {
  World = 0,
  Background = 1,
  Low = 2,
  Medium = 3,
  High = 4,
  Dialog = 5,
  Fullscreen = 6,
  FullscreenDialog = 7,
  Tooltip = 8,
};

using FrameLevel = int32_t;

struct StrataEntry {
  std::string frameName;
  RenderStrata strata{RenderStrata::Medium};
  FrameLevel level{0};
  bool isShown{true};
};

class StrataManager {
 public:
  StrataManager() = default;
  ~StrataManager() = default;

  void SetStrata(const std::string& frameName, RenderStrata strata,
                 FrameLevel level);

  [[nodiscard]] RenderStrata GetStrata(const std::string& frameName) const;

  [[nodiscard]] FrameLevel GetLevel(const std::string& frameName) const;

  void SetShown(const std::string& frameName, bool shown);

  [[nodiscard]] bool IsShown(const std::string& frameName) const;

  [[nodiscard]] std::vector<StrataEntry> GetVisibleFrames() const;

  [[nodiscard]] std::vector<StrataEntry> GetFramesByStrata(
      RenderStrata strata) const;

  [[nodiscard]] std::size_t GetFrameCount() const;

  [[nodiscard]] std::size_t GetVisibleCount() const;

  [[nodiscard]] std::vector<std::string> GetRenderOrder() const;

  void Raise(const std::string& frameName);

  void Lower(const std::string& frameName);

  void Reset();

 private:
  struct StoredEntry {
    StrataEntry entry;
    std::uint64_t creation_order{0};
  };

  [[nodiscard]] FrameLevel CountVisibleInStrata(RenderStrata strata) const;
  void CompactVisibleLevels(RenderStrata strata);

  std::unordered_map<std::string, StoredEntry> entries_;
  std::uint64_t next_creation_order_{0};
};

}
