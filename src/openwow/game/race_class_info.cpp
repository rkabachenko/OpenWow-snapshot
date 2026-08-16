
#include "openwow/game/race_class_info.h"

#include <algorithm>

namespace openwow::game {

RaceClassInfoManager& RaceClassInfoManager::Get() {
    static RaceClassInfoManager instance;
    return instance;
}

void RaceClassInfoManager::AddRaceInfo(const RaceInfoDisplayEntry& entry) {
    std::lock_guard lock(mutex_);

    for (auto& r : races_) {
        if (r.raceId == entry.raceId) {
            r = entry;
            return;
        }
    }
    races_.push_back(entry);
}

std::optional<RaceInfoDisplayEntry> RaceClassInfoManager::GetRaceInfo(
    uint32_t raceId) const {
    std::lock_guard lock(mutex_);
    for (const auto& r : races_) {
        if (r.raceId == raceId) return r;
    }
    return std::nullopt;
}

std::vector<RaceInfoDisplayEntry> RaceClassInfoManager::GetAllRaces() const {
    std::lock_guard lock(mutex_);
    return races_;
}

std::vector<RaceInfoDisplayEntry> RaceClassInfoManager::GetRacesByFaction(
    uint32_t factionId) const {
    std::lock_guard lock(mutex_);
    std::vector<RaceInfoDisplayEntry> result;
    for (const auto& r : races_) {
        if (r.factionId == factionId) result.push_back(r);
    }
    return result;
}

void RaceClassInfoManager::AddClassInfo(const ClassInfoDisplayEntry& entry) {
    std::lock_guard lock(mutex_);
    for (auto& c : classes_) {
        if (c.classId == entry.classId) {
            c = entry;
            return;
        }
    }
    classes_.push_back(entry);
}

std::optional<ClassInfoDisplayEntry> RaceClassInfoManager::GetClassInfo(
    uint32_t classId) const {
    std::lock_guard lock(mutex_);
    for (const auto& c : classes_) {
        if (c.classId == classId) return c;
    }
    return std::nullopt;
}

std::vector<ClassInfoDisplayEntry> RaceClassInfoManager::GetAllClasses() const {
    std::lock_guard lock(mutex_);
    return classes_;
}

void RaceClassInfoManager::SetRaceClassAllowed(uint32_t raceId,
                                               uint32_t classId,
                                               bool allowed) {
    std::lock_guard lock(mutex_);
    for (auto& c : combos_) {
        if (c.raceId == raceId && c.classId == classId) {
            c.isAllowed = allowed;
            return;
        }
    }
    combos_.push_back({raceId, classId, allowed});
}

bool RaceClassInfoManager::IsRaceClassAllowed(uint32_t raceId,
                                              uint32_t classId) const {
    std::lock_guard lock(mutex_);
    for (const auto& c : combos_) {
        if (c.raceId == raceId && c.classId == classId) return c.isAllowed;
    }
    return false;
}

std::vector<uint32_t> RaceClassInfoManager::GetAllowedClassesForRace(
    uint32_t raceId) const {
    std::lock_guard lock(mutex_);
    std::vector<uint32_t> result;
    for (const auto& c : combos_) {
        if (c.raceId == raceId && c.isAllowed) result.push_back(c.classId);
    }
    return result;
}

std::vector<uint32_t> RaceClassInfoManager::GetAllowedRacesForClass(
    uint32_t classId) const {
    std::lock_guard lock(mutex_);
    std::vector<uint32_t> result;
    for (const auto& c : combos_) {
        if (c.classId == classId && c.isAllowed) result.push_back(c.raceId);
    }
    return result;
}

void RaceClassInfoManager::RegisterWotLKDefaults() {
    std::lock_guard lock(mutex_);
    races_.clear();
    classes_.clear();
    combos_.clear();

    races_.push_back({1,  "Human",    0, "Recent discoveries have revealed that humans are descended from the barbaric vrykul.",
        {{20549, "Every Man for Himself", "Removes all movement impairing effects and all effects which cause loss of control."},
         {20598, "The Human Spirit", "Increases spirit by 3%."},
         {20864, "Mace Specialization", "Expertise with maces and two-handed maces increased by 3."},
         {20597, "Sword Specialization", "Expertise with swords and two-handed swords increased by 3."},
         {58985, "Diplomacy", "Reputation gains increased by 10%."},
         {20599, "Perception", "Dramatically increases stealth detection for 20 sec."}},
        "Northshire Valley"});

    races_.push_back({3,  "Dwarf",    0, "The bold and courageous dwarves are an ancient race descended from the earthen.",
        {{20594, "Stoneform", "Removes all poison, disease, and bleed effects and increases armor by 10% for 8 sec."},
         {20595, "Gun Specialization", "Your critical strike chance with guns is increased by 1%."},
         {20596, "Frost Resistance", "Reduces the chance you will be hit by Frost spells by 2%."},
         {2481,  "Find Treasure", "Allows the dwarf to sense nearby treasure, making it appear on the minimap."},
         {59224, "Mace Specialization", "Expertise with maces and two-handed maces increased by 5."}},
        "Coldridge Valley"});

    races_.push_back({4,  "Night Elf", 0, "The ancient and reclusive night elves have played a pivotal role in shaping Azeroth's fate.",
        {{20583, "Shadowmeld", "Activate to slip into the shadows, reducing the chance for enemies to detect your presence."},
         {20582, "Quickness", "Reduces the chance that melee and ranged attackers will hit you by 2%."},
         {20585, "Wisp Spirit", "Transform into a wisp upon death, increasing speed by 75%."},
         {20584, "Nature Resistance", "Reduces the chance you will be hit by Nature spells by 2%."},
         {21009, "Elusiveness", "Reduces the chance enemies have to detect you while Shadowmelded or Stealthed."}},
        "Shadowglen"});

    races_.push_back({7,  "Gnome",    0, "The eccentric, often brilliant gnomes are one of the most peculiar races of the world.",
        {{20589, "Escape Artist", "Escape the effects of any immobilization or movement speed reduction effect."},
         {20591, "Expansive Mind", "Increase Intellect by 5%."},
         {20593, "Arcane Resistance", "Reduces the chance you will be hit by Arcane spells by 2%."},
         {20592, "Engineering Specialization", "Engineering skill increased by 15."}},
        "New Tinkertown"});

    races_.push_back({11, "Draenei",  0, "Driven from their homeworld of Argus, the draenei traveled throughout the cosmos.",
        {{28875, "Gemcutting", "Jewelcrafting skill increased by 5."},
         {59542, "Gift of the Naaru", "Heals the target for 20% of their total health over 15 sec."},
         {6562,  "Heroic Presence", "Increases chance to hit with all spells and attacks by 1%."},
         {59535, "Shadow Resistance", "Reduces the chance you will be hit by Shadow spells by 2%."}},
        "Ammen Vale"});

    races_.push_back({2,  "Orc",      1, "Unlike the other races of the Horde, orcs are not native to Azeroth.",
        {{33697, "Blood Fury", "Increases melee attack power and spell damage for 15 sec."},
         {20573, "Hardiness", "Duration of Stun effects reduced by an additional 15%."},
         {20574, "Axe Specialization", "Expertise with Fist Weapons, Axes and Two-Handed Axes increased by 5."},
         {20575, "Command", "Damage dealt by Death Knight, Hunter and Warlock pets increased by 5%."}},
        "Valley of Trials"});

    races_.push_back({5,  "Undead",   1, "Having broken free from the tyrannical rule of the Lich King, a group of undead seek to retain their own free will.",
        {{7744,  "Will of the Forsaken", "Removes any Charm, Fear and Sleep effect."},
         {20577, "Cannibalize", "When activated, regenerates 7% of total health every 2 sec for 10 sec."},
         {20579, "Shadow Resistance", "Reduces the chance you will be hit by Shadow spells by 2%."},
         {5227,  "Underwater Breathing", "Underwater breath lasts 233% longer than normal."}},
        "Deathknell"});

    races_.push_back({6,  "Tauren",   1, "The tauren are huge, bestial creatures who live in the grassy, open barrens of central Kalimdor.",
        {{20549, "War Stomp", "Stuns up to 5 enemies within 8 yds for 2 sec."},
         {20550, "Endurance", "Base Health increased by 5%."},
         {20551, "Nature Resistance", "Reduces the chance you will be hit by Nature spells by 2%."},
         {20552, "Cultivation", "Herbalism skill increased by 15."}},
        "Camp Narache"});

    races_.push_back({8,  "Troll",    1, "The savage trolls of the Darkspear tribe were once at the brink of extinction.",
        {{26297, "Berserking", "Increases your casting and attack speed by 20% for 10 sec."},
         {26290, "Bow Specialization", "Your critical strike chance with bows is increased by 1%."},
         {20557, "Throwing Specialization", "Your critical strike chance with throwing weapons is increased by 1%."},
         {20558, "Regeneration", "Health regeneration rate increased by 10%. 10% of total Health regeneration may continue during combat."},
         {58943, "Da Voodoo Shuffle", "Reduces the duration of all movement impairing effects by 15%."},
         {20555, "Beast Slaying", "Damage dealt versus Beasts increased by 5%."}},
        "Echo Isles"});

    races_.push_back({10, "Blood Elf", 1, "For nearly 7,000 years, high elven society centered on the Sunwell.",
        {{28877, "Arcane Affinity", "Enchanting skill increased by 10."},
         {50613, "Arcane Torrent", "Silence all enemies within 8 yards for 2 sec and restores some resources."},
         {822,   "Magic Resistance", "Reduces the chance you will be hit by spells by 2%."}},
        "Sunstrider Isle"});

    classes_.push_back({1,  "Warrior",      "Warriors equip themselves carefully for combat and engage their enemies head-on.",
        "Strength, Stamina", "Tank, Melee DPS", "Interface\\Icons\\ClassIcon_Warrior"});
    classes_.push_back({2,  "Paladin",      "Guardians of the Holy Light, paladins bolster their allies with holy auras and blessings.",
        "Strength, Intellect, Stamina", "Tank, Healer, Melee DPS", "Interface\\Icons\\ClassIcon_Paladin"});
    classes_.push_back({3,  "Hunter",       "Hunters battle their foes at a distance, commanding their pets to attack.",
        "Agility, Intellect", "Ranged DPS", "Interface\\Icons\\ClassIcon_Hunter"});
    classes_.push_back({4,  "Rogue",        "Rogues often initiate combat with a surprise attack from the shadows.",
        "Agility, Stamina", "Melee DPS", "Interface\\Icons\\ClassIcon_Rogue"});
    classes_.push_back({5,  "Priest",       "Priests use powerful healing magic to fortify themselves and their allies.",
        "Intellect, Spirit", "Healer, Ranged DPS", "Interface\\Icons\\ClassIcon_Priest"});
    classes_.push_back({6,  "Death Knight", "Death Knights are melee fighters who wield dark magic.",
        "Strength, Stamina", "Tank, Melee DPS", "Interface\\Icons\\ClassIcon_DeathKnight"});
    classes_.push_back({7,  "Shaman",       "Shaman are spiritual guides and practitioners, harnessing the elements.",
        "Intellect, Agility, Stamina", "Healer, Ranged DPS, Melee DPS", "Interface\\Icons\\ClassIcon_Shaman"});
    classes_.push_back({8,  "Mage",         "Mages wield the elements of fire, frost, and the arcane.",
        "Intellect, Spirit", "Ranged DPS", "Interface\\Icons\\ClassIcon_Mage"});
    classes_.push_back({9,  "Warlock",      "Warlocks are masters of shadow, flame, and demonic power.",
        "Intellect, Stamina", "Ranged DPS", "Interface\\Icons\\ClassIcon_Warlock"});
    classes_.push_back({11, "Druid",        "Druids harness the vast powers of nature to preserve balance and protect life.",
        "Intellect, Agility, Spirit, Stamina", "Tank, Healer, Ranged DPS, Melee DPS", "Interface\\Icons\\ClassIcon_Druid"});

    for (uint32_t cls : {1u, 2u, 4u, 5u, 6u, 8u, 9u})
        combos_.push_back({1, cls, true});

    for (uint32_t cls : {1u, 3u, 4u, 6u, 7u, 9u})
        combos_.push_back({2, cls, true});

    for (uint32_t cls : {1u, 2u, 3u, 4u, 5u, 6u})
        combos_.push_back({3, cls, true});

    for (uint32_t cls : {1u, 3u, 4u, 5u, 6u, 11u})
        combos_.push_back({4, cls, true});

    for (uint32_t cls : {1u, 4u, 5u, 6u, 8u, 9u})
        combos_.push_back({5, cls, true});

    for (uint32_t cls : {1u, 3u, 6u, 7u, 11u})
        combos_.push_back({6, cls, true});

    for (uint32_t cls : {1u, 4u, 6u, 8u, 9u})
        combos_.push_back({7, cls, true});

    for (uint32_t cls : {1u, 3u, 4u, 5u, 6u, 7u, 8u})
        combos_.push_back({8, cls, true});

    for (uint32_t cls : {2u, 3u, 4u, 5u, 6u, 8u, 9u})
        combos_.push_back({10, cls, true});

    for (uint32_t cls : {1u, 2u, 3u, 5u, 6u, 7u, 8u})
        combos_.push_back({11, cls, true});
}

std::string RaceClassInfoManager::GetFactionName(uint32_t factionId) const {
    if (factionId == 0) return "Alliance";
    if (factionId == 1) return "Horde";
    return "Unknown";
}

void RaceClassInfoManager::Reset() {
    std::lock_guard lock(mutex_);
    races_.clear();
    classes_.clear();
    combos_.clear();
}

}
