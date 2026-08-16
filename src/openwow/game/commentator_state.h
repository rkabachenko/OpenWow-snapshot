#pragma once

#include "openwow/game/object_guid.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/packet_reader.h"
#include "openwow/world/camera/world_camera.h"
#include "openwow/world/collision/collision.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace openwow::game {

struct CommentatorInstanceKeyTail {
    std::uint32_t key_u32{0};
    std::uint8_t key_u8{0};
    std::uint16_t key_u16{0};

    [[nodiscard]] bool operator==(const CommentatorInstanceKeyTail&) const = default;
};

struct CommentatorInstanceKey {
    std::uint32_t map_id{0};
    CommentatorInstanceKeyTail tail{};

    [[nodiscard]] bool operator==(const CommentatorInstanceKey&) const = default;
};

struct CommentatorPlayerInfo {
    ObjectGuid guid{};
    std::uint32_t field_08{0};
    std::uint32_t field_12{0};
    std::uint32_t field_16{0};
    std::uint32_t field_20{0};
    std::uint32_t team_index{0};
    std::uint32_t field_28{0};
    std::uint8_t field_32{0};
    std::uint16_t field_36{0};
    std::uint32_t field_40{0};
    std::uint32_t field_44{0};
    std::uint32_t field_48{0};
    std::uint32_t field_52{0};
    std::uint32_t field_56{0};
    std::uint8_t field_60{0};
    std::string name{};
    std::uint8_t field_112{0};
    std::uint8_t field_113{0};
    std::uint8_t field_114{0};
};

struct CommentatorRosterStorage {
    static constexpr std::size_t kPlayersPerTeam = 5;

    std::array<std::uint32_t, 2> counts{};
    std::vector<CommentatorPlayerInfo> slots{};

    void ResetVisibleCounts() { counts.fill(0); }

    void Store(CommentatorPlayerInfo player) {
        if (player.team_index >= counts.size()) {
            return;
        }

        const auto global_slot =
            static_cast<std::size_t>(player.team_index) * kPlayersPerTeam +
            counts[player.team_index]++;
        if (global_slot >= slots.size()) {
            slots.resize(global_slot + 1);
        }
        slots[global_slot] = std::move(player);
    }

    [[nodiscard]] const CommentatorPlayerInfo* Get(
        const std::size_t team_index,
        const std::size_t player_index) const {
        if (team_index >= counts.size() || player_index >= counts[team_index]) {
            return nullptr;
        }

        const auto global_slot = team_index * kPlayersPerTeam + player_index;
        return global_slot < slots.size() ? &slots[global_slot] : nullptr;
    }
};

struct CommentatorInstanceInfo {
    CommentatorInstanceKey key{};
    ObjectGuid guid{};
    std::uint32_t extra_u32{0};
    CommentatorRosterStorage roster{};
};

struct CommentatorMapInfo {
    std::uint32_t field0{0};
    std::uint32_t field1{0};
    std::uint32_t field2{0};
    std::vector<CommentatorInstanceInfo> instances;
};

struct CommentatorSkirmishQueueEntry {
    ObjectGuid first_guid{};
    ObjectGuid second_guid{};
    bool rated{false};
};

enum class CommentatorPlayerInfoPacketResult {
    ParseError,
    Ignored,
    Updated,
};

class CommentatorState {
 public:
    static constexpr float kMinFieldOfViewRadians =
        openwow::world::kCameraMinFieldOfViewRadians;
    static constexpr float kMaxFieldOfViewRadians =
        openwow::world::kCameraMaxFieldOfViewRadians;
    static constexpr float kZoomStepBaselineRadians = 0.34906584f;
    static constexpr float kDefaultFieldOfViewRadians = 1.5707964f;
    static constexpr float kDefaultCameraDistance = 12.0f;
    static constexpr bool kDefaultCameraCollision = true;
    static constexpr float kDefaultMoveSpeed = 10.0f;
    static constexpr float kDefaultTargetHeightOffset = 0.0f;
    static constexpr std::uint32_t kDefaultSkirmishMode = 1u;

    static CommentatorState& Get() {
        static CommentatorState instance;
        return instance;
    }

