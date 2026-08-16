#include "openwow/render/models/display_info_resolver.h"

#include "openwow/data/formats/m2/model_path.h"
#include "openwow/foundation/diagnostics/logging.h"

namespace openwow::render {

namespace {

constexpr std::string_view kErrorCubeModelPath = "Spells/ErrorCube.m2";

}

void DisplayInfoResolver::BindDbc(const openwow::data::dbc::DbcLoader* dbc) {
  dbc_ = dbc;
}

std::string DisplayInfoResolver::ResolveCreatureModel(std::uint32_t display_id) const {
  if (dbc_ == nullptr) return {};

  const auto& display_store = dbc_->creature_display_info();
  const auto* cdi = display_store.LookupEntry(display_id);
  if (cdi == nullptr) {
    if (openwow::diagnostics::IsLogEnabled(
            openwow::diagnostics::LogLevel::kWarn)) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                                "DisplayInfoResolver: unknown creature display id " +
                                    std::to_string(display_id));
    }
    return std::string(kErrorCubeModelPath);
  }

  const auto* cmd = dbc_->creature_model_data().LookupEntry(cdi->model_id);
  if (cmd == nullptr) {
    if (openwow::diagnostics::IsLogEnabled(
            openwow::diagnostics::LogLevel::kWarn)) {
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kWarn,
          "DisplayInfoResolver: unknown creature model data id " +
              std::to_string(cdi->model_id) +
              " (display=" + std::to_string(display_id) + ")");
    }
    return std::string(kErrorCubeModelPath);
  }

  auto model_path = openwow::data::m2::NormalizeModelPath(
      std::string(cmd->model_name));
  return model_path.empty() ? std::string(kErrorCubeModelPath)
                            : std::move(model_path);
}

float DisplayInfoResolver::GetCreatureModelScale(std::uint32_t display_id) const {
  if (dbc_ == nullptr || display_id == 0) return 1.0f;

  const auto* cdi = dbc_->creature_display_info().LookupEntry(display_id);
  if (cdi == nullptr) return 1.0f;

  float scale = cdi->scale;

  const auto* cmd = dbc_->creature_model_data().LookupEntry(cdi->model_id);
  if (cmd != nullptr && cmd->scale > 0.0f) {
    scale *= cmd->scale;
  }

  return (scale > 0.0f) ? scale : 1.0f;
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
