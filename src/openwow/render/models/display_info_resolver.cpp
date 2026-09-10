#include "openwow/render/models/display_info_resolver.h"

#include "openwow/data/formats/m2/model_path.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <string_view>

namespace openwow::render {

namespace {

constexpr std::string_view kErrorCubeModelPath = "Spells/ErrorCube.m2";

[[nodiscard]] std::string BuildCreatureDisplayTexturePath(
    const std::string_view model_path, const std::string_view texture_name) {
  if (texture_name.empty()) {
    return {};
  }
  const auto separator = model_path.find_last_of("\\/");
  if (separator == std::string_view::npos) {
    return std::string(texture_name);
  }
  std::string path(model_path.substr(0u, separator + 1u));
  path.append(texture_name);
  return path;
}

}

void DisplayInfoResolver::BindDbc(const openwow::data::dbc::DbcLoader* dbc) {
  dbc_ = dbc;
}

CreatureDisplayVisual DisplayInfoResolver::ResolveCreatureDisplay(
    const std::uint32_t display_id,
    const std::string_view effective_model_path) const {
  CreatureDisplayVisual visual;
  if (dbc_ == nullptr) {
    return visual;
  }

  const auto* const display =
      dbc_->creature_display_info().LookupEntry(display_id);
  if (display == nullptr) {
    if (openwow::diagnostics::IsLogEnabled(
            openwow::diagnostics::LogLevel::kWarn)) {
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kWarn,
          "DisplayInfoResolver: unknown creature display id " +
              std::to_string(display_id));
    }
    visual.model_path = std::string(kErrorCubeModelPath);
    return visual;
  }

  const auto* const model =
      dbc_->creature_model_data().LookupEntry(display->model_id);
  if (model == nullptr) {
    if (openwow::diagnostics::IsLogEnabled(
            openwow::diagnostics::LogLevel::kWarn)) {
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kWarn,
          "DisplayInfoResolver: unknown creature model data id " +
              std::to_string(display->model_id) +
              " (display=" + std::to_string(display_id) + ")");
    }
    visual.model_path = std::string(kErrorCubeModelPath);
    return visual;
  }

  visual.resolved = true;
  visual.model_path = openwow::data::m2::NormalizeModelPath(
      std::string(model->model_name));
  if (visual.model_path.empty()) {
    visual.model_path = std::string(kErrorCubeModelPath);
  }
  visual.model_scale = display->scale;
  if (model->scale > 0.0f) {
    visual.model_scale *= model->scale;
  }
  if (!(visual.model_scale > 0.0f)) {
    visual.model_scale = 1.0f;
  }
  visual.mount_height = model->mount_height;
  visual.model_opacity =
      std::clamp(static_cast<float>(display->model_alpha) *
                     (1.0f / 255.0f),
                 0.0f, 1.0f);
  for (std::size_t index = 0u; index < visual.texture_paths.size(); ++index) {
    visual.texture_paths[index] = BuildCreatureDisplayTexturePath(
        effective_model_path.empty() ? std::string_view(visual.model_path)
                                     : effective_model_path,
        display->texture_variation[index]);
  }
  if (display->particle_color_id != 0u) {
    if (const auto* const colors =
            dbc_->particle_color().LookupEntry(display->particle_color_id);
        colors != nullptr) {
      visual.particle_colors = CreatureDisplayParticleColors{
          .start = colors->start,
          .mid = colors->mid,
          .end = colors->end,
      };
    }
  }
  visual.geoset_data = display->creature_geoset_data;
  return visual;
}

std::string DisplayInfoResolver::ResolveCreatureModel(std::uint32_t display_id) const {
  return ResolveCreatureDisplay(display_id).model_path;
}

std::string DisplayInfoResolver::ResolvePlayerModel(std::uint8_t race,
                                                    std::uint8_t gender) const {
  if (dbc_ == nullptr || race == 0) return {};

  const auto* race_entry = dbc_->chr_races().LookupEntry(static_cast<std::uint32_t>(race));
  if (race_entry == nullptr) {
    if (openwow::diagnostics::IsLogEnabled(
            openwow::diagnostics::LogLevel::kWarn)) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                                "DisplayInfoResolver: unknown race id " +
                                    std::to_string(race));
    }
    return {};
  }

  const std::uint32_t display_id =
      (gender == 1) ? race_entry->model_female : race_entry->model_male;

  return ResolveCreatureModel(display_id);
}

std::string DisplayInfoResolver::ResolveGameObjectModel(std::uint32_t display_id) const {
  if (dbc_ == nullptr || display_id == 0) return {};

  const auto* gdi = dbc_->gameobject_display_info().LookupEntry(display_id);
  if (gdi == nullptr) {
    if (openwow::diagnostics::IsLogEnabled(
            openwow::diagnostics::LogLevel::kWarn)) {
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kWarn,
          "DisplayInfoResolver: unknown gameobject display id " +
              std::to_string(display_id));
    }
    return {};
  }

  return openwow::data::m2::NormalizeModelPath(std::string(gdi->filename));
}

std::string DisplayInfoResolver::ResolveItemModelLeft(std::uint32_t display_id) const {
  if (dbc_ == nullptr || display_id == 0) return {};

  const auto* idi = dbc_->item_display_info().LookupEntry(display_id);
  if (idi == nullptr) return {};

  if (idi->model_name_left.empty()) return {};

  std::string path = "item/objectcomponents/weapon/";
  path += openwow::data::m2::NormalizeModelPath(
      std::string(idi->model_name_left));
  return path;
}

std::string DisplayInfoResolver::ResolveItemModelRight(std::uint32_t display_id) const {
  if (dbc_ == nullptr || display_id == 0) return {};

  const auto* idi = dbc_->item_display_info().LookupEntry(display_id);
  if (idi == nullptr) return {};

  if (idi->model_name_right.empty()) return {};

  std::string path = "item/objectcomponents/weapon/";
  path += openwow::data::m2::NormalizeModelPath(
      std::string(idi->model_name_right));
  return path;
}

}
