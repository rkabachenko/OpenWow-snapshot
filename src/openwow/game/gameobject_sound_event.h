#pragma once
namespace openwow::audio { class SoundRuntime; }

#include <cstdint>

namespace openwow::data::dbc {
class DbcLoader;
struct GameObjectDisplayInfoEntry;
}

namespace openwow::world {
class WorldCamera;
}

namespace openwow::game {

class CGGameObject_C;

namespace go_sound_event {

inline constexpr std::uint32_t kOpen0 = 0x304F4724;
inline constexpr std::uint32_t kOpen1 = 0x314F4724;
inline constexpr std::uint32_t kOpen2 = 0x324F4724;
inline constexpr std::uint32_t kOpen3 = 0x334F4724;

inline constexpr std::uint32_t kOpen4 = 0x344F4724;
inline constexpr std::uint32_t kOpen5 = 0x354F4724;

inline constexpr std::uint32_t kCustom0 = 0x30434724;
inline constexpr std::uint32_t kCustom1 = 0x31434724;
inline constexpr std::uint32_t kCustom2 = 0x32434724;
inline constexpr std::uint32_t kCustom3 = 0x33434724;

inline constexpr std::uint32_t kSound = 0x444E5324;
inline constexpr std::uint32_t kDirectSoundObject = 0x4F534424;

inline constexpr std::uint32_t kShake = 0x4B485324;

inline constexpr std::uint32_t kLoopSound = 0x4C534424;

}

bool PlayGameObjectDisplayInfoSound(
    openwow::audio::SoundRuntime& sound_runtime,
    const data::dbc::DbcLoader& dbc, std::uint32_t display_id, int slot_index,
    const float* position,
    std::uint32_t* sound_handle_id);

bool DispatchGameObjectSoundEvent(
    openwow::audio::SoundRuntime& sound_runtime,
    const data::dbc::DbcLoader& dbc, openwow::world::WorldCamera* camera,
    std::uint32_t display_id, std::uint32_t event_fourcc,
    std::uint32_t data, const float* position,
    std::uint32_t* sound_handle_id);

bool HandleGameObjectSoundEvent(
    const CGGameObject_C& game_object,
    openwow::world::WorldCamera* camera, std::uint32_t event_fourcc,
    std::uint32_t data, const float* position,
    std::uint32_t* sound_handle_id);

bool HandleDestructibleBuildingSoundEvent(
    const CGGameObject_C& game_object,
    openwow::world::WorldCamera* camera, std::uint32_t event_fourcc,
    std::uint32_t data, const float* position,
    std::uint32_t* sound_handle_id);

float GetDungeonDifficultyModelOpacity(const CGGameObject_C& game_object);

}