    struct Vec3 { float x, y, z; };
    struct ManualCameraOverride {
        Vec3 position;
        Vec3 forward;
        float fov;
    };

    void SetMoveSpeed(float speed) {
        if (!(speed < 40.0f)) {
            moveSpeed_ = 40.0f;
        } else if (speed <= 0.0f) {
            moveSpeed_ = 0.0f;
        } else {
            moveSpeed_ = speed;
        }
    }

    [[nodiscard]] float GetMoveSpeed() const { return moveSpeed_; }

    void SetMovementRates(const float forward, const float strafe,
                          const float vertical, const float turn_rate) {
        forward_rate_ = forward;
        strafe_rate_ = strafe;
        vertical_rate_ = vertical;
        turn_rate_ = turn_rate;
    }

    void SetCamera(float x, float y, float z, float yaw, float pitch) {
        cameraPos_ = {x, y, z};
        yaw_ = yaw;
        pitch_ = pitch;
        manual_camera_override_ = true;
    }

    void ClearTrackedCameraGuids() {
        look_at_guid_ = 0;
        follow_guid_ = 0;
    }

    void SetLookAtGuid(const std::uint64_t guid) {
        look_at_guid_ = guid;
        follow_guid_ = 0;
        manual_camera_override_ = true;
    }

    void SetFollowGuid(const std::uint64_t guid) {
        follow_guid_ = guid;
        look_at_guid_ = 0;
        manual_camera_override_ = true;
    }

    [[nodiscard]] std::uint64_t GetLookAtGuid() const { return look_at_guid_; }
    [[nodiscard]] std::uint64_t GetFollowGuid() const { return follow_guid_; }
    [[nodiscard]] float GetCameraDistance() const { return camera_distance_; }
    [[nodiscard]] bool IsCameraCollisionEnabled() const { return camera_collision_enabled_; }
    void SetCameraCollisionEnabled(const bool enabled) { camera_collision_enabled_ = enabled; }
    void BindCollision(const openwow::world::CollisionManager* collision) {
        collision_ = collision;
    }
    [[nodiscard]] float GetTargetHeightOffset() const { return target_height_offset_; }
    void SetTargetHeightOffset(const float offset) { target_height_offset_ = offset; }
    [[nodiscard]] std::uint64_t GetBattlemasterGuid() const {
        return battlemaster_guid_;
    }
    void SetBattlemasterGuid(const std::uint64_t guid) {
        battlemaster_guid_ = guid;
    }

