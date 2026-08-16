
#pragma once

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_structures.h"

#include <cstdint>
#include <vector>

namespace openwow::game {

class TaxiFlightCamera {
 public:

  void StartFlight(const openwow::data::dbc::DbcLoader& dbc,
                   std::uint32_t path_id);

  void CancelFlight();

  bool Update(float dt, float& out_x, float& out_y, float& out_z);

  [[nodiscard]] bool IsInFlight() const { return in_flight_; }

  [[nodiscard]] float ElapsedTime() const { return elapsed_; }

 private:
  struct SplineNode {
    float x, y, z;
    std::uint32_t delay_ms;
  };

  bool in_flight_ = false;
  float elapsed_ = 0.0f;
  float speed_ = 32.0f;
  std::vector<SplineNode> nodes_;

  std::size_t current_segment_ = 0;
  float segment_progress_ = 0.0f;
  float segment_length_ = 0.0f;

  void ComputeSegmentLength();

  static void CatmullRom(float p0x, float p0y, float p0z,
                          float p1x, float p1y, float p1z,
                          float p2x, float p2y, float p2z,
                          float p3x, float p3y, float p3z,
                          float t,
                          float& out_x, float& out_y, float& out_z);
};

}
