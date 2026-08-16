
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string_view>

namespace openwow::core {

enum class TimingMethod : int {
    Auto               = 0,
    TickCount          = 1,
    PerformanceCounter = 2,
};

struct TimingMethodSelection {
    TimingMethod selected_method = TimingMethod::TickCount;
    int validation_result = 0;
};

[[nodiscard]] constexpr std::string_view TimingMethodNameFromIdaValue(
    int value) noexcept {
    switch (value) {
        case -1: return "[Not Set]";
        case 0: return "[Best Available]";
        case 1: return "GetTickCount";
        case 2: return "QueryPerformanceCounter";
        default: return "[Unknown]";
    }
}

[[nodiscard]] constexpr std::string_view TimingMethodName(TimingMethod m) noexcept {
    return TimingMethodNameFromIdaValue(static_cast<int>(m));
}

[[nodiscard]] constexpr TimingMethod TimingMethodFromCVarValue(int value) noexcept {
    switch (value) {
        case 1: return TimingMethod::TickCount;
        case 2: return TimingMethod::PerformanceCounter;
        default: return TimingMethod::Auto;
    }
}

[[nodiscard]] constexpr TimingMethod ResolveRecommendedTimingMethod(
    bool performance_counter_validated) noexcept {
    return performance_counter_validated ? TimingMethod::PerformanceCounter
                                         : TimingMethod::TickCount;
}

[[nodiscard]] constexpr TimingMethodSelection ResolveTimingMethodSelection(
    TimingMethod requested,
    TimingMethod recommended,
    int validation_result) noexcept {
    TimingMethodSelection selection{
        recommended,
        validation_result,
    };

    if (requested == TimingMethod::Auto || requested == recommended) {
        return selection;
    }

    selection.selected_method = requested;
    if (requested == TimingMethod::PerformanceCounter &&
        recommended == TimingMethod::TickCount) {
        selection.validation_result = 5;
    }

    return selection;
}

namespace detail {

struct PerformanceCounterCalibrationSample {
    std::int64_t counter_ticks = 0;
    std::uint32_t tick_count_ms = 0;
};

[[nodiscard]] constexpr std::uint32_t WrappedTickCountDelta(
    std::uint32_t current_tick_count_ms,
    std::uint32_t previous_tick_count_ms) noexcept {
    return current_tick_count_ms - previous_tick_count_ms;
}

struct PerformanceCounterScaleFactors {
    double nanoseconds_per_tick = 0.0;
    double microseconds_per_tick = 0.0;
    double milliseconds_per_tick = 0.0;
    double seconds_per_tick = 0.0;
};

[[nodiscard]] inline PerformanceCounterScaleFactors
ComputePerformanceCounterScaleFactors(const std::int64_t frequency_hz) noexcept {
    if (frequency_hz <= 0) {
        return {};
    }

    const double seconds_per_tick = 1.0 / static_cast<double>(frequency_hz);
    return {
        1000000000.0 * seconds_per_tick,
        1000000.0 * seconds_per_tick,
        1000.0 * seconds_per_tick,
        seconds_per_tick,
    };
}

[[nodiscard]] inline std::int64_t ComputeCurrentTimeNsSince2000FromCounter(
    const std::int64_t baseline_time_ns_since_2000,
    const std::int64_t current_counter_ticks,
    const std::int64_t baseline_counter_ticks,
    const double nanoseconds_per_tick) noexcept {
    const auto delta_ticks = current_counter_ticks - baseline_counter_ticks;
    const auto delta_nanoseconds = static_cast<std::int64_t>(
        static_cast<double>(delta_ticks) * nanoseconds_per_tick);
    return baseline_time_ns_since_2000 + delta_nanoseconds;
}

[[nodiscard]] inline std::uint64_t ComputeTickCount64FromCounter(
    const std::int64_t counter_ticks,
    const double milliseconds_per_tick) noexcept {
    return static_cast<std::uint64_t>(
        static_cast<double>(counter_ticks) * milliseconds_per_tick);
}

[[nodiscard]] inline bool PerformanceCounterFailedMonotonicityCheck(
    const std::int64_t current_counter_ticks,
    const std::int64_t previous_counter_ticks) noexcept {
    return current_counter_ticks <= previous_counter_ticks;
}

[[nodiscard]] inline std::int64_t ComputePerformanceCounterElapsedMs(
    const std::int64_t counter_delta_ticks,
    const std::int64_t frequency_hz) noexcept {
    if (frequency_hz <= 0) {
        return 0;
    }

    return static_cast<std::int64_t>(
        static_cast<double>(counter_delta_ticks) /
        static_cast<double>(frequency_hz) * 1000.0);
}

[[nodiscard]] inline int ValidatePerformanceCounterTickDrift(
    const std::int64_t frequency_hz,
    const std::int64_t start_counter_ticks,
    const std::int64_t end_counter_ticks,
    const std::uint32_t start_tick_count_ms,
    const std::uint32_t end_tick_count_ms) noexcept {
    const auto observed_elapsed_ms = static_cast<std::int64_t>(
        WrappedTickCountDelta(end_tick_count_ms, start_tick_count_ms));
    const auto counter_elapsed_ms = ComputePerformanceCounterElapsedMs(
        end_counter_ticks - start_counter_ticks, frequency_hz);
    const auto absolute_difference_ms =
        observed_elapsed_ms >= counter_elapsed_ms
            ? observed_elapsed_ms - counter_elapsed_ms
            : counter_elapsed_ms - observed_elapsed_ms;
    return absolute_difference_ms >= 5 ? 3 : 0;
}

struct TickCount64State {
    std::uint64_t high_bits_ms = 0;
    std::uint32_t last_tick_count_ms = 0;
    bool initialized = false;
};

[[nodiscard]] inline std::uint64_t ExtendWrappedTickCount32(
    const std::uint32_t current_tick_count_ms,
    TickCount64State& state) noexcept {
    if (!state.initialized) {
        state.last_tick_count_ms = current_tick_count_ms;
        state.initialized = true;
        return current_tick_count_ms;
    }

    if (current_tick_count_ms < state.last_tick_count_ms) {
        state.high_bits_ms += 0x100000000ULL;
    }

    state.last_tick_count_ms = current_tick_count_ms;
    return state.high_bits_ms + current_tick_count_ms;
}

[[nodiscard]] constexpr std::uint64_t ResolveTimingCounterFrequencyHz(
    const TimingMethod selected_method,
    const std::int64_t performance_counter_frequency_hz) noexcept {
    return selected_method == TimingMethod::PerformanceCounter &&
                   performance_counter_frequency_hz > 0
               ? static_cast<std::uint64_t>(performance_counter_frequency_hz)
               : 1000ull;
}

[[nodiscard]] inline double ConvertTimingCounterDeltaToMilliseconds(
    const std::uint64_t counter_delta,
    const std::uint64_t counter_frequency_hz) noexcept {
    if (counter_frequency_hz == 0) {
        return 0.0;
    }

    return static_cast<double>(counter_delta) * 1000.0 /
           static_cast<double>(counter_frequency_hz);
}

[[nodiscard]] double UpdatePerformanceCounterDriftRatio(
    std::int64_t frequency_hz,
    PerformanceCounterCalibrationSample current_sample,
    PerformanceCounterCalibrationSample& previous_sample) noexcept;

}

class GameClock {
public:

