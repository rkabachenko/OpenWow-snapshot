#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace openwow::game {

struct DisplayRecordGeosetSelection {
  static constexpr std::size_t kFamilyCount = 8;

  std::array<std::uint16_t, kFamilyCount> section_ids{};

  [[nodiscard]] bool Empty() const {
    for (const auto section_id : section_ids) {
      if (section_id != 0) {
        return false;
      }
    }

    return true;
  }
};

struct DisplayParticleColorRecord {
  std::array<std::uint32_t, 3> start{};
  std::array<std::uint32_t, 3> mid{};
  std::array<std::uint32_t, 3> end{};
};

struct CreatureDisplayModelOverrides {
  std::array<std::string, 3> texture_paths{};
  bool has_particle_colors = false;
  DisplayParticleColorRecord particle_colors{};
};

struct DisplayInfoEntry {
  std::uint32_t display_id = 0;
  std::uint32_t model_id = 0;
  std::string model_path;
  float model_scale = 1.0f;
  float model_data_scale = 1.0f;
  std::uint32_t model_alpha = 255;
  std::array<std::string, 3> texture_variation{};
  std::uint32_t particle_color_id = 0;
  std::int32_t extra_display_info_id = 0;
  std::uint32_t packed_geoset_selection = 0;

  std::array<float, 3> bounding_box_min{};
  std::array<float, 3> bounding_box_max{};
  std::uint8_t blood_id = 0;
  std::int32_t size_class = -1;
  std::uint32_t npc_sound_id = 0;
  std::uint32_t model_data_npc_sound_id = 0;
  float collision_width = 0.0f;
  float collision_height = 0.0f;
  float mount_height = 0.0f;
  std::uint32_t portrait_texture_file_id = 0;
};

struct ExtraDisplayInfoEntry {
  std::uint32_t id = 0;
  std::uint8_t race = 0;
  std::uint8_t gender = 0;
  std::uint8_t skin_color = 0;
  std::uint8_t face = 0;
  std::uint8_t hair_style = 0;
  std::uint8_t hair_color = 0;
  std::uint8_t facial_hair = 0;
  std::uint32_t helm_display_id = 0;
  std::uint32_t shoulder_display_id = 0;
  std::uint32_t shirt_display_id = 0;
  std::uint32_t chest_display_id = 0;
  std::uint32_t belt_display_id = 0;
  std::uint32_t legs_display_id = 0;
  std::uint32_t boots_display_id = 0;
  std::uint32_t wrist_display_id = 0;
  std::uint32_t gloves_display_id = 0;
  std::uint32_t tabard_display_id = 0;
  std::uint32_t cape_display_id = 0;
  std::uint32_t texture_file_id = 0;
};

class DisplayInfoResolver {
 public:
  static DisplayInfoResolver& Get();

  [[nodiscard]] const DisplayInfoEntry* GetDisplayInfo(
      std::uint32_t display_id) const;
  [[nodiscard]] const ExtraDisplayInfoEntry* GetExtraDisplayInfo(
      std::uint32_t id) const;

  [[nodiscard]] std::string ResolveModelPath(std::uint32_t display_id) const;
  [[nodiscard]] float ResolveModelScale(std::uint32_t display_id) const;
  [[nodiscard]] DisplayRecordGeosetSelection ResolveDisplayRecordGeosets(
      std::uint32_t display_id) const;
  [[nodiscard]] CreatureDisplayModelOverrides ResolveCreatureModelOverrides(
      std::uint32_t display_id) const;
  [[nodiscard]] static DisplayRecordGeosetSelection DecodePackedGeosetSelection(
      std::uint32_t packed_geoset_selection);

  static std::string GetRaceModelPath(std::uint8_t race, std::uint8_t gender);
  static std::string GetShapeshiftModelPath(std::uint8_t form,
                                            std::uint8_t race,
                                            std::uint8_t gender);

  struct EquipmentVisual {
    std::uint32_t item_display_id = 0;
    std::string model_path;
    std::string texture_path;
    std::uint8_t inventory_type = 0;
  };
  [[nodiscard]] EquipmentVisual ResolveEquipmentVisual(
      std::uint32_t item_display_id, std::uint8_t slot) const;

  struct MountVisual {
    std::uint32_t display_id = 0;
    std::string model_path;
    float scale = 1.0f;
  };
  [[nodiscard]] MountVisual ResolveMountVisual(
      std::uint32_t mount_display_id) const;

  void AddDisplayInfo(DisplayInfoEntry entry);
  void AddExtraDisplayInfo(ExtraDisplayInfoEntry entry);
  void AddParticleColorRecord(std::uint32_t id, DisplayParticleColorRecord record);
  void LoadDefaults();
  void Clear();
  [[nodiscard]] std::size_t Count() const;
  [[nodiscard]] std::size_t ExtraCount() const;
  [[nodiscard]] std::optional<DisplayParticleColorRecord> GetParticleColorRecord(
      std::uint32_t id) const;

 private:
  DisplayInfoResolver() = default;
  std::unordered_map<std::uint32_t, DisplayInfoEntry> entries_;
  std::unordered_map<std::uint32_t, ExtraDisplayInfoEntry> extra_entries_;
  std::unordered_map<std::uint32_t, DisplayParticleColorRecord> particle_color_records_;
};

}
