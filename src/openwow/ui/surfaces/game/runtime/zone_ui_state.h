#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace openwow::game {
class WorldSession;
}
namespace openwow::ui {
class MinimapSystem;
class WorldMapSystem;
}

namespace openwow::ui::game {

class AreaId {
public:
  constexpr AreaId() = default;
  explicit constexpr AreaId(const std::int32_t value) : value_(value) {}

  [[nodiscard]] constexpr std::int32_t value() const {
    return value_;
  }

  friend constexpr bool operator==(AreaId, AreaId) = default;

private:
  std::int32_t value_{0};
};

class ZoneId {
public:
  constexpr ZoneId() = default;
  explicit constexpr ZoneId(const std::int32_t value) : value_(value) {}

  [[nodiscard]] constexpr std::int32_t value() const {
    return value_;
  }

  friend constexpr bool operator==(ZoneId, ZoneId) = default;

private:
  std::int32_t value_{0};
};

class MapId {
public:
  constexpr MapId() = default;
  explicit constexpr MapId(const std::int32_t value) : value_(value) {}

  [[nodiscard]] constexpr std::int32_t value() const {
    return value_;
  }

  friend constexpr bool operator==(MapId, MapId) = default;

private:
  std::int32_t value_{0};
};

struct ZoneUiLocation {
  AreaId area;
  ZoneId zone;
  MapId map;
};

struct ZoneUiText {
  std::string_view zone;
  std::string_view subzone;
  std::string_view real_zone;
};

struct ZoneUiUpdate {
  ZoneUiLocation location;
  ZoneUiText text;
  bool is_instance{false};
};

class ZoneUiState {
public:
  ZoneUiState(openwow::ui::MinimapSystem& minimap,
              openwow::ui::WorldMapSystem& world_map)
      : minimap_(minimap), world_map_(world_map) {}
  void Apply(const ZoneUiUpdate &update, openwow::game::WorldSession *session);
  void Reset();

  [[nodiscard]] AreaId area_id() const {
    return location_.area;
  }

  [[nodiscard]] ZoneId zone_id() const {
    return location_.zone;
  }

  [[nodiscard]] MapId map_id() const {
    return location_.map;
  }

  [[nodiscard]] const std::string &zone_text() const {
    return zone_text_;
  }

  [[nodiscard]] const std::string &subzone_text() const {
    return subzone_text_;
  }

  [[nodiscard]] const std::string &real_zone_text() const {
    return real_zone_text_;
  }

private:
  openwow::ui::MinimapSystem& minimap_;
  openwow::ui::WorldMapSystem& world_map_;
  ZoneUiLocation location_;
  std::string zone_text_;
  std::string subzone_text_;
  std::string real_zone_text_;
};

}
