#pragma once

#include <cstdint>
#include <vector>

namespace openwow::game {

class WorldSession;

class SpellbookPrivateUsability {
 public:
  void Reset() noexcept;

  void OnSpellLearned(WorldSession& session, std::uint32_t spell_id,
                      std::uint32_t superseded_spell_id);

  void OnSpellForgotten(WorldSession& session, std::uint32_t spell_id);

  void Refresh(WorldSession& session);

  void RefreshPower(WorldSession& session);

  struct Entry {
    std::uint32_t spell_id = 0;
    bool broad_usable = false;
    bool power_unavailable = false;
  };

 private:
  std::vector<Entry> first_collection_;
  std::vector<Entry> stance_collection_;
};

}
