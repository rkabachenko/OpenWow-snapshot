#pragma once

#include <array>

namespace openwow::game::camera {

struct CameraViewPresetDefaults {
  const char* distance;
  const char* pitch_degrees;
  const char* yaw_degrees;
};

inline constexpr std::array<const char*, 8> kRetailCameraViewSuffixes{
    "", "A", "B", "C", "D", "E", "Com", "Barber Shop"};

inline constexpr std::array<CameraViewPresetDefaults, 8>
    kRetailCameraViewDefaults{{
        {"0.0", "0.0", "0.0"},
        {"0.0", "0.0", "0.0"},
        {"5.55", "10.0", "0.0"},
        {"5.55", "20.0", "0.0"},
        {"13.88", "30.0", "0.0"},
        {"13.88", "10.0", "0.0"},
        {"0.0", "0.0", "0.0"},
        {"5.0", "10.0", "0.0"},
    }};

inline constexpr std::array<const char*, 5> kRetailCameraSmoothStyleSuffixes{
    "Never", "Smart", "Always", "Spline", "Smarter"};

inline constexpr std::array<const char*, 7> kRetailCameraSmoothEventSuffixes{
    "Idle", "Stop", "Track", "Move", "Strafe", "Turn", "Fear"};

inline constexpr std::array<const char*, 3> kRetailCameraSmoothAxisSuffixes{
    "Distance", "Pitch", "Yaw"};

inline constexpr std::array<const char*, 2>
    kRetailCameraSmoothParameterSuffixes{"Delay", "Factor"};

struct CameraSmoothDelayFactorDefaults {
  const char* delay;
  const char* factor;
};

inline constexpr std::array<CameraSmoothDelayFactorDefaults, 35>
    kRetailCameraSmoothEventDefaults{{

        {"0.0", "0.0"}, {"0.0", "0.0"}, {"0.0", "0.0"}, {"0.0", "0.0"},
        {"0.0", "0.0"}, {"0.0", "0.0"}, {"0.0", "0.0"},

        {"0.0", "0.0"}, {"0.0", "0.0"}, {"0.4", "10.0"}, {"0.0", "1.0"},
        {"0.0", "1.0"}, {"0.0", "1.0"}, {"0.4", "10.0"},

        {"0.0", "1.0"}, {"0.0", "1.0"}, {"0.0", "1.0"}, {"0.0", "1.0"},
        {"0.0", "1.0"}, {"0.0", "1.0"}, {"0.0", "1.0"},

        {"0.0", "4.0"}, {"0.0", "4.0"}, {"0.0", "4.0"}, {"0.0", "1.0"},
        {"0.0", "1.0"}, {"0.0", "1.0"}, {"0.0", "4.0"},

        {"0.0", "0.0"}, {"0.0", "0.0"}, {"0.4", "10.0"}, {"0.0", "1.0"},
        {"0.0", "1.0"}, {"0.0", "1.0"}, {"0.4", "10.0"},
    }};

inline constexpr std::array<CameraSmoothDelayFactorDefaults, 15>
    kRetailCameraSmoothViewDataDefaults{{

        {"0.0", "0.0"}, {"0.0", "0.0"}, {"0.0", "0.0"},

        {"0.0", "0.0"}, {"0.0", "0.0"}, {"0.0", "1.0"},

        {"0.0", "0.0"}, {"0.0", "1.0"}, {"0.0", "1.0"},

        {"0.0", "0.0"}, {"0.0", "1.0"}, {"0.0", "1.0"},

        {"0.0", "0.0"}, {"0.0", "1.0"}, {"0.0", "1.0"},
    }};

}
