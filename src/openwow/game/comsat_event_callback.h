
#pragma once

#include <cstdint>

namespace openwow::game {

class ComSatCallback {
public:
  virtual ~ComSatCallback() = default;

  virtual void NotifyLocalTalkerStart(char error) = 0;

  virtual void NotifyLocalTalkerStop(char error) = 0;

  virtual void NotifyTalkerStart(char error, std::uint32_t guid_low,
                                 std::uint32_t guid_high,
                                 std::uint32_t session_lo,
                                 std::uint32_t session_hi) = 0;

  virtual void NotifyTalkerStop(char error, std::uint32_t guid_low,
                                std::uint32_t guid_high,
                                std::uint32_t session_lo,
                                std::uint32_t session_hi) = 0;

  virtual void OnReserved5() = 0;

  virtual void OnReserved6() = 0;
};

class ComSatEventCallback final : public ComSatCallback {
public:
  void NotifyLocalTalkerStart(char error) override;
  void NotifyLocalTalkerStop(char error) override;
  void NotifyTalkerStart(char error, std::uint32_t guid_low,
                         std::uint32_t guid_high, std::uint32_t session_lo,
                         std::uint32_t session_hi) override;
  void NotifyTalkerStop(char error, std::uint32_t guid_low,
                        std::uint32_t guid_high, std::uint32_t session_lo,
                        std::uint32_t session_hi) override;
  void OnReserved5() override;
  void OnReserved6() override;
};

ComSatEventCallback& GetComSatEventCallback();

void ComSatEventCallback_AtExit();

}
