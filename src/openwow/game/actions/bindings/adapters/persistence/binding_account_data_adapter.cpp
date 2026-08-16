#include "openwow/game/actions/bindings/adapters/persistence/binding_account_data_adapter.h"

#include "openwow/game/account_data.h"
#include "openwow/game/actions/bindings/adapters/persistence/binding_profile_storage.h"
#include "openwow/game/actions/bindings/application/binding_profiles.h"

#include <optional>

namespace openwow::game::actions::bindings::adapters::persistence {
namespace {

[[nodiscard]] std::optional<BindingProfileScope> ScopeForAccountDataType(
    const AccountDataType type) {
  switch (type) {
    case AccountDataType::GlobalBindings:
      return BindingProfileScope::kAccount;
    case AccountDataType::PerCharacterBindings:
      return BindingProfileScope::kCharacter;
    default:
      return std::nullopt;
  }
}

[[nodiscard]] std::optional<BindingProfileScope> ScopeForCookie(
    const std::uint16_t value) {
  switch (value) {
    case 1:
      return BindingProfileScope::kAccount;
    case 2:
      return BindingProfileScope::kCharacter;
    default:
      return std::nullopt;
  }
}

void ApplyPayload(BindingProfiles& profiles,
                  const BindingProfileScope scope,
                  const std::string_view data) {
  if (!data.empty()) {
    BindingProfileStorage::ApplyText(profiles, data, scope);
  }
}

}

void BindingAccountDataAdapter::LoadCached(AccountData& account_data,
                                           BindingProfiles& profiles) {
  const auto generation = profiles.BeginProfileLoad();
  ApplyPayload(profiles, BindingProfileScope::kAccount,
               account_data.GetBindings(true));
  profiles.CompleteProfileLoad(generation, BindingProfileScope::kAccount);
  ApplyPayload(profiles, BindingProfileScope::kCharacter,
               account_data.GetBindings(false));
  profiles.CompleteProfileLoad(generation, BindingProfileScope::kCharacter);
}

void BindingAccountDataAdapter::Save(AccountData& account_data,
                                     const BindingProfiles& profiles) {
  for (const auto scope : profiles.PersistentProfilesNeedingSave()) {
    account_data.SaveBindings(
        BindingProfileStorage::Serialize(profiles, scope),
        scope == BindingProfileScope::kAccount);
  }
}

void BindingAccountDataAdapter::ApplyAccountDataSlot(
    BindingProfiles& profiles,
    const AccountDataType type,
    const std::string_view data) {
  const auto scope = ScopeForAccountDataType(type);
  if (!scope) {
    return;
  }
  ApplyPayload(profiles, *scope, data);
  profiles.Finalize();
}

BindingProfileLoadGeneration
BindingAccountDataAdapter::BeginAsynchronousLoad(BindingProfiles& profiles) {
  return profiles.BeginProfileLoad();
}

std::uint32_t BindingAccountDataAdapter::MakeLoadCookie(
    const BindingProfileLoadGeneration generation,
    const BindingProfileScope scope) {
  return (static_cast<std::uint32_t>(generation.value()) << 16) |
         static_cast<std::uint32_t>(scope);
}

void BindingAccountDataAdapter::CompleteAsynchronousLoad(
    BindingProfiles& profiles,
    const std::uint32_t cookie,
    const std::string_view data) {
  const BindingProfileLoadGeneration generation(
      static_cast<std::uint16_t>((cookie >> 16) & 0xFFFFu));
  const auto scope =
      ScopeForCookie(static_cast<std::uint16_t>(cookie & 0xFFFFu));
  if (!scope || !profiles.AcceptsProfileLoad(generation)) {
    return;
  }
  ApplyPayload(profiles, *scope, data);
  profiles.CompleteProfileLoad(generation, *scope);
}

}
