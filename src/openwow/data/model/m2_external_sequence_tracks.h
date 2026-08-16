#pragma once

#include "openwow/data/model/m2_model.h"

#include <cstdint>
#include <span>
#include <string>

namespace openwow::data::model {

[[nodiscard]] bool AdoptM2ExternalSequenceTracks(
    M2Model *destination, M2Model &&source,
    std::span<const std::uint16_t> sequence_indices,
    std::string *error = nullptr);

}
