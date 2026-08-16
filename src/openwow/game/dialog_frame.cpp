
#include "openwow/game/dialog_frame.h"

#include <algorithm>

namespace openwow::game {

void DialogFrame::ShowDialog(DialogEntry dialog) {

    if (dialog_.has_value() && showing_) {
        history_.push_back(*dialog_);

        while (history_.size() > kMaxHistory) {
            history_.erase(history_.begin());
        }
    }

    dialog_ = std::move(dialog);
    showing_ = true;
}

bool DialogFrame::IsShowing() const { return showing_; }

std::optional<DialogEntry> DialogFrame::GetDialog() const { return dialog_; }

DialogType DialogFrame::GetDialogType() const {
    if (!dialog_.has_value()) return DialogType::Generic;
    return dialog_->type;
}

std::string DialogFrame::GetDialogTypeName(DialogType type) {
    switch (type) {
        case DialogType::Gossip:        return "Gossip";
        case DialogType::QuestAccept:   return "Quest Accept";
        case DialogType::QuestProgress: return "Quest Progress";
        case DialogType::QuestComplete: return "Quest Complete";
        case DialogType::Generic:       return "Generic";
    }
    return "Unknown";
}

bool DialogFrame::HasProgressText() const {
    return dialog_.has_value() && !dialog_->progressText.empty();
}

std::string DialogFrame::GetProgressText() const {
    if (!dialog_.has_value()) return "";
    return dialog_->progressText;
}

bool DialogFrame::HasCompletionText() const {
    return dialog_.has_value() && !dialog_->completionText.empty();
}

std::string DialogFrame::GetCompletionText() const {
    if (!dialog_.has_value()) return "";
    return dialog_->completionText;
}

void DialogFrame::SetNPC(ObjectGuid npcGuid, std::string npcName) {
    npcGuid_ = npcGuid;
    npcName_ = std::move(npcName);
}

ObjectGuid DialogFrame::GetNPCGuid() const { return npcGuid_; }
const std::string& DialogFrame::GetNPCName() const { return npcName_; }

bool DialogFrame::HasMoney() const {
    return dialog_.has_value() && dialog_->money > 0;
}

uint32_t DialogFrame::GetMoney() const {
    return dialog_.has_value() ? dialog_->money : 0;
}

bool DialogFrame::HasDecline() const {
    return dialog_.has_value() && dialog_->hasDecline;
}

void DialogFrame::Close() {
    showing_ = false;
}

std::vector<DialogButtonType> DialogFrame::GetButtonTypes() const {
    std::vector<DialogButtonType> buttons;
    if (!dialog_.has_value()) return buttons;

    switch (dialog_->type) {
        case DialogType::QuestAccept:
            buttons.push_back(DialogButtonType::Accept);
            if (dialog_->hasDecline) {
                buttons.push_back(DialogButtonType::Decline);
            }
            break;

        case DialogType::QuestProgress:
            buttons.push_back(DialogButtonType::Continue);
            buttons.push_back(DialogButtonType::Cancel);
            break;

        case DialogType::QuestComplete:
            buttons.push_back(DialogButtonType::Complete);
            buttons.push_back(DialogButtonType::Cancel);
            break;

        case DialogType::Gossip:
            buttons.push_back(DialogButtonType::Cancel);
            break;

        case DialogType::Generic:
        default:
            buttons.push_back(DialogButtonType::Accept);
            if (dialog_->hasDecline) {
                buttons.push_back(DialogButtonType::Decline);
            }
            buttons.push_back(DialogButtonType::Cancel);
            break;
    }
    return buttons;
}

std::string DialogFrame::GetTitle() const {
    if (!dialog_.has_value()) return "";
    return dialog_->title;
}

std::string DialogFrame::GetBody() const {
    if (!dialog_.has_value()) return "";
    return dialog_->body;
}

uint32_t DialogFrame::GetQuestId() const {
    if (!dialog_.has_value()) return 0;
    return dialog_->questId;
}

uint32_t DialogFrame::GetDialogHistoryCount() const {
    return static_cast<uint32_t>(history_.size());
}

const std::vector<DialogEntry>& DialogFrame::GetDialogHistory() const {
    return history_;
}

void DialogFrame::ClearHistory() {
    history_.clear();
}

void DialogFrame::Reset() {
    npcGuid_ = ObjectGuid{};
    npcName_.clear();
    dialog_.reset();
    history_.clear();
    showing_ = false;
}

}
