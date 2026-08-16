
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "openwow/game/packet_reader.h"

namespace openwow::data::dbc {
class DbcLoader;
struct SpellEntry;
struct SpellVisualEntry;
struct SpellVisualKitEntry;
struct SpellVisualEffectNameEntry;
}

namespace openwow::game {

struct SpellVisualEvent {
  std::uint64_t target_guid = 0;
  std::uint32_t spell_visual_kit_id = 0;
  bool is_impact = false;
};

struct SpellVisualItemModelContext {
  std::uint32_t display_id{0};
  std::uint32_t inventory_type{0};
};

struct SpellVisualMissTargetContext {
  std::uint64_t guid{0};
  std::uint8_t reason{0};
  std::uint8_t reflect_result{0};
};

struct SpellVisualExtraTargetContext {
  float world_x{0.0f};
  float world_y{0.0f};
  float world_z{0.0f};
};

struct SpellGoVisualData {

  std::uint64_t missile_caster_guid{0};
  std::uint8_t cast_count{0};
  std::uint64_t explicit_target_guid{0};
  std::vector<std::uint64_t> hit_targets;
  std::vector<SpellVisualMissTargetContext> miss_targets;
  std::vector<SpellVisualExtraTargetContext> extra_targets;
  bool has_destination = false;
  float destination_x = 0.0f;
  float destination_y = 0.0f;
  float destination_z = 0.0f;
  std::optional<SpellVisualItemModelContext> missile_item;
  std::optional<SpellVisualItemModelContext> main_hand_weapon;
  std::optional<SpellVisualItemModelContext> off_hand_weapon;
  std::optional<SpellVisualItemModelContext> ranged_weapon;
};

struct CinematicTrigger {
  std::uint32_t cinematic_sequence_id = 0;
};

struct MovieTrigger {
  std::uint32_t movie_id = 0;
};

enum class SpellVisualCategory : std::uint8_t {
  kNone = 0,
  kMelee,
  kRanged,
  kHoly,
  kFire,
  kFrost,
  kNature,
  kShadow,
  kArcane,
  kPhysical,
};

struct SpellVisualKit {
  std::uint32_t kit_id = 0;
  std::string missile_model;
  std::uint32_t missile_sound = 0;
  std::uint32_t impact_kit_id = 0;
  SpellVisualCategory category = SpellVisualCategory::kNone;
  std::uint32_t animation_override = 0;
  float cast_time_scale = 1.0f;
  float missile_speed = 24.0f;
  bool has_missile = false;
};

enum class MissileModelSourceType : std::int32_t {
  kItemComponent2     = -5,
  kItemComponent1     = -4,
  kRangedWeapon       = -3,
  kOffHandWeapon      = -2,
  kMainHandWeapon     = -1,
  kItemDisplayInfo    =  0,

};

struct MissileVisualTimingParams {
  float min_speed{0.25f};
  float speed_scale{0.01f};
  float phase{0.0f};
  std::uint32_t motion_flags{0};

  static MissileVisualTimingParams FromRawDbc(
      std::int32_t raw_speed, std::int32_t raw_scale,
      std::int32_t raw_phase, std::uint32_t flags);
};

struct MissileVisualCreationResult {
  bool success{false};
  std::uint32_t instance_count{0};
  MissileModelSourceType model_source{MissileModelSourceType::kItemDisplayInfo};
  std::string resolved_model_path;
  float model_scale{1.0f};
  MissileVisualTimingParams timing;
  std::uint32_t attachment_type{0};
  std::uint32_t salvo_count{1};
  std::uint32_t visual_flags{0};
  bool pet_owner_flag{false};
  bool area_trigger{false};
};

class SpellVisualHandler {
 public:

  bool HandlePlaySpellVisual(const std::uint8_t* data, std::size_t len);

  bool HandlePlaySpellImpact(const std::uint8_t* data, std::size_t len);

  bool HandleTriggerCinematic(const std::uint8_t* data, std::size_t len);

  bool HandleTriggerMovie(const std::uint8_t* data, std::size_t len);

  void RegisterKit(const SpellVisualKit& kit);
  void MapSpellToKit(std::uint32_t spell_id, std::uint32_t kit_id);
  [[nodiscard]] const SpellVisualKit* ResolveKitForSpell(std::uint32_t spell_id) const;
  [[nodiscard]] const SpellVisualKit* GetKit(std::uint32_t kit_id) const;

  [[nodiscard]] const std::vector<SpellVisualEvent>& visual_events() const {
    return visual_events_;
  }
  [[nodiscard]] std::vector<SpellVisualEvent> GetEventsForTarget(
      std::uint64_t target_guid) const;
  [[nodiscard]] std::vector<SpellVisualEvent> GetRecentEvents(
      std::size_t max_count) const;
  void RemoveEventsForTarget(std::uint64_t target_guid);
  void SetMaxEvents(std::size_t max);
  [[nodiscard]] std::size_t GetEventCount() const;