    void UpdateCamera(const float delta_seconds, const ObjectManager* objects) {
        AdvanceFieldOfView(delta_seconds);
        if (!manual_camera_override_) {
            return;
        }

        if (const auto* follow_target = ResolveTrackedObject(objects, follow_guid_)) {
            const auto focus = BuildTrackedFocusPoint(*follow_target);
            const float cos_pitch = std::cos(pitch_);
            const float sin_pitch = std::sin(pitch_);
            const float cos_yaw = std::cos(yaw_);
            const float sin_yaw = std::sin(yaw_);
            Vec3 next_camera_pos = {
                focus.x - cos_pitch * cos_yaw * camera_distance_,
                focus.y - cos_pitch * sin_yaw * camera_distance_,
                focus.z + sin_pitch * camera_distance_,
            };
            if (camera_collision_enabled_ && collision_ != nullptr) {
                constexpr float kCollisionInset = 0.80000001f;

                const std::array<float, 3> trace_start{focus.x, focus.y, focus.z};
                const std::array<float, 3> trace_end{
                    next_camera_pos.x,
                    next_camera_pos.y,
                    next_camera_pos.z,
                };
                const float dx = trace_end[0] - trace_start[0];
                const float dy = trace_end[1] - trace_start[1];
                const float dz = trace_end[2] - trace_start[2];
                const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (const auto terrain_hit =
                        distance > 1.0e-6f
                            ? collision_->Raycast(
                                  trace_start[0], trace_start[1], trace_start[2],
                                  dx / distance, dy / distance, dz / distance,
                                  distance)
                            : std::nullopt;
                    terrain_hit.has_value()) {
                    const std::array<float, 3> offset{
                        trace_start[0] - terrain_hit->x,
                        trace_start[1] - terrain_hit->y,
                        trace_start[2] - terrain_hit->z,
                    };
                    const float offset_length = std::sqrt(
                        offset[0] * offset[0] +
                        offset[1] * offset[1] +
                        offset[2] * offset[2]);
                    if (offset_length > 1.0e-6f) {
                        const float offset_scale = kCollisionInset / offset_length;
                        next_camera_pos = {
                            terrain_hit->x + offset[0] * offset_scale,
                            terrain_hit->y + offset[1] * offset_scale,
                            terrain_hit->z + offset[2] * offset_scale,
                        };
                    } else {
                        next_camera_pos = focus;
                    }
                }
            }
            cameraPos_ = next_camera_pos;
            return;
        }

        if (const auto* look_at_target = ResolveTrackedObject(objects, look_at_guid_)) {
            const auto focus = BuildTrackedFocusPoint(*look_at_target);
            const float dx = focus.x - cameraPos_.x;
            const float dy = focus.y - cameraPos_.y;
            const float dz = focus.z - cameraPos_.z;
            const float distance_squared = dx * dx + dy * dy + dz * dz;
            if (distance_squared >= 0.0001f) {
                const float inverse_distance = 1.0f / std::sqrt(distance_squared);
                const float fx = dx * inverse_distance;
                const float fy = dy * inverse_distance;
                const float fz = dz * inverse_distance;
                yaw_ = std::atan2(fy, fx);
                pitch_ = -std::asin(std::clamp(fz, -1.0f, 1.0f));
            }
            return;
        }

        const float cos_pitch = std::cos(pitch_);
        const float sin_pitch = std::sin(pitch_);
        const float cos_yaw = std::cos(yaw_);
        const float sin_yaw = std::sin(yaw_);
        const Vec3 forward{cos_pitch * cos_yaw,
                           cos_pitch * sin_yaw,
                           -sin_pitch};
        const Vec3 left{-sin_yaw, cos_yaw, 0.0f};
        const Vec3 up{sin_pitch * cos_yaw,
                      sin_pitch * sin_yaw,
                      cos_pitch};
        const float distance = moveSpeed_ * delta_seconds;
        cameraPos_.x +=
            (forward_rate_ * forward.x + strafe_rate_ * left.x +
             vertical_rate_ * up.x) * distance;
        cameraPos_.y +=
            (forward_rate_ * forward.y + strafe_rate_ * left.y +
             vertical_rate_ * up.y) * distance;
        cameraPos_.z +=
            (forward_rate_ * forward.z + strafe_rate_ * left.z +
             vertical_rate_ * up.z) * distance;

        if (std::fabs(turn_rate_) >= 0.001f) {
            yaw_ += turn_rate_;
        }
    }

    [[nodiscard]] Vec3 GetCameraPosition() const { return cameraPos_; }
    [[nodiscard]] float GetYaw() const { return yaw_; }
    [[nodiscard]] float GetPitch() const { return pitch_; }
    [[nodiscard]] bool HasManualCameraOverride() const { return manual_camera_override_; }
    [[nodiscard]] std::optional<ManualCameraOverride> GetManualCameraOverride() const {
        if (!manual_camera_override_) {
            return std::nullopt;
        }

        const float cos_pitch = std::cos(pitch_);
        return ManualCameraOverride{
            cameraPos_,
            {cos_pitch * std::cos(yaw_), cos_pitch * std::sin(yaw_), -std::sin(pitch_)},
            current_fov_,
        };
    }

    void SetFieldOfView(float fov) {
        current_fov_ = ClampFieldOfView(fov);
        target_fov_ = current_fov_;
    }

    void SetTargetFieldOfView(float fov) {
        target_fov_ = ClampFieldOfView(fov);
    }

    [[nodiscard]] float GetFieldOfView() const { return current_fov_; }
    [[nodiscard]] float GetTargetFieldOfView() const { return target_fov_; }

