#include "openwow/data/formats/dbc/faction_reaction.h"

namespace openwow::data::dbc {

namespace {

[[nodiscard]] bool HasEnemyFaction(const FactionTemplateEntry& a,
                                   const FactionTemplateEntry& b) {
  for (const auto enemy : a.enemies) {
    if (enemy != 0u && enemy == b.faction) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool HasFriendFaction(const FactionTemplateEntry& a,
                                    const FactionTemplateEntry& b) {
  for (const auto friend_faction : a.friends) {
    if (friend_faction != 0u && friend_faction == b.faction) {
      return true;
    }
  }
  return false;
}

}

game::ReactionType ComputeFactionReaction(
    std::uint32_t faction_a, std::uint32_t faction_b,
    const DbcStore<FactionTemplateEntry>& store) {
  if (faction_a == faction_b && faction_a != 0) {
    return game::ReactionType::kFriendly;
  }

  const auto* a = store.LookupEntry(faction_a);
  const auto* b = store.LookupEntry(faction_b);

  if (!a || !b) {
    return game::ReactionType::kNeutral;
  }

  return ComputeFactionReactionForEntries(*a, *b);
}

game::ReactionType ComputeFactionReactionForEntries(
    const FactionTemplateEntry& entry_a, const FactionTemplateEntry& entry_b) {
  const auto* const a = &entry_a;
  const auto* const b = &entry_b;

  if ((a->enemy_group & b->faction_group) != 0) {
    return game::ReactionType::kHostile;
  }

  if (HasEnemyFaction(*a, *b)) {
    return game::ReactionType::kHostile;
  }

  if ((a->friend_group & b->faction_group) != 0) {
    return game::ReactionType::kFriendly;
  }
  if ((b->friend_group & a->faction_group) != 0) {
    return game::ReactionType::kFriendly;
  }

  if (HasFriendFaction(*a, *b)) {
    return game::ReactionType::kFriendly;
  }
  if (HasFriendFaction(*b, *a)) {
    return game::ReactionType::kFriendly;
  }

  if ((a->flags & kFactionFlagHostileByDefault) != 0) {
    return game::ReactionType::kHostile;
  }

  return game::ReactionType::kNeutral;
}

int ComputeCorpseReactionLevel(
    const std::uint32_t source_template, const std::uint32_t target_template,
    const DbcStore<FactionTemplateEntry>& store) {
  const auto* source = store.LookupEntry(source_template);
  const auto* target = store.LookupEntry(target_template);
  if (source == nullptr || target == nullptr) {
    return static_cast<int>(game::ReactionType::kNeutral);
  }

  if ((target->faction_group & source->enemy_group) != 0) {
    return static_cast<int>(game::ReactionType::kHostile);
  }

  if (HasEnemyFaction(*source, *target)) {
    return static_cast<int>(game::ReactionType::kHostile);
  }

  if ((target->faction_group & source->friend_group) != 0) {
    return static_cast<int>(game::ReactionType::kFriendly);
  }

  if (HasFriendFaction(*source, *target)) {
    return static_cast<int>(game::ReactionType::kFriendly);
  }

  if ((source->faction_group & target->friend_group) != 0) {
    return static_cast<int>(game::ReactionType::kFriendly);
  }

  if (HasFriendFaction(*target, *source)) {
    return static_cast<int>(game::ReactionType::kFriendly);
  }

  if ((source->flags & kFactionFlagHostileByDefault) != 0) {
    return static_cast<int>(game::ReactionType::kHostile);
  }

  return static_cast<int>(game::ReactionType::kNeutral);
}

bool IsPvPFactionTemplate(std::uint32_t faction_template_id,
                          const DbcStore<FactionTemplateEntry>& store) {
  const auto* entry = store.LookupEntry(faction_template_id);
  return entry && (entry->flags & kFactionFlagPvP) != 0;
}

}