    using SteadyTimePoint = std::chrono::steady_clock::time_point;
    using Duration        = std::chrono::steady_clock::duration;

    static GameClock& Instance();

    GameClock(const GameClock&) = delete;
    GameClock& operator=(const GameClock&) = delete;

    void Init(TimingMethod requested_method);

    [[nodiscard]] TimingMethod GetTimingMethod() const noexcept;

    double Tick();

    [[nodiscard]] static SteadyTimePoint Now() noexcept;

    [[nodiscard]] static double ElapsedMs(SteadyTimePoint start) noexcept;

    [[nodiscard]] static double ElapsedSec(SteadyTimePoint start) noexcept;

    [[nodiscard]] double FrameDelta() const noexcept;

    [[nodiscard]] double SmoothedFPS() const noexcept;

    [[nodiscard]] double FrameDeltaMs() const noexcept;

    [[nodiscard]] double TotalElapsedMs() const noexcept;

    [[nodiscard]] double TotalElapsedSec() const noexcept;

    [[nodiscard]] std::uint64_t FrameCount() const noexcept;

    [[nodiscard]] SteadyTimePoint LastTickTime() const noexcept;

    [[nodiscard]] static std::uint32_t GetTickCount32() noexcept;

    [[nodiscard]] static std::uint64_t GetRawTimingCounter() noexcept;

    [[nodiscard]] static std::uint64_t GetTimingCounterFrequencyHz() noexcept;

    [[nodiscard]] static double GetTickCountSeconds() noexcept;

    [[nodiscard]] static std::uint64_t GetTickCount64() noexcept;

    [[nodiscard]] static std::int64_t GetCurrentTimeNsSince2000() noexcept;

    [[nodiscard]] int ValidationResult() const noexcept;

private:
    GameClock();

    void ResolveMethod();

    TimingMethod           requested_method_{TimingMethod::Auto};
    TimingMethod           selected_method_{TimingMethod::Auto};
    bool                   initialised_{false};

    SteadyTimePoint        epoch_;
    SteadyTimePoint        lastTickTime_;
    double                 frameDelta_{0.0};
    std::atomic<double>    totalSec_{0.0};
    std::atomic<uint64_t>  frameCount_{0};
    int                    validationResult_{-1};

    static constexpr int kFpsRingSize = 30;
    double               fpsRing_[kFpsRingSize]{};
    int                  fpsRingHead_{0};
};

}
