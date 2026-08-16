#pragma once
#include "openwow/data/formats/dbc/dbc_loader.h"

#include <cstdint>
#include <string>

namespace openwow::render {

class DisplayInfoResolver {
 public:
  DisplayInfoResolver() = default;

  void BindDbc(const openwow::data::dbc::DbcLoader* dbc);

  [[nodiscard]] bool IsReady() const { return dbc_ != nullptr; }

  [[nodiscard]] std::string ResolveCreatureModel(std::uint32_t display_id) const;

  [[nodiscard]] float GetCreatureModelScale(std::uint32_t display_id) const;

  [[nodiscard]] std::string ResolvePlayerModel(std::uint8_t race,
                                               std::uint8_t gender) const;

  [[nodiscard]] std::string ResolveGameObjectModel(std::uint32_t display_id) const;

  [[nodiscard]] std::string ResolveItemModelLeft(std::uint32_t display_id) const;

  [[nodiscard]] std::string ResolveItemModelRight(std::uint32_t display_id) const;

 private:
  const openwow::data::dbc::DbcLoader* dbc_{nullptr};

};

}
