#include "openwow/game/account_data_runtime_sync.h"

#include "openwow/core/cvar.h"
#include "openwow/game/chat_cache.h"
#include "openwow/game/actions/bindings/adapters/persistence/binding_account_data_adapter.h"
#include "openwow/game/actions/bindings/adapters/persistence/binding_profile_storage.h"
#include "openwow/game/actions/bindings/application/binding_profiles.h"
#include "openwow/game/actions/macros/adapters/persistence/macro_account_data_adapter.h"
#include "openwow/game/actions/macros/application/macro_catalog.h"
#include "openwow/game/vehicle_system.h"
#include "openwow/game/world_session.h"
#include "openwow/network/protocol/wotlk/opcodes.h"
#include "openwow/network/protocol/wotlk/world_packet.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/game_ui_core.h"
#include "openwow/ui/game/runtime/retained_layout.h"
#include "openwow/world/camera/world_camera.h"

#include <cstdio>

namespace openwow::game {
namespace {

void FormatAndSetRuntimeConfigCVar(const char* name, const float value) {
  char buffer[40];
  std::snprintf(buffer, sizeof(buffer), "%f", static_cast<double>(value));
  (void)openwow::ui::game::CVarSystem::Instance().SetCVar(name, buffer, true);
}

void SnapshotSavedCameraConfigCVars(
    const openwow::world::WorldCamera* world_camera) {
  if (world_camera == nullptr) {
    return;
  }

  if (VehicleSystem::Get().IsInVehicle()) {
    FormatAndSetRuntimeConfigCVar("cameraSavedVehicleDistance",
                                  world_camera->target_distance());
  } else {
    FormatAndSetRuntimeConfigCVar("cameraSavedDistance",
                                  world_camera->target_distance());
  }

  constexpr float kRadiansToDegrees = 57.29578f;
  FormatAndSetRuntimeConfigCVar("cameraSavedPitch",
                                world_camera->target_pitch() * kRadiansToDegrees);
}

void SyncRuntimeConfigAccountDataInternal(
    const openwow::world::WorldCamera* world_camera) {
  auto& cvars = openwow::ui::game::CVarSystem::Instance();
  if (cvars.Count() == 0) {
    return;
  }

  auto& account_data = AccountData::Get();
  account_data.SyncLocalData(
      AccountDataType::GlobalConfig,
      cvars.SerializeConfig(openwow::ui::game::CVarSerializationScope::kAccountDataSlot0));

  SnapshotSavedCameraConfigCVars(world_camera);
  account_data.SyncLocalData(
      AccountDataType::PerCharacterConfig,
      cvars.SerializeConfig(openwow::ui::game::CVarSerializationScope::kAccountDataSlot1));
}

void SyncUploadSnapshot(const AccountDataUploadContext& context) {
  auto& account_data = AccountData::Get();

  if (context.include_config) {
    SyncRuntimeConfigAccountDataInternal(context.world_camera);
  }

  if (context.binding_profiles != nullptr) {
    using actions::bindings::adapters::persistence::BindingProfileStorage;
    account_data.SyncLocalData(
        AccountDataType::GlobalBindings,
        BindingProfileStorage::Serialize(
            *context.binding_profiles, BindingProfileScope::kAccount));
    account_data.SyncLocalData(
        AccountDataType::PerCharacterBindings,
        BindingProfileStorage::Serialize(
            *context.binding_profiles, BindingProfileScope::kCharacter));
  }

  if (context.macro_catalog != nullptr &&
      (context.macro_catalog->IsDirty()
       || context.macro_catalog->GetNumAccountMacros() != 0
       || context.macro_catalog->GetNumCharacterMacros() != 0)) {
    using actions::macros::persistence::MacroAccountDataAdapter;
    account_data.SyncLocalData(AccountDataType::GlobalMacros,
        MacroAccountDataAdapter::Encode(
            context.macro_catalog->SnapshotMacros(MacroScope::kAccount)));
    account_data.SyncLocalData(AccountDataType::PerCharacterMacros,
        MacroAccountDataAdapter::Encode(
            context.macro_catalog->SnapshotMacros(MacroScope::kCharacter)));
  }

  if (context.retained_layout != nullptr) {
    account_data.SyncLocalData(AccountDataType::PerCharacterLayout,
                               context.retained_layout->BuildLayoutCache());
  }

  if (context.dbc != nullptr) {
    account_data.SyncLocalData(
        AccountDataType::PerCharacterChat,
        SerializeChatCache(context.dbc, context.zone_id));
  }
}

}

void SyncRuntimeConfigAccountData(
    const openwow::world::WorldCamera* world_camera) {
  SyncRuntimeConfigAccountDataInternal(world_camera);
}

bool DownloadRuntimeAccountData(
    const std::function<bool(const openwow::net::wotlk::WorldPacket&)>&
        send_packet) {
  if (!send_packet) {
    return false;
  }

  auto& account_data = AccountData::Get();
  bool sent_any = false;
  for (std::uint32_t raw_type = 0;
       raw_type < static_cast<std::uint32_t>(AccountDataType::NumTypes);
       ++raw_type) {
    const auto type = static_cast<AccountDataType>(raw_type);
    if (!account_data.ShouldDownload(type)
        || !account_data.MarkServerDownloadPending(type)) {
      continue;
    }

    openwow::net::wotlk::WorldPacket packet(
        openwow::net::wotlk::Opcode::CMSG_REQUEST_ACCOUNT_DATA);
    packet.AppendU32(raw_type);
    if (!send_packet(packet)) {
      account_data.ClearServerDownloadPending(type);
      continue;
    }
    sent_any = true;
  }

  return sent_any;
}

void ApplyAccountDataPayload(WorldSession& session,
                             const AccountDataType type,
                             const std::uint32_t timestamp,
                             const std::string& data,
                             BindingProfiles* bindings,
                             MacroCatalog* macros,
                             openwow::ui::game::runtime::RetainedLayout*
                                 retained_layout) {
  AccountData::Get().SetAccountData(type, timestamp, data);

  ApplyCachedAccountDataPayload(session, type, data, bindings, macros,
                                retained_layout);
}

void ApplyCachedAccountDataPayload(WorldSession& session,
                                   const AccountDataType type,
                                   const std::string& data,
                                   BindingProfiles* bindings,
                                   MacroCatalog* macros,
                                   openwow::ui::game::runtime::RetainedLayout*
                                       retained_layout) {

  switch (type) {
    case AccountDataType::GlobalBindings:
    case AccountDataType::PerCharacterBindings:
      if (bindings != nullptr) {
        actions::bindings::adapters::persistence::BindingAccountDataAdapter::
            ApplyAccountDataSlot(*bindings, type, data);
      }
      break;
    case AccountDataType::GlobalMacros:
    case AccountDataType::PerCharacterMacros:
      if (macros != nullptr) {
        const auto scope = type == AccountDataType::GlobalMacros
                               ? MacroScope::kAccount
                               : MacroScope::kCharacter;
        actions::macros::persistence::MacroAccountDataAdapter::Load(
            *macros, scope, data);
      }
      break;
    case AccountDataType::PerCharacterLayout:
      if (retained_layout != nullptr) {
        retained_layout->ApplyLayoutCache(data);
      }
      break;
    case AccountDataType::PerCharacterChat:
      ApplyChatCachePayload(session, data);
      break;
    case AccountDataType::GlobalConfig:
    case AccountDataType::PerCharacterConfig:

      (void)openwow::core::ida::CVar_ParseConfigBuffer(data);
      break;
    case AccountDataType::NumTypes:
      break;
  }
}

namespace {

bool UploadRuntimeAccountDataInternal(const AccountDataUploadContext& context) {
  if (!context.send_packet) {
    return false;
  }

  SyncUploadSnapshot(context);

  auto& account_data = AccountData::Get();
  bool sent_any = false;
  bool had_failure = false;
  for (std::uint32_t raw_type = 0;
       raw_type < static_cast<std::uint32_t>(AccountDataType::NumTypes);
       ++raw_type) {
    const auto type = static_cast<AccountDataType>(raw_type);
    account_data.ClearServerDownloadPending(type);
    const auto payload = account_data.SnapshotForUpload(type);
    if (!payload.has_value()) {
      continue;
    }

    const std::uint32_t sequence = account_data.AllocateUploadSequence();
    const auto compressed = AccountData::Compress(payload->data);
    if (!payload->data.empty() && compressed.empty()) {
      had_failure = true;
      continue;
    }

    openwow::net::wotlk::WorldPacket packet(
        openwow::net::wotlk::Opcode::CMSG_UPDATE_ACCOUNT_DATA);
    packet.AppendU32(raw_type);
    packet.AppendU32(sequence);
    packet.AppendU32(static_cast<std::uint32_t>(payload->data.size()));
    if (!compressed.empty()) {
      packet.AppendBytes(compressed.data(), compressed.size());
    }

    if (!context.send_packet(packet)) {
      had_failure = true;
      continue;
    }
    account_data.MarkUploaded(type, sequence, *payload);
    sent_any = true;
  }

  if (account_data.HasBoundPersistenceIdentity() &&
      !account_data.FlushBoundPersistence()) {
    had_failure = true;
  }
  account_data.FinishUploadAttempt(had_failure);
  return sent_any;
}

}

bool UploadRuntimeAccountData(const AccountDataUploadContext& context) {
  return UploadRuntimeAccountDataInternal(context);
}

bool PumpRuntimeAccountDataUpload(
    const AccountDataUploadContext& context,
    const AccountData::UploadClock::time_point now) {
  if (!AccountData::Get().IsUploadDue(now)) {
    return false;
  }
  return UploadRuntimeAccountDataInternal(context);
}

}
