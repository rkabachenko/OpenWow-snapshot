
#include "openwow/game/profession_system.h"

#include <utility>

namespace openwow::game {

namespace {

bool ClearTradeSkillSpellIdentifiers(std::uint32_t& active_spell_id,
                                     std::uint32_t& pending_spell_id) {
    const bool changed = active_spell_id != 0 || pending_spell_id != 0;
    active_spell_id = 0;
    pending_spell_id = 0;
    return changed;
}

}

ProfessionSystem& ProfessionSystem::Get() {
    static ProfessionSystem instance;
    return instance;
}

void ProfessionSystem::OpenTradeSkill(
    uint32_t skillLine, std::string linked_player) {
    std::lock_guard lock(mutex_);
    trade_skill_open_ = true;
    open_skill_line_  = skillLine;
    linked_trade_skill_player_ = std::move(linked_player);
}

void ProfessionSystem::CloseTradeSkill() {
    std::lock_guard lock(mutex_);
    trade_skill_open_ = false;
    open_skill_line_  = 0;
    linked_trade_skill_player_.clear();
}

bool ProfessionSystem::HasTradeSkillWindow() const {
    std::lock_guard lock(mutex_);
    return trade_skill_open_;
}

bool ProfessionSystem::IsTradeSkillLinked() const {
    std::lock_guard lock(mutex_);
    return !linked_trade_skill_player_.empty();
}

std::string ProfessionSystem::GetLinkedTradeSkillPlayer() const {
    std::lock_guard lock(mutex_);
    return linked_trade_skill_player_;
}

uint32_t ProfessionSystem::GetOpenSkillLine() const {
    std::lock_guard lock(mutex_);
    return open_skill_line_;
}

void ProfessionSystem::Reset() {
    std::lock_guard lock(mutex_);
    trade_skill_open_ = false;
    open_skill_line_  = 0;
    linked_trade_skill_player_.clear();
    npc_trade_skill_spell_      = 0;
    player_trade_skill_spell_   = 0;
    npc_trade_skill_repeat_     = 0;
    player_trade_skill_repeat_  = 0;
}

bool ProfessionSystem::ClearAllTradeSkillSpells() {
    std::lock_guard lock(mutex_);
    const bool changed = ClearTradeSkillSpellIdentifiers(
        npc_trade_skill_spell_, player_trade_skill_spell_);
    npc_trade_skill_repeat_ = 0;
    player_trade_skill_repeat_ = 0;
    return changed;
}

std::uint32_t ProfessionSystem::ClearTradeSkillSpell(uint32_t spell_id) {
    std::lock_guard lock(mutex_);
    std::uint32_t event_count = 0;
    if (spell_id == npc_trade_skill_spell_) {
        npc_trade_skill_spell_ = 0;
        npc_trade_skill_repeat_ = 0;
        ++event_count;
    }
    if (spell_id == player_trade_skill_spell_) {
        player_trade_skill_spell_ = 0;
        player_trade_skill_repeat_ = 0;
        ++event_count;
    }
    return event_count;
}

ProfessionSystem::TradeSkillCompletionAction
ProfessionSystem::OnTradeSkillSpellComplete(uint32_t spell_id) {
    std::lock_guard lock(mutex_);
    if (!npc_trade_skill_spell_) {
        if (!player_trade_skill_spell_) {
            return TradeSkillCompletionAction::kNone;
        }
        npc_trade_skill_spell_ = 0;
        player_trade_skill_spell_ = 0;
        npc_trade_skill_repeat_ = 0;
        player_trade_skill_repeat_ = 0;
        return TradeSkillCompletionAction::kCleared;
    }

    if (!npc_trade_skill_repeat_ || npc_trade_skill_spell_ != spell_id) {
        npc_trade_skill_spell_ = 0;
        player_trade_skill_spell_ = 0;
        npc_trade_skill_repeat_ = 0;
        player_trade_skill_repeat_ = 0;
        return TradeSkillCompletionAction::kCleared;
    }

    --npc_trade_skill_repeat_;
    if (npc_trade_skill_repeat_ == 0) {
        npc_trade_skill_spell_ = 0;
        return TradeSkillCompletionAction::kCompleted;
    }
    return TradeSkillCompletionAction::kRepeat;
}

void ProfessionSystem::TransferPlayerCraftToNpc(uint32_t spell_id) {
    std::lock_guard lock(mutex_);
    if (spell_id == player_trade_skill_spell_) {
        npc_trade_skill_spell_  = player_trade_skill_spell_;
        npc_trade_skill_repeat_ = player_trade_skill_repeat_;
    }
    player_trade_skill_repeat_ = 0;
    player_trade_skill_spell_  = 0;
}

void ProfessionSystem::QueueTradeSkillCraft(uint32_t spell_id,
                                             uint32_t repeat_count) {
    std::lock_guard lock(mutex_);
    if (spell_id == npc_trade_skill_spell_) {
        npc_trade_skill_repeat_ = repeat_count;
        return;
    }
    player_trade_skill_spell_ = spell_id;
    player_trade_skill_repeat_ = repeat_count;
}

bool ProfessionSystem::HasActiveTradeSkillSpell(uint32_t spell_id) const {
    std::lock_guard lock(mutex_);
    return spell_id != 0 && npc_trade_skill_spell_ == spell_id;
}

bool ProfessionSystem::StopTradeSkillRepeat() {
    std::lock_guard lock(mutex_);
    return ClearTradeSkillSpellIdentifiers(
        npc_trade_skill_spell_, player_trade_skill_spell_);
}

uint32_t ProfessionSystem::GetTradeSkillRepeatCount(
    uint32_t selected_spell_id) const {
    std::lock_guard lock(mutex_);
    uint32_t repeat_count = 1;
    if (selected_spell_id != 0 &&
        selected_spell_id == npc_trade_skill_spell_) {
        repeat_count = npc_trade_skill_repeat_;
    } else if (selected_spell_id != 0 &&
               selected_spell_id == player_trade_skill_spell_) {
        repeat_count = player_trade_skill_repeat_;
    }
    return repeat_count <= 1 ? 1 : repeat_count;
}

void ProfessionSystem::SetTradeSkillSpellStateForTests(
    uint32_t npc_spell_id, uint32_t npc_repeat_count,
    uint32_t player_spell_id, uint32_t player_repeat_count) {
    std::lock_guard lock(mutex_);
    npc_trade_skill_spell_ = npc_spell_id;
    npc_trade_skill_repeat_ = npc_repeat_count;
    player_trade_skill_spell_ = player_spell_id;
    player_trade_skill_repeat_ = player_repeat_count;
}

uint32_t ProfessionSystem::GetNpcTradeSkillSpell() const {
    std::lock_guard lock(mutex_);
    return npc_trade_skill_spell_;
}

uint32_t ProfessionSystem::GetPlayerTradeSkillSpell() const {
    std::lock_guard lock(mutex_);
    return player_trade_skill_spell_;
}

uint32_t ProfessionSystem::GetNpcTradeSkillRepeat() const {
    std::lock_guard lock(mutex_);
    return npc_trade_skill_repeat_;
}

uint32_t ProfessionSystem::GetPlayerTradeSkillRepeat() const {
    std::lock_guard lock(mutex_);
    return player_trade_skill_repeat_;
}

}
