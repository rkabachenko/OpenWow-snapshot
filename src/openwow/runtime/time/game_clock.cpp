
#include "openwow/runtime/time/game_clock.h"
#include "openwow/runtime/time/game_time.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <cmath>
#include <limits>
#include <mutex>
#include <string>
#include <thread>

namespace openwow::core {

namespace {

using Clock = std::chrono::steady_clock;
using HundredNanoseconds = std::chrono::duration<std::int64_t, std::ratio<1, 10000000>>;

constexpr std::int64_t kPerformanceCounterFrequencyHz = 1000000000;
constexpr std::uint64_t kUnixEpochFileTimeOffsetTicks = 116444736000000000ULL;

struct SharedTimerBackendState {
    bool performance_counter_supported = false;
    int validation_result = 1;
    std::int64_t performance_counter_frequency_hz = 0;
    std::int64_t baseline_counter_ticks = 0;
    std::int64_t baseline_time_ns_since_2000 = 0;
    detail::PerformanceCounterScaleFactors scale_factors{};
};

struct TickCount64FallbackState {
    std::mutex mutex;
    detail::TickCount64State state{};
};

[[nodiscard]] std::int64_t ReadPerformanceCounterTicks() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               Clock::now().time_since_epoch())
        .count();
}

[[nodiscard]] std::uint32_t ReadRawTickCount32() noexcept {
    return static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now().time_since_epoch())
            .count());
}

[[nodiscard]] std::uint64_t ReadSystemClockFileTimeTicks() noexcept {
    const auto unix_ticks = std::chrono::duration_cast<HundredNanoseconds>(
                                std::chrono::system_clock::now().time_since_epoch())
                                .count();
    return static_cast<std::uint64_t>(unix_ticks) + kUnixEpochFileTimeOffsetTicks;
}

[[nodiscard]] std::int64_t ReadSystemTimeNsSince2000() noexcept {
    return ida::TimeNsSince2000FromFileTimeTicks(ReadSystemClockFileTimeTicks());
}

[[nodiscard]] bool IsPerformanceCounterBackendSupported() noexcept {
    using Period = Clock::period;

    if constexpr (Period::num == 0) {
        return false;
    }

    const auto first = Clock::now();
    const auto second = Clock::now();
    return second >= first;
}

void YieldTimingProbeThread() noexcept {
    std::this_thread::yield();
}

[[nodiscard]] std::uint32_t WaitForTickCountChange(
    std::uint32_t previous_tick_count_ms) noexcept {
    std::uint32_t tick_count_ms = previous_tick_count_ms;
    do {
        tick_count_ms = ReadRawTickCount32();
    } while (tick_count_ms == previous_tick_count_ms);
    return tick_count_ms;
}

[[nodiscard]] std::uint32_t GetLogicalProcessorCount() noexcept {
    const auto processor_count = std::thread::hardware_concurrency();
    return processor_count == 0 ? 1u : processor_count;
}

[[nodiscard]] int ProbePerformanceCounterValidation(
    const std::int64_t frequency_hz) noexcept {
    if (frequency_hz <= 0) {
        return 2;
    }

    YieldTimingProbeThread();
    const auto start_tick_count_ms = ReadRawTickCount32();
    const auto calibration_start_tick_ms =
        WaitForTickCountChange(start_tick_count_ms);
    const auto calibration_start_counter_ticks = ReadPerformanceCounterTicks();

    if (GetLogicalProcessorCount() > 1) {
        auto previous_counter_ticks = calibration_start_counter_ticks;
        for (std::uint32_t attempt = 0; attempt < 0x200; ++attempt) {
            YieldTimingProbeThread();
            const auto current_counter_ticks = ReadPerformanceCounterTicks();
            if (detail::PerformanceCounterFailedMonotonicityCheck(
                    current_counter_ticks, previous_counter_ticks)) {
                return 4;
            }
            previous_counter_ticks = current_counter_ticks;
        }
    }

    auto drift_window_start_tick_ms = calibration_start_tick_ms;
    do {
        drift_window_start_tick_ms = ReadRawTickCount32();
    } while (detail::WrappedTickCountDelta(
                 drift_window_start_tick_ms,
                 calibration_start_tick_ms) < 250u);

    const auto drift_window_end_tick_ms =
        WaitForTickCountChange(drift_window_start_tick_ms);
    const auto drift_window_end_counter_ticks = ReadPerformanceCounterTicks();
    return detail::ValidatePerformanceCounterTickDrift(
        frequency_hz,
        calibration_start_counter_ticks,
        drift_window_end_counter_ticks,
        calibration_start_tick_ms,
        drift_window_end_tick_ms);
}

