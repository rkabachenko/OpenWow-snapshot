
#pragma once

#include <cstdint>
#include <mutex>
#include <string>

namespace openwow::game {

class ProfessionSystem {
 public:
    enum class TradeSkillCompletionAction : std::uint8_t {
        kNone,
        kCleared,
        kCompleted,
        kRepeat,
    };

    static ProfessionSystem& Get();

    void OpenTradeSkill(uint32_t skillLine, std::string linked_player = {});
    void CloseTradeSkill();
    [[nodiscard]] bool HasTradeSkillWindow() const;
    [[nodiscard]] bool IsTradeSkillLinked() const;
    [[nodiscard]] std::string GetLinkedTradeSkillPlayer() const;

    [[nodiscard]] uint32_t GetOpenSkillLine() const;

    [[nodiscard]] bool ClearAllTradeSkillSpells();
    [[nodiscard]] std::uint32_t ClearTradeSkillSpell(uint32_t spell_id);
    [[nodiscard]] TradeSkillCompletionAction OnTradeSkillSpellComplete(
        uint32_t spell_id);
    void TransferPlayerCraftToNpc(uint32_t spell_id);
    void QueueTradeSkillCraft(uint32_t spell_id, uint32_t repeat_count);
    [[nodiscard]] bool HasActiveTradeSkillSpell(uint32_t spell_id) const;
    [[nodiscard]] bool StopTradeSkillRepeat();
    [[nodiscard]] uint32_t GetTradeSkillRepeatCount(
        uint32_t selected_spell_id) const;

    void SetTradeSkillSpellStateForTests(uint32_t npc_spell_id,
                                        uint32_t npc_repeat_count,
                                        uint32_t player_spell_id,
                                        uint32_t player_repeat_count);

    [[nodiscard]] uint32_t GetNpcTradeSkillSpell() const;
    [[nodiscard]] uint32_t GetPlayerTradeSkillSpell() const;
    [[nodiscard]] uint32_t GetNpcTradeSkillRepeat() const;
    [[nodiscard]] uint32_t GetPlayerTradeSkillRepeat() const;

    void Reset();

 private:
    ProfessionSystem() = default;

    bool     trade_skill_open_ = false;
    uint32_t open_skill_line_  = 0;
    std::string linked_trade_skill_player_;
    uint32_t npc_trade_skill_spell_      = 0;
    uint32_t player_trade_skill_spell_   = 0;
    uint32_t npc_trade_skill_repeat_     = 0;
    uint32_t player_trade_skill_repeat_  = 0;

    mutable std::mutex mutex_;
};

}
