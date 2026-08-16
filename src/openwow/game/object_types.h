#pragma once

#include <cstdint>

namespace openwow::game {

enum class TypeID : std::uint8_t {
  kObject        = 0,
  kItem          = 1,
  kContainer     = 2,
  kUnit          = 3,
  kPlayer        = 4,
  kGameObject    = 5,
  kDynamicObject = 6,
  kCorpse        = 7,
};

inline constexpr int kNumClientObjectTypes = 8;

enum TypeMask : std::uint16_t {
  kTypeMaskObject        = 0x0001,
  kTypeMaskItem          = 0x0002,
  kTypeMaskContainer     = 0x0006,
  kTypeMaskUnit          = 0x0008,
  kTypeMaskPlayer        = 0x0010,
  kTypeMaskGameObject    = 0x0020,
  kTypeMaskDynamicObject = 0x0040,
  kTypeMaskCorpse        = 0x0080,
};

constexpr std::uint16_t TypeMaskFor(TypeID type) {
  switch (type) {
    case TypeID::kObject:        return kTypeMaskObject;
    case TypeID::kItem:          return kTypeMaskObject | kTypeMaskItem;
    case TypeID::kContainer:     return kTypeMaskObject | kTypeMaskContainer;
    case TypeID::kUnit:          return kTypeMaskObject | kTypeMaskUnit;
    case TypeID::kPlayer:        return kTypeMaskObject | kTypeMaskUnit | kTypeMaskPlayer;
    case TypeID::kGameObject:    return kTypeMaskObject | kTypeMaskGameObject;
    case TypeID::kDynamicObject: return kTypeMaskObject | kTypeMaskDynamicObject;
    case TypeID::kCorpse:        return kTypeMaskObject | kTypeMaskCorpse;
  }
  return kTypeMaskObject;
}

enum class UpdateType : std::uint8_t {
  kValues              = 0,
  kMovement            = 1,
  kCreateObject        = 2,
  kCreateObject2       = 3,
  kOutOfRangeObjects   = 4,
  kNearObjects         = 5,
};

enum UpdateFlag : std::uint16_t {
  kUpdateFlagNone              = 0x0000,
  kUpdateFlagSelf              = 0x0001,
  kUpdateFlagTransport         = 0x0002,
  kUpdateFlagHasTarget         = 0x0004,
  kUpdateFlagUnknown           = 0x0008,
  kUpdateFlagLowGuid           = 0x0010,
  kUpdateFlagLiving            = 0x0020,
  kUpdateFlagStationaryPosition= 0x0040,
  kUpdateFlagVehicle           = 0x0080,
  kUpdateFlagPosition          = 0x0100,
  kUpdateFlagRotation          = 0x0200,
};

enum MovementFlag : std::uint32_t {
  kMoveFlagNone            = 0x00000000,
  kMoveFlagForward         = 0x00000001,
  kMoveFlagBackward        = 0x00000002,
  kMoveFlagStrafeLeft      = 0x00000004,
  kMoveFlagStrafeRight     = 0x00000008,
  kMoveFlagTurnLeft        = 0x00000010,
  kMoveFlagTurnRight       = 0x00000020,
  kMoveFlagPitchUp         = 0x00000040,
  kMoveFlagPitchDown       = 0x00000080,
  kMoveFlagWalking         = 0x00000100,
  kMoveFlagOnTransport     = 0x00000200,
  kMoveFlagDisableGravity  = 0x00000400,
  kMoveFlagRoot            = 0x00000800,
  kMoveFlagFalling         = 0x00001000,
  kMoveFlagFallingFar      = 0x00002000,
  kMoveFlagPendingStop     = 0x00004000,
  kMoveFlagPendingStrafeStop = 0x00008000,
  kMoveFlagPendingForward  = 0x00010000,
  kMoveFlagPendingBackward = 0x00020000,
  kMoveFlagPendingStrafeLeft  = 0x00040000,
  kMoveFlagPendingStrafeRight = 0x00080000,
  kMoveFlagPendingRoot     = 0x00100000,
  kMoveFlagSwimming        = 0x00200000,
  kMoveFlagAscending       = 0x00400000,
  kMoveFlagDescending      = 0x00800000,
  kMoveFlagCanFly          = 0x01000000,
  kMoveFlagFlying          = 0x02000000,
  kMoveFlagSplineElevation = 0x04000000,
  kMoveFlagSplineEnabled   = 0x08000000,
  kMoveFlagWaterwalking    = 0x10000000,
  kMoveFlagFallingSlow     = 0x20000000,
  kMoveFlagHover           = 0x40000000,
};

enum MovementFlag2 : std::uint16_t {
  kMoveFlag2None                  = 0x0000,
  kMoveFlag2NoStrafe              = 0x0001,
  kMoveFlag2NoJumping             = 0x0002,

  kMoveFlag2Unknown4              = 0x0004,
  kMoveFlag2FullSpeedTurning      = 0x0008,
  kMoveFlag2FullSpeedPitching     = 0x0010,
  kMoveFlag2AlwaysAllowPitching   = 0x0020,

  kMoveFlag2SuppressSteepPitchFall = 0x0200,
  kMoveFlag2InterpolatedMovement  = 0x0400,
  kMoveFlag2InterpolatedTurning   = 0x0800,
  kMoveFlag2InterpolatedPitching  = 0x1000,
  kMoveFlag2CanTransitionBetweenSwimAndFly = 0x4000,
};

enum SpeedType : std::uint8_t {
  kSpeedWalk       = 0,
  kSpeedRun        = 1,
  kSpeedRunBack    = 2,
  kSpeedSwim       = 3,
  kSpeedSwimBack   = 4,
  kSpeedFlight     = 5,
  kSpeedFlightBack = 6,
  kSpeedTurnRate   = 7,
  kSpeedPitchRate  = 8,
  kMaxSpeeds       = 9,
};

enum UpdateFieldFlag : std::uint16_t {
  kUFNone        = 0x000,
  kUFPublic      = 0x001,
  kUFPrivate     = 0x002,
  kUFOwner       = 0x004,
  kUFItemOwner   = 0x010,
  kUFSpecialInfo = 0x020,
  kUFPartyMember = 0x040,
  kUFDynamic     = 0x100,
};

}