    void ZoomInFieldOfView() {
        float base_fov = current_fov_;
        if (base_fov <= kZoomStepBaselineRadians) {
            base_fov = kZoomStepBaselineRadians;
        }

        float next_target = target_fov_ - base_fov * 0.1f;
        const float current_band_max = current_fov_ * 1.1f;
        if (current_band_max <= next_target) {
            next_target = current_band_max;
        }

        target_fov_ = ClampFieldOfView(next_target);
    }

    void ZoomOutFieldOfView() {
        float base_fov = current_fov_;
        if (base_fov <= kZoomStepBaselineRadians) {
            base_fov = kZoomStepBaselineRadians;
        }

        float next_target = target_fov_ + (base_fov + base_fov * 0.1f) * 0.1f;
        const float current_band_min = current_fov_ * 0.9f;
        if (current_band_min >= next_target) {
            next_target = current_band_min;
        }

        target_fov_ = ClampFieldOfView(next_target);
    }

    void AdvanceFieldOfView(float delta_seconds) {
        current_fov_ = openwow::world::AdvanceCameraFieldOfView(
            current_fov_, target_fov_, delta_seconds);
    }

    [[nodiscard]] CommentatorPlayerInfoPacketResult HandlePlayerInfoPacket(
        const std::uint8_t* data,
        const std::size_t size) {
        PacketReader reader(data, size);

        CommentatorInstanceKey key;
        std::uint64_t instance_guid = 0;
        if (!reader.ReadU32(key.map_id) ||
            !reader.ReadU32(key.tail.key_u32) ||
            !reader.ReadU16(key.tail.key_u16) ||
            !reader.ReadU8(key.tail.key_u8) ||
            !reader.ReadU64(instance_guid)) {
            return CommentatorPlayerInfoPacketResult::ParseError;
        }

        auto* instance = FindMutableInstance(key, ObjectGuid(instance_guid));
        if (!instance) {
            return CommentatorPlayerInfoPacketResult::Ignored;
        }

        std::uint32_t player_count = 0;
        if (!reader.ReadU32(player_count)) {
            return CommentatorPlayerInfoPacketResult::ParseError;
        }

        CommentatorRosterStorage parsed_roster;
        parsed_roster.slots.reserve(
            player_count + CommentatorRosterStorage::kPlayersPerTeam);

        for (std::uint32_t player_index = 0; player_index < player_count; ++player_index) {
            CommentatorPlayerInfo player;
            std::uint8_t raw_name[48]{};
            std::uint64_t player_guid = 0;

            if (!reader.ReadU64(player_guid) ||
                !reader.ReadBytes(raw_name, sizeof(raw_name)) ||
                !reader.ReadU8(player.field_112) ||
                !reader.ReadU8(player.field_113) ||
                !reader.ReadU8(player.field_114) ||
                !reader.ReadU32(player.field_08) ||
                !reader.ReadU32(player.field_12) ||
                !reader.ReadU32(player.field_16) ||
                !reader.ReadU32(player.field_20) ||
                !reader.ReadU32(player.team_index) ||
                !reader.ReadU32(player.field_28)) {
                return CommentatorPlayerInfoPacketResult::ParseError;
            }

            std::uint16_t raw_field_36 = 0;
            if (!reader.ReadU16(raw_field_36) ||
                !reader.ReadU8(player.field_32) ||
                !reader.ReadU32(player.field_40) ||
                !reader.ReadU32(player.field_44) ||
                !reader.ReadU32(player.field_48) ||
                !reader.ReadU32(player.field_52) ||
                !reader.ReadU32(player.field_56) ||
                !reader.ReadU8(player.field_60)) {
                return CommentatorPlayerInfoPacketResult::ParseError;
            }

            player.guid = ObjectGuid(player_guid);
            player.field_36 = raw_field_36;

            const auto* terminator = static_cast<const std::uint8_t*>(
                std::memchr(raw_name, 0, sizeof(raw_name)));
            const auto name_length = terminator
                ? static_cast<std::size_t>(terminator - raw_name)
                : sizeof(raw_name);
            player.name.assign(
                reinterpret_cast<const char*>(raw_name),
                name_length);

            parsed_roster.Store(std::move(player));
        }

        instance->roster = std::move(parsed_roster);
        return CommentatorPlayerInfoPacketResult::Updated;
    }