  const CinematicTrigger& last_cinematic() const { return last_cinematic_; }
  const MovieTrigger& last_movie() const { return last_movie_; }
  bool cinematic_active() const { return cinematic_active_; }
  bool movie_active() const { return movie_active_; }
  void BeginCinematic();
  void StopCinematic();
  void StopMovie();
  void BeginMovie() { movie_active_ = true; }

  [[nodiscard]] static std::string GetCategoryName(SpellVisualCategory cat);
  void Clear();

  struct SpellVisualCreateParams {
    std::uint32_t spell_id{0};
    std::uint64_t caster_guid{0};
    std::uint64_t target_guid{0};
    std::uint32_t effect_index{0};
    std::uint32_t visual_kit_id{0};
    float position[3]{0.0f, 0.0f, 0.0f};
    float orientation[3]{0.0f, 0.0f, 0.0f};
    float scale{1.0f};
    std::uint32_t flags{0};
    std::uint32_t start_time{0};
    std::int32_t missile_model{0};
    std::uint32_t attachment_type_raw{0};
    std::uint32_t spell_visual_flags{0};
    std::uint32_t item_display_id{0};
    std::uint32_t item_inventory_type{0};

    std::uint32_t missile_motion_id{0};

    std::uint32_t duration_ms{0};

    std::int32_t raw_speed{0};
    std::int32_t raw_speed_scale{0};
    std::int32_t raw_phase{0};
    std::uint32_t raw_motion_flags{0};
  };

  [[nodiscard]] std::uint32_t CreateSpellVisualEffect(
      const SpellVisualCreateParams& params);

  [[nodiscard]] MissileVisualCreationResult CreateSpellVisualEffectDetailed(
      const SpellVisualCreateParams& params);

 private:
  bool ParseVisualEvent(const std::uint8_t* data, std::size_t len,
                        bool is_impact);
  void TrimEvents();

  std::vector<SpellVisualEvent> visual_events_;
  CinematicTrigger last_cinematic_{};
  MovieTrigger last_movie_{};
  bool cinematic_active_ = false;
  bool movie_active_ = false;
  std::size_t max_events_ = 256;

  std::unordered_map<std::uint32_t, SpellVisualKit> kits_;
  std::unordered_map<std::uint32_t, std::uint32_t> spell_to_kit_;
};

struct ResolvedSpellVisualEffect {
  std::uint32_t visual_id{0};
  const data::dbc::SpellVisualEntry* visual{nullptr};
  const data::dbc::SpellVisualKitEntry* kit{nullptr};
  const data::dbc::SpellVisualEffectNameEntry* effect{nullptr};
};

[[nodiscard]] ResolvedSpellVisualEffect ResolveSpellVisualEffectRecords(
    const data::dbc::DbcLoader& dbc,
    const data::dbc::SpellEntry& spell,
    std::int32_t violence_level,
    bool emit_diagnostics = true);

[[nodiscard]] const data::dbc::SpellVisualEffectNameEntry*
ResolveSpellVisualEffect(const data::dbc::DbcLoader& dbc,
                         const data::dbc::SpellEntry& spell,
                         std::int32_t violence_level);

void CGUnit_C_InterpolatePosition_Sub01(
    void* self, unsigned int quadrant, float progress);
void CGUnit_C_InterpolatePosition_SubA(
    void* self, unsigned int quadrant, float progress);
void CGUnit_C_InterpolatePosition_SubB(
    void* self, unsigned int quadrant, float progress);
void CGUnit_C_InterpolatePosition_SubC(
    void* self, unsigned int quadrant, float progress);
void CGUnit_C_InterpolatePosition(void* self);

struct LegacyCooldownRenderVertex {
  float x{0.0f};
  float y{0.0f};
  float z{0.0f};
  float u{0.0f};
  float v{0.0f};
};

struct LegacyCooldownRenderEntry {
  std::uint32_t texture_handle{0};
  std::uint32_t render_state_key{0};
  std::uint32_t blend_state_key{0};
  std::uint32_t packed_abgr{0};
  std::uint32_t pixel_shader_handle{0};
  std::array<LegacyCooldownRenderVertex, 10> vertices{};
  std::uint32_t vertex_count{0};
  std::array<std::uint16_t, 12> indices{};
  std::uint32_t index_count{0};
};

using LegacyCooldownBaseRenderCallback = std::function<void(void*, int)>;
using LegacyCooldownPixelShaderResolver =
    std::function<std::uint32_t(std::uint32_t shader_index)>;

void CSimpleCooldown_RegisterLayerRenderCallbacks(
    void* self, int layer_index,
    const LegacyCooldownBaseRenderCallback& base_callback,
    const LegacyCooldownPixelShaderResolver& pixel_shader_resolver,
    std::vector<LegacyCooldownRenderEntry>& out_entries);

}
