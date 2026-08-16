#include "openwow/ui/surfaces/game/adapters/protocol/world_ui_session_command_adapter.h"

#include "openwow/game/world_session.h"
#include "openwow/net/wotlk/protocol/packet_sender.h"

namespace openwow::ui::game {

std::optional<openwow::game::ObjectGuid>
WorldUiSessionCommandAdapter::CurrentWorldUiSelection() const {
  if (session_ == nullptr) {
    return std::nullopt;
  }
  const auto selection = session_->objects().GetTargetGuid();
  if (selection.IsEmpty()) {
    return std::nullopt;
  }
  return selection;
}

void WorldUiSessionCommandAdapter::SetWorldUiSelection(
    const openwow::game::ObjectGuid selection) {
  if (session_ != nullptr) {
    session_->Send(openwow::net::wotlk::PacketSender::BuildSetSelection(
        selection.GetRawValue()));
  }
}

void WorldUiSessionCommandAdapter::EnableWorldUiVoice(
    const WorldUiVoiceSettings settings) {
  if (session_ != nullptr) {
    session_->Send(openwow::net::wotlk::PacketSender::BuildVoiceChatEnable(
        settings.voice_enabled, settings.microphone_enabled));
  }
}

}