    [[nodiscard]] std::size_t GetTeamPlayerCount(const std::size_t team_index) const {
        const auto* instance = GetSelectedInstance();
        if (!instance || team_index >= instance->roster.counts.size()) {
            return 0;
        }
        return instance->roster.counts[team_index];
    }

    [[nodiscard]] const CommentatorPlayerInfo* GetPlayerInfo(
        const std::size_t team_index,
        const std::size_t player_index) const {
        const auto* instance = GetSelectedInstance();
        if (!instance) {
            return nullptr;
        }
        return instance->roster.Get(team_index, player_index);
    }

    [[nodiscard]] ObjectGuid GetSelectedPlayerGuidByTokenIndex(
        const std::size_t token_index) const {
        const auto team_index =
            token_index >= CommentatorRosterStorage::kPlayersPerTeam ? std::size_t{1}
                                                                     : std::size_t{0};
        const auto player_index =
            team_index == 0 ? token_index
                            : token_index - CommentatorRosterStorage::kPlayersPerTeam;
        if (const auto* player = GetPlayerInfo(team_index, player_index)) {
            return player->guid;
        }
        return {};
    }

    [[nodiscard]] bool SetSelectedPlayer(const std::size_t team_index,
                                         const std::size_t player_index) {
        if (!GetPlayerInfo(team_index, player_index)) {
            return false;
        }

        selected_player_team_index_ = team_index;
        selected_player_index_ = player_index;
        return true;
    }

    [[nodiscard]] std::size_t GetSelectedPlayerTeamIndex() const {
        return selected_player_team_index_;
    }

    [[nodiscard]] std::size_t GetSelectedPlayerIndex() const {
        return selected_player_index_;
    }

    [[nodiscard]] std::size_t GetMapCount() const { return maps_.size(); }

    [[nodiscard]] const CommentatorMapInfo* GetMapInfo(const std::size_t index) const {
        return index < maps_.size() ? &maps_[index] : nullptr;
    }

    [[nodiscard]] const CommentatorInstanceInfo* GetSelectedInstance() const {
        if (!selected_instance_key_) {
            return nullptr;
        }
        return FindInstance(*selected_instance_key_);
    }

    [[nodiscard]] std::optional<std::uint32_t> GetCurrentMapId() const {
        if (const auto* instance = GetSelectedInstance()) {
            return instance->key.map_id;
        }
        return std::nullopt;
    }

    [[nodiscard]] bool SelectMapAndInstance(
        const std::size_t map_index,
        const std::size_t instance_index) {
        const auto* map = GetMapInfo(map_index);
        if (!map || instance_index >= map->instances.size()) {
            return false;
        }

        selected_instance_key_ = map->instances[instance_index].key;
        return true;
    }