SharedTimerBackendState CreateSharedTimerBackendState() noexcept {
    SharedTimerBackendState state{};
    if (!IsPerformanceCounterBackendSupported()) {
        state.validation_result = 1;
        return state;
    }

    state.performance_counter_supported = true;
    state.performance_counter_frequency_hz = kPerformanceCounterFrequencyHz;
    state.validation_result = ProbePerformanceCounterValidation(
        state.performance_counter_frequency_hz);

    if (state.validation_result != 0) {
        return state;
    }

    state.baseline_counter_ticks = ReadPerformanceCounterTicks();
    state.baseline_time_ns_since_2000 = ReadSystemTimeNsSince2000();
    state.scale_factors = detail::ComputePerformanceCounterScaleFactors(
        state.performance_counter_frequency_hz);
    return state;
}

SharedTimerBackendState& MutableSharedTimerBackendState() {
    static SharedTimerBackendState state;
    return state;
}

std::once_flag& SharedTimerBackendStateInitFlag() {
    static std::once_flag flag;
    return flag;
}

const SharedTimerBackendState& GetSharedTimerBackendState() noexcept {
    auto& state = MutableSharedTimerBackendState();
    std::call_once(SharedTimerBackendStateInitFlag(), [&state] {
        state = CreateSharedTimerBackendState();
    });
    return state;
}

TickCount64FallbackState& GetTickCount64FallbackState() {
    static TickCount64FallbackState state;
    return state;
}

}

double detail::UpdatePerformanceCounterDriftRatio(
    const std::int64_t frequency_hz,
    const PerformanceCounterCalibrationSample current_sample,
    PerformanceCounterCalibrationSample& previous_sample) noexcept {
    if (frequency_hz <= 0) {
        previous_sample = current_sample;
        return std::numeric_limits<double>::infinity();
    }

    const auto counter_delta_ticks =
        current_sample.counter_ticks - previous_sample.counter_ticks;
    const double counter_seconds = static_cast<double>(counter_delta_ticks) /
                                   static_cast<double>(frequency_hz);
    const double tick_seconds = static_cast<double>(WrappedTickCountDelta(
                                    current_sample.tick_count_ms,
                                    previous_sample.tick_count_ms)) *
                                0.001;

    previous_sample = current_sample;

    if (counter_seconds <= 0.0) {
        return std::numeric_limits<double>::infinity();
    }

    return std::fabs(counter_seconds - tick_seconds) / counter_seconds;
}

GameClock& GameClock::Instance() {
    static GameClock instance;
    return instance;
}

GameClock::GameClock()
    : epoch_(std::chrono::steady_clock::now()),
      lastTickTime_(epoch_) {
    ResolveMethod();
}

void GameClock::Init(const TimingMethod requested_method) {
    if (initialised_) return;

    requested_method_ = requested_method;
    ResolveMethod();

    epoch_        = std::chrono::steady_clock::now();
    lastTickTime_ = epoch_;

    initialised_ = true;

    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
        std::string("GameClock initialised — method: ") +
        std::to_string(static_cast<int>(selected_method_)) + " (" +
        std::string(TimingMethodName(selected_method_)) + ")");
}

TimingMethod GameClock::GetTimingMethod() const noexcept {
    return selected_method_;
}

void GameClock::ResolveMethod() {
    const auto& backend = GetSharedTimerBackendState();
    const auto recommended_method = ResolveRecommendedTimingMethod(
        backend.validation_result == 0);
    const auto selection = ResolveTimingMethodSelection(
        requested_method_, recommended_method, backend.validation_result);
    validationResult_ = selection.validation_result;
    selected_method_ = selection.selected_method;
}

