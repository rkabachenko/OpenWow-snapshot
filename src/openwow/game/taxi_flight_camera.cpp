
#include "openwow/game/taxi_flight_camera.h"

#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <cmath>

namespace openwow::game {

void TaxiFlightCamera::StartFlight(const openwow::data::dbc::DbcLoader& dbc,
                                   std::uint32_t path_id) {
  nodes_.clear();
  current_segment_ = 0;
  segment_progress_ = 0.0f;
  elapsed_ = 0.0f;

  struct TempNode {
    std::uint32_t index;
    float x, y, z;
    std::uint32_t delay;
  };
  std::vector<TempNode> temp;

  for (const auto& pn : dbc.taxi_path_node().entries()) {
    if (pn.path_id == path_id) {
      temp.push_back({pn.node_index, pn.x, pn.y, pn.z, pn.delay});
    }
  }

  std::sort(temp.begin(), temp.end(),
            [](const auto& a, const auto& b) {
              return a.index < b.index;
            });

  if (temp.size() < 2) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "TaxiFlightCamera: path " + std::to_string(path_id)
                           + " has < 2 nodes, cannot fly.");
    in_flight_ = false;
    return;
  }

  nodes_.reserve(temp.size());
  for (const auto& t : temp) {
    nodes_.push_back(SplineNode{t.x, t.y, t.z, t.delay});
  }

  in_flight_ = true;
  ComputeSegmentLength();

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "TaxiFlightCamera: started flight on path "
                         + std::to_string(path_id)
                         + " with " + std::to_string(nodes_.size())
                         + " nodes.");
}

void TaxiFlightCamera::CancelFlight() {
  in_flight_ = false;
  nodes_.clear();
  current_segment_ = 0;
  segment_progress_ = 0.0f;
  elapsed_ = 0.0f;
}

bool TaxiFlightCamera::Update(float dt, float& out_x, float& out_y,
                               float& out_z) {
  if (!in_flight_ || nodes_.size() < 2) {
    in_flight_ = false;
    return false;
  }

  elapsed_ += dt;

  if (segment_length_ > 0.0f) {
    const float distance_this_frame = speed_ * dt;
    segment_progress_ += distance_this_frame / segment_length_;
  }

  while (segment_progress_ >= 1.0f && current_segment_ + 1 < nodes_.size() - 1) {
    segment_progress_ -= 1.0f;
    ++current_segment_;
    ComputeSegmentLength();
  }

  if (current_segment_ + 1 >= nodes_.size()) {
    out_x = nodes_.back().x;
    out_y = nodes_.back().y;
    out_z = nodes_.back().z;
    in_flight_ = false;
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                       "TaxiFlightCamera: flight complete.");
    return false;
  }

  const auto idx = current_segment_;
  const auto n = nodes_.size();

  const auto& p0 = nodes_[idx > 0 ? idx - 1 : 0];
  const auto& p1 = nodes_[idx];
  const auto& p2 = nodes_[idx + 1];
  const auto& p3 = nodes_[idx + 2 < n ? idx + 2 : n - 1];

  const float t = std::clamp(segment_progress_, 0.0f, 1.0f);
  CatmullRom(p0.x, p0.y, p0.z,
             p1.x, p1.y, p1.z,
             p2.x, p2.y, p2.z,
             p3.x, p3.y, p3.z,
             t, out_x, out_y, out_z);

  return true;
}

void TaxiFlightCamera::ComputeSegmentLength() {
  if (current_segment_ + 1 >= nodes_.size()) {
    segment_length_ = 0.0f;
    return;
  }
  const auto& a = nodes_[current_segment_];
  const auto& b = nodes_[current_segment_ + 1];
  const float dx = b.x - a.x;
  const float dy = b.y - a.y;
  const float dz = b.z - a.z;
  segment_length_ = std::sqrt(dx * dx + dy * dy + dz * dz);
}

void TaxiFlightCamera::CatmullRom(float p0x, float p0y, float p0z,
                                   float p1x, float p1y, float p1z,
                                   float p2x, float p2y, float p2z,
                                   float p3x, float p3y, float p3z,
                                   float t,
                                   float& out_x, float& out_y, float& out_z) {
  const float t2 = t * t;
  const float t3 = t2 * t;

  out_x = 0.5f * ((2.0f * p1x)
      + (-p0x + p2x) * t
      + (2.0f * p0x - 5.0f * p1x + 4.0f * p2x - p3x) * t2
      + (-p0x + 3.0f * p1x - 3.0f * p2x + p3x) * t3);
  out_y = 0.5f * ((2.0f * p1y)
      + (-p0y + p2y) * t
      + (2.0f * p0y - 5.0f * p1y + 4.0f * p2y - p3y) * t2
      + (-p0y + 3.0f * p1y - 3.0f * p2y + p3y) * t3);
  out_z = 0.5f * ((2.0f * p1z)
      + (-p0z + p2z) * t
      + (2.0f * p0z - 5.0f * p1z + 4.0f * p2z - p3z) * t2
      + (-p0z + 3.0f * p1z - 3.0f * p2z + p3z) * t3);
}

}