    [[nodiscard]] bool HandleMapInfoPacket(const std::uint8_t* data,
                                           const std::size_t size) {
        PacketReader reader(data, size);

        std::uint32_t map_count = 0;
        std::uint64_t active_instance_guid = 0;
        if (!reader.ReadU32(map_count) || !reader.ReadU64(active_instance_guid)) {
            return false;
        }

        struct ParsedInstanceInfo {
            CommentatorInstanceKey key{};
            std::uint64_t guid{0};
            std::uint32_t extra_u32{0};
        };

        struct ParsedMapInfo {
            std::uint32_t field0{0};
            std::uint32_t field1{0};
            std::uint32_t field2{0};
            std::vector<ParsedInstanceInfo> instances{};
        };

        std::vector<ParsedMapInfo> parsed_maps;
        parsed_maps.reserve(map_count);
        std::optional<CommentatorInstanceKey> matched_instance_key;

        for (std::uint32_t map_index = 0; map_index < map_count; ++map_index) {
            ParsedMapInfo parsed_map;
            std::uint32_t instance_count = 0;
            if (!reader.ReadU32(parsed_map.field0) ||
                !reader.ReadU32(parsed_map.field1) ||
                !reader.ReadU32(parsed_map.field2) ||
                !reader.ReadU32(instance_count)) {
                return false;
            }

            parsed_map.instances.reserve(instance_count);
            for (std::uint32_t instance_index = 0; instance_index < instance_count;
                 ++instance_index) {
                ParsedInstanceInfo parsed_instance;

                if (!reader.ReadU32(parsed_instance.key.map_id) ||
                    !reader.ReadU32(parsed_instance.key.tail.key_u32) ||
                    !reader.ReadU16(parsed_instance.key.tail.key_u16) ||
                    !reader.ReadU8(parsed_instance.key.tail.key_u8) ||
                    !reader.ReadU64(parsed_instance.guid) ||
                    !reader.ReadU32(parsed_instance.extra_u32)) {
                    return false;
                }

                if (active_instance_guid != 0 &&
                    parsed_instance.guid == active_instance_guid) {
                    matched_instance_key = parsed_instance.key;
                }

                parsed_map.instances.push_back(parsed_instance);
            }

            parsed_maps.push_back(std::move(parsed_map));
        }

        maps_.resize(parsed_maps.size());
        for (std::size_t map_index = 0; map_index < parsed_maps.size(); ++map_index) {
            const auto& parsed_map = parsed_maps[map_index];
            auto& map = maps_[map_index];
            map.field0 = parsed_map.field0;
            map.field1 = parsed_map.field1;
            map.field2 = parsed_map.field2;
            map.instances.resize(parsed_map.instances.size());

            for (std::size_t instance_index = 0;
                 instance_index < parsed_map.instances.size();
                 ++instance_index) {
                const auto& parsed_instance = parsed_map.instances[instance_index];
                auto& instance = map.instances[instance_index];
                instance.key = parsed_instance.key;
                instance.guid = ObjectGuid(parsed_instance.guid);
                instance.extra_u32 = parsed_instance.extra_u32;
                instance.roster.ResetVisibleCounts();
            }
        }

        if (matched_instance_key) {
            selected_instance_key_ = *matched_instance_key;
        }

        return true;
    }

    [[nodiscard]] bool HandleSkirmishQueueResult(const std::uint8_t* data,
                                                 const std::size_t size) {
        PacketReader reader(data, size);

        std::uint32_t mode = 0;
        if (!reader.ReadU32(mode)) {
            return false;
        }

        skirmish_mode_ = mode;
        skirmish_queue_.clear();

        std::uint32_t entry_count = 0;
        if (!reader.ReadU32(entry_count)) {
            return false;
        }

        skirmish_queue_.reserve(entry_count);
        for (std::uint32_t index = 0; index < entry_count; ++index) {
            std::uint64_t first_guid = 0;
            std::uint64_t second_guid = 0;
            std::uint8_t rated = 0;
            if (!reader.ReadU64(first_guid) ||
                !reader.ReadU64(second_guid) ||
                !reader.ReadU8(rated)) {
                return false;
            }

            skirmish_queue_.push_back(CommentatorSkirmishQueueEntry{
                ObjectGuid(first_guid),
                ObjectGuid(second_guid),
                rated != 0,
            });
        }

        return true;
    }

    [[nodiscard]] bool HandleSkirmishModePacket(const std::uint8_t* data,
                                                const std::size_t size) {
        PacketReader reader(data, size);
        return reader.ReadU32(skirmish_mode_);
    }

    [[nodiscard]] std::uint32_t GetSkirmishMode() const { return skirmish_mode_; }

    [[nodiscard]] std::size_t GetSkirmishQueueCount() const {
        return skirmish_queue_.size();
    }

    [[nodiscard]] const CommentatorSkirmishQueueEntry* GetSkirmishQueueEntry(
        const std::size_t index) const {
        return index < skirmish_queue_.size() ? &skirmish_queue_[index] : nullptr;
    }

    static constexpr std::array<uint16_t, 5> kOpcodes = {
        955, 952, 950, 1308, 1309
    };

    void SetActive(bool active) {
        active_ = active;
        if (!active) {
            manual_camera_override_ = false;
        }
    }
    [[nodiscard]] bool IsActive() const { return active_; }

