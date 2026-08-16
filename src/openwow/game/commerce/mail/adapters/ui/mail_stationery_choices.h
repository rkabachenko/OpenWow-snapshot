#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::game {

class ItemDefinitions;
class PlayerInventoryReplica;
class QueryCache;

struct MailStationeryListing {
  std::uint32_t stationery_id = 0;
  std::uint32_t item_id = 0;
  std::string name;
  std::string icon_path;
  std::string selected_texture;
  std::uint32_t buy_price = 0;
  bool is_default = false;
  bool is_owned = false;
};

class MailStationeryChoices {
 public:
  ~MailStationeryChoices();

  void Prime(const data::dbc::DbcLoader* dbc, QueryCache& queries,
             ItemDefinitions& item_definitions, PlayerInventoryReplica& inventory);

  [[nodiscard]] std::vector<MailStationeryListing> Refresh(
      const data::dbc::DbcLoader* dbc, QueryCache& queries,
      ItemDefinitions& item_definitions, PlayerInventoryReplica& inventory);
  void Reset();

 private:
  struct Definition {
    std::uint32_t stationery_id = 0;
    std::uint32_t item_id = 0;
    std::string texture;
    bool is_default = false;
  };

  static std::uint64_t ComputeDefinitionSignature(
      const data::dbc::DbcLoader& dbc);
  void ResetLocked();
  void ConfigureLocked(const data::dbc::DbcLoader* dbc,
                       QueryCache& queries, ItemDefinitions& item_definitions,
                       PlayerInventoryReplica& inventory,
                       std::uint64_t signature);
  void QueueMissingTemplatesLocked();
  [[nodiscard]] std::vector<MailStationeryListing>
  BuildVisibleListingsLocked() const;
  void OnItemTemplateResolved(std::uint64_t generation,
                              std::uint32_t item_id);

  std::mutex mutex_;
  const data::dbc::DbcLoader* dbc_ = nullptr;
  QueryCache* queries_ = nullptr;
  ItemDefinitions* item_definitions_ = nullptr;
  PlayerInventoryReplica* inventory_ = nullptr;
  std::uint64_t dbc_signature_ = 0;
  std::uint64_t generation_ = 0;
  std::vector<Definition> definitions_;
  std::vector<std::uint32_t> pending_item_ids_;
  std::vector<MailStationeryListing> visible_listings_;
};

}