double GameClock::Tick() {
    if (!initialised_) {
        Init(TimingMethod::Auto);
    }

    const auto now = std::chrono::steady_clock::now();
    const std::chrono::duration<double> dt = now - lastTickTime_;
    frameDelta_   = dt.count();
    lastTickTime_ = now;

    fpsRing_[fpsRingHead_] = frameDelta_;
    fpsRingHead_ = (fpsRingHead_ + 1 == kFpsRingSize) ? 0 : fpsRingHead_ + 1;

    const std::chrono::duration<double> total = now - epoch_;
    totalSec_.store(total.count(), std::memory_order_relaxed);
    frameCount_.fetch_add(1, std::memory_order_relaxed);

    return frameDelta_;
}

GameClock::SteadyTimePoint GameClock::Now() noexcept {
    return std::chrono::steady_clock::now();
}

double GameClock::ElapsedMs(SteadyTimePoint start) noexcept {
    const auto elapsed =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start);
    return elapsed.count();
}

double GameClock::ElapsedSec(SteadyTimePoint start) noexcept {
    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
    return elapsed.count();
}

double GameClock::FrameDelta() const noexcept {
    return frameDelta_;
}

double GameClock::SmoothedFPS() const noexcept {
    double sum = 0.0;
    for (int i = 0; i < kFpsRingSize; ++i) {
        sum += fpsRing_[i];
    }
    const double avg_time = sum * 0.033333335;
    if (avg_time >= 0.001) {
        return 1.0 / avg_time;
    }
    return 1000.0;
}

double GameClock::FrameDeltaMs() const noexcept {
    return frameDelta_ * 1000.0;
}

double GameClock::TotalElapsedMs() const noexcept {
    return totalSec_.load(std::memory_order_relaxed) * 1000.0;
}

double GameClock::TotalElapsedSec() const noexcept {
    return totalSec_.load(std::memory_order_relaxed);
}

std::uint64_t GameClock::FrameCount() const noexcept {
    return frameCount_.load(std::memory_order_relaxed);
}

GameClock::SteadyTimePoint GameClock::LastTickTime() const noexcept {
    return lastTickTime_;
}

std::uint32_t GameClock::GetTickCount32() noexcept {

    return ReadRawTickCount32();
}

std::uint64_t GameClock::GetRawTimingCounter() noexcept {
    const auto selected_method = Instance().GetTimingMethod();
    if (selected_method == TimingMethod::PerformanceCounter) {
        return static_cast<std::uint64_t>(ReadPerformanceCounterTicks());
    }

    return static_cast<std::uint64_t>(ReadRawTickCount32());
}

std::uint64_t GameClock::GetTimingCounterFrequencyHz() noexcept {
    const auto selected_method = Instance().GetTimingMethod();
    const auto& backend = GetSharedTimerBackendState();
    return detail::ResolveTimingCounterFrequencyHz(
        selected_method, backend.performance_counter_frequency_hz);
}

double GameClock::GetTickCountSeconds() noexcept {
    return static_cast<double>(GetTickCount32()) * 0.001;
}

std::uint64_t GameClock::GetTickCount64() noexcept {
    const auto selected_method = Instance().GetTimingMethod();
    const auto& backend = GetSharedTimerBackendState();
    if (selected_method == TimingMethod::PerformanceCounter &&
        backend.performance_counter_supported &&
        backend.performance_counter_frequency_hz > 0) {
        return detail::ComputeTickCount64FromCounter(
            ReadPerformanceCounterTicks(),
            backend.scale_factors.milliseconds_per_tick);
    }

    auto& fallback = GetTickCount64FallbackState();
    std::lock_guard lock(fallback.mutex);
    return detail::ExtendWrappedTickCount32(
        ReadRawTickCount32(), fallback.state);
}

std::int64_t GameClock::GetCurrentTimeNsSince2000() noexcept {
    const auto& backend = GetSharedTimerBackendState();
    if (!backend.performance_counter_supported ||
        backend.performance_counter_frequency_hz <= 0) {
        return ReadSystemTimeNsSince2000();
    }

    return detail::ComputeCurrentTimeNsSince2000FromCounter(
        backend.baseline_time_ns_since_2000,
        ReadPerformanceCounterTicks(),
        backend.baseline_counter_ticks,
        backend.scale_factors.nanoseconds_per_tick);
}

int GameClock::ValidationResult() const noexcept {
    return validationResult_;
}

}