    void Reset() {
        moveSpeed_ = kDefaultMoveSpeed;
        cameraPos_ = {0, 0, 0};
        yaw_ = 0.0f;
        pitch_ = 0.0f;
        look_at_guid_ = 0;
        follow_guid_ = 0;
        camera_distance_ = kDefaultCameraDistance;
        current_fov_ = kDefaultFieldOfViewRadians;

        target_fov_ = current_fov_;
        camera_collision_enabled_ = kDefaultCameraCollision;
        target_height_offset_ = kDefaultTargetHeightOffset;
        manual_camera_override_ = false;
        active_ = false;
        forward_rate_ = 0.0f;
        strafe_rate_ = 0.0f;
        vertical_rate_ = 0.0f;
        turn_rate_ = 0.0f;
        maps_.clear();
        battlemaster_guid_ = 0;
        selected_instance_key_.reset();
        selected_player_team_index_ = 0;
        selected_player_index_ = 0;
        skirmish_mode_ = kDefaultSkirmishMode;
        skirmish_queue_.clear();
    }

 private:
    CommentatorState() { Reset(); }

    float forward_rate_{0.0f};
    float strafe_rate_{0.0f};
    float vertical_rate_{0.0f};
    float turn_rate_{0.0f};

    [[nodiscard]] const CommentatorInstanceInfo* FindInstance(
        const CommentatorInstanceKey& key) const {
        for (const auto& map : maps_) {
            for (const auto& instance : map.instances) {
                if (instance.key == key) {
                    return &instance;
                }
            }
        }
        return nullptr;
    }

    [[nodiscard]] CommentatorInstanceInfo* FindMutableInstance(
        const CommentatorInstanceKey& key,
        const ObjectGuid guid) {
        for (auto& map : maps_) {
            for (auto& instance : map.instances) {
                if (instance.key == key && instance.guid == guid) {
                    return &instance;
                }
            }
        }
        return nullptr;
    }

    static float ClampFieldOfView(float fov) {
        return openwow::world::ClampCameraFieldOfView(fov);
    }

    [[nodiscard]] static const WorldObject* ResolveTrackedObject(
        const ObjectManager* objects,
        const std::uint64_t guid) {
        if (objects == nullptr || guid == 0) {
            return nullptr;
        }
        return objects->Get(ObjectGuid(guid));
    }

    [[nodiscard]] Vec3 BuildTrackedFocusPoint(const WorldObject& object) const {
        float focus_height = 1.5f;
        if (object.HasObjectBoundingBox()) {
            float bounds[6]{};
            object.GetObjectBoundingBox(bounds);
            const float bounds_height = bounds[5] - bounds[2];
            if (bounds_height > 0.0f) {
                focus_height = bounds_height * 0.75f;
            }
        }

        const auto focus_position = object.GetPosition();
        return {
            focus_position.x,
            focus_position.y,
            focus_position.z + target_height_offset_ + focus_height,
        };
    }

    float moveSpeed_{kDefaultMoveSpeed};
    Vec3 cameraPos_{0, 0, 0};
    float yaw_{0.0f};
    float pitch_{0.0f};
    std::uint64_t look_at_guid_{0};
    std::uint64_t follow_guid_{0};
    float camera_distance_{kDefaultCameraDistance};
    float current_fov_{kDefaultFieldOfViewRadians};

    float target_fov_{kDefaultFieldOfViewRadians};

    bool camera_collision_enabled_{kDefaultCameraCollision};
    const openwow::world::CollisionManager* collision_{nullptr};
    float target_height_offset_{kDefaultTargetHeightOffset};
    bool manual_camera_override_{false};
    bool active_{false};
    std::vector<CommentatorMapInfo> maps_{};
    std::uint64_t battlemaster_guid_{0};
    std::optional<CommentatorInstanceKey> selected_instance_key_{};
    std::size_t selected_player_team_index_{0};
    std::size_t selected_player_index_{0};
    std::uint32_t skirmish_mode_{kDefaultSkirmishMode};
    std::vector<CommentatorSkirmishQueueEntry> skirmish_queue_{};
};

}
