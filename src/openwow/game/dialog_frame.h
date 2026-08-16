#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "openwow/game/object_guid.h"

namespace openwow::game {

enum class DialogButtonType : uint8_t {
    Accept   = 0,
    Decline  = 1,
    Cancel   = 2,
    Complete = 3,
    Continue = 4,
};

enum class DialogType : uint8_t {
    Gossip        = 0,
    QuestAccept   = 1,
    QuestProgress = 2,
    QuestComplete = 3,
    Generic       = 4,
};

struct DialogEntry {
    std::string title;
    std::string body;
    std::string progressText;
    std::string completionText;
    std::string acceptText;
    std::string declineText;
    uint32_t money   = 0;
    uint32_t iconId  = 0;
    uint32_t questId = 0;
    DialogType type  = DialogType::Generic;
    bool hasDecline  = true;
};

class DialogFrame {
public:
    DialogFrame() = default;

    void ShowDialog(DialogEntry dialog);
    [[nodiscard]] bool IsShowing() const;
    [[nodiscard]] std::optional<DialogEntry> GetDialog() const;
    [[nodiscard]] DialogType GetDialogType() const;
    [[nodiscard]] static std::string GetDialogTypeName(DialogType type);

    [[nodiscard]] bool HasProgressText() const;
    [[nodiscard]] std::string GetProgressText() const;
    [[nodiscard]] bool HasCompletionText() const;
    [[nodiscard]] std::string GetCompletionText() const;
    void SetNPC(ObjectGuid npcGuid, std::string npcName);
    [[nodiscard]] ObjectGuid GetNPCGuid() const;
    [[nodiscard]] const std::string& GetNPCName() const;

    [[nodiscard]] bool HasMoney() const;
    [[nodiscard]] uint32_t GetMoney() const;

    [[nodiscard]] bool HasDecline() const;

    void Close();

    [[nodiscard]] std::string GetTitle() const;
    [[nodiscard]] std::string GetBody() const;
    [[nodiscard]] uint32_t GetQuestId() const;

    [[nodiscard]] uint32_t GetDialogHistoryCount() const;
    [[nodiscard]] const std::vector<DialogEntry>& GetDialogHistory() const;
    void ClearHistory();    [[nodiscard]] std::vector<DialogButtonType> GetButtonTypes() const;

    void Reset();

private:
    ObjectGuid npcGuid_;
    std::string npcName_;
    std::optional<DialogEntry> dialog_;
    std::vector<DialogEntry> history_;
    bool showing_ = false;
    static constexpr std::size_t kMaxHistory = 20;
};

}
