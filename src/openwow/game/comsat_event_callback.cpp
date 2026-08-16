
#include "openwow/game/comsat_event_callback.h"

#include "openwow/game/comsat_client.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <atomic>

namespace openwow::game {

namespace {

std::atomic<std::int32_t> s_local_talker_guard{-1};

std::atomic<std::uint32_t> s_local_talker_active{0};

}

void ComSatEventCallback::NotifyLocalTalkerStart(char error) {
  if (error || s_local_talker_guard.load(std::memory_order_acquire) < 0) {
    return;
  }
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kTrace, "COMSAT_START: NotifyLocalTalkerStart");
  s_local_talker_active.store(1, std::memory_order_release);
  VoiceChat_EnqueueComSatEvent(0, 0, 0, 0, 0);
}

void ComSatEventCallback::NotifyLocalTalkerStop(char error) {
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kTrace, "COMSAT_STOP: NotifyLocalTalkerStop");
  if (!error) {
    s_local_talker_active.store(0, std::memory_order_release);
    VoiceChat_EnqueueComSatEvent(1, 0, 0, 0, 0);
  }
}

void ComSatEventCallback::NotifyTalkerStart(char error,
                                            std::uint32_t guid_low,
                                            std::uint32_t guid_high,
                                            std::uint32_t session_lo,
                                            std::uint32_t session_hi) {
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kTrace, "COMSAT_START: NotifyTalkerStart");
  if (!error) {
    VoiceChat_EnqueueComSatEvent(2, guid_low, guid_high, session_lo,
                                 session_hi);
  }
}

void ComSatEventCallback::NotifyTalkerStop(char error,
                                           std::uint32_t guid_low,
                                           std::uint32_t guid_high,
                                           std::uint32_t session_lo,
                                           std::uint32_t session_hi) {
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kTrace, "COMSAT_STOP: NotifyTalkerStop");
  if (!error) {
    VoiceChat_EnqueueComSatEvent(3, guid_low, guid_high, session_lo,
                                 session_hi);
  }
}

void ComSatEventCallback::OnReserved5() {}

void ComSatEventCallback::OnReserved6() {}

ComSatEventCallback& GetComSatEventCallback() {
  static ComSatEventCallback s_instance;
  return s_instance;
}

void ComSatEventCallback_AtExit() {

  s_local_talker_guard.store(-1, std::memory_order_release);
  s_local_talker_active.store(0, std::memory_order_release);
}

}
