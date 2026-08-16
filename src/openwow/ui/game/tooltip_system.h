
#pragma once
#include "openwow/ui/game/tooltip_builder.h"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
namespace openwow::game {
class CGGameObject_C;
class EquipmentSets;
class ItemDefinitions;
class ObjectManager;
class PlayerInventoryReplica;
class WorldSession;
}
namespace openwow::data::dbc {
class DbcLoader;
}
namespace openwow::ui::game {
struct TooltipLine {
  std::string left_text;
  std::string right_text;
  float left_r = 1.0f, left_g = 1.0f, left_b = 1.0f;
  float right_r = 1.0f, right_g = 1.0f, right_b = 1.0f;
  bool wrap = false;
  bool operator==(const TooltipLine&) const = default;
};
struct TooltipFrameStackEntry {
  std::string label;
  int strata = 0;
  int level = 0;
  bool mouse_enabled = false;
  bool visible = false;
};
struct TooltipFrameStackSnapshot {
  float cursor_x = 0.0f;
  float cursor_y = 0.0f;
  std::vector<TooltipFrameStackEntry> entries;
};
struct TooltipMoneyScriptArgs {
  std::uint32_t cost = 0;
  std::optional<std::uint32_t> max_cost;
};

struct TooltipItemDisplayOptions {
  bool name_only{false};
  bool socket_to_destroy{false};
};

struct TooltipTextureData {
  std::string filename;
  float tex_coords[4] = {};
  std::uint32_t vertex_color = 0xFFFFFFFF;

  std::uint32_t line_index{0};
  bool operator==(const TooltipTextureData&) const = default;
};
int CompareTooltipFrameStackEntry(const TooltipFrameStackEntry& left,
                                  const TooltipFrameStackEntry& right);
class TooltipSystem {
 public:
  TooltipSystem();
  ~TooltipSystem();
  TooltipSystem(const TooltipSystem&) = delete;
  TooltipSystem& operator=(const TooltipSystem&) = delete;

  class ScopedActivation final {
   public:
    explicit ScopedActivation(TooltipSystem& tooltip) noexcept;
    ~ScopedActivation();
    ScopedActivation(const ScopedActivation&) = delete;
    ScopedActivation& operator=(const ScopedActivation&) = delete;
   private:
    TooltipSystem* previous_{nullptr};
  };
  static TooltipSystem& Get();
  static void ResetDefault();
  void BindDbcLoader(const openwow::data::dbc::DbcLoader* dbc);
  void BindWorldSession(openwow::game::WorldSession* session);
  void BindEquipmentSets(
      const openwow::game::EquipmentSets* equipment) {
    equipment_ = equipment;
  }
  [[nodiscard]] const openwow::game::EquipmentSets* GetEquipmentSets()
      const noexcept {
    return equipment_;
  }
  void BindInventory(
      const openwow::game::PlayerInventoryReplica* inventory) {
    inventory_ = inventory;
  }
  [[nodiscard]] const openwow::game::PlayerInventoryReplica* GetInventory()
      const noexcept {
    return inventory_;
  }
  void BindItemDefinitions(openwow::game::ItemDefinitions* item_definitions) {
    item_definitions_ = item_definitions;
  }
  [[nodiscard]] openwow::game::ItemDefinitions* GetItemDefinitions()
      const noexcept {
    return item_definitions_;
  }
  void SetOwner(const std::string& frameName,
                const std::string& anchor = "ANCHOR_LEFT");
  void SetAnchor(const std::string& anchor);
  void ClearLines();
  void AddLine(const std::string& text, float r = 1.0f, float g = 1.0f,
               float b = 1.0f, bool wrap = false);
  void AddDoubleLine(const std::string& leftText,
                     const std::string& rightText, float leftR = 1.0f,
                     float leftG = 1.0f, float leftB = 1.0f,
                     float rightR = 1.0f, float rightG = 1.0f,
                     float rightB = 1.0f, bool wrap = false);
  void AppendToFirstLine(const std::string& text);
  void AddMoneyLine(const std::string& prefix, std::uint32_t money,
                    const std::string& suffix = {});
  void Show();

  void Hide();
  void Reset();

  void FadeOut();

  [[nodiscard]] bool IsFading() const;
  [[nodiscard]] float GetFadeTimer() const;

  [[nodiscard]] bool AdvanceFade(float elapsed_seconds);
  void ClearFadeState();

  void SetItemById(std::uint32_t itemId);
  void SetItemByLink(const std::string& link);
  void SetItemFromLoot(std::uint32_t itemId,
                       std::int32_t randomPropertyId,
                       std::uint32_t suffixFactor,
                       std::uint32_t playerLevel = 0,
                       std::uint64_t itemGuid = 0,
                       TooltipItemDisplayOptions options = {});
  [[nodiscard]] bool SetItemWithInstanceData(
      std::uint32_t itemId,
      std::int32_t randomPropertyId,
      std::uint32_t suffixFactor,
      const openwow::ui::TooltipItemInstanceData& instanceData,
      std::uint32_t playerLevel = 0,
      std::uint64_t itemGuid = 0,
      TooltipItemDisplayOptions options = {});
  void OverrideItemIdentity(std::string itemName, std::string itemLink);
  bool SetSpellById(std::uint32_t spellId,
                    bool showRank = true,
                    bool isPet = false);
  void SetSpellId(std::uint32_t spellId);
  void SetSecondarySpellId(std::uint32_t spellId);

  bool SetUnit(const std::string& unitToken, bool hideStatus = false);
  void SetWorldGameObject(const openwow::game::CGGameObject_C& game_object);
  void HandlePendingWorldGameObjectRefresh(std::uint32_t generation,
                                           bool success);

  void PublishToLiveGameTooltipFrame();
  void HideLiveGameTooltipFrame();
  void SetFrameStack(const TooltipFrameStackSnapshot& snapshot);
  const std::vector<TooltipLine>& GetLines() const;
  bool IsShown() const;
  [[nodiscard]] bool HasOwner() const;
  std::string GetOwnerName() const;
  std::string GetAnchor() const;
  int GetNumLines() const;
  [[nodiscard]] std::uint64_t GetClearGeneration() const;
  [[nodiscard]] std::uint64_t GetPresentationRevision() const;

  void SetMinimumWidth(float w, bool force = false);
  [[nodiscard]] float GetMinimumWidth() const;
  [[nodiscard]] bool IsForceMinWidth() const;
  void SetPadding(float p);
  [[nodiscard]] float GetPadding() const;
  void ClearTextures();

  void AddTexture(const std::string& filename, const float* tex_coords,
                  std::uint32_t vertex_color = 0xFFFFFFFF);
  [[nodiscard]] const std::vector<TooltipTextureData>& GetTextures() const;
  std::string GetLineText(int line) const;
  std::uint32_t GetItemId() const;
  std::uint64_t GetItemGuid() const;
  std::string GetItemName() const;
  std::string GetItemLink() const;
  std::uint32_t GetSpellId() const;
  std::uint32_t GetSecondarySpellId() const;
  [[nodiscard]] std::uint64_t GetUnitGuid() const;
  void SetUnitGuid(std::uint64_t guid);

  [[nodiscard]] std::uint32_t GetQuestId() const;
  void SetQuestId(std::uint32_t questId);

  [[nodiscard]] std::uint32_t GetAchievementId() const;
  void SetAchievementId(std::uint32_t id);
  [[nodiscard]] std::uint64_t GetAchievementPlayerGuid() const;
  void SetAchievementPlayerGuid(std::uint64_t guid);
  [[nodiscard]] std::uint8_t GetAchievementCompleted() const;
  void SetAchievementCompleted(std::uint8_t completed);
  [[nodiscard]] const std::array<std::uint32_t, 8>& GetAchievementCriteriaData() const;
  void SetAchievementCriteriaData(const std::array<std::uint32_t, 8>& data);
  [[nodiscard]] const std::array<std::uint32_t, 4>& GetAchievementCriteriaBitmask() const;
  void SetAchievementCriteriaBitmask(const std::array<std::uint32_t, 4>& mask);

  [[nodiscard]] int GetPendingSpellAsyncCount() const;
  void SetPendingSpellAsyncCount(int count);
  [[nodiscard]] std::optional<std::uint64_t> GetWorldGameObjectGuid() const;
  [[nodiscard]] bool HasStatusBar() const;
  [[nodiscard]] float GetStatusBarMin() const;
  [[nodiscard]] float GetStatusBarMax() const;
  [[nodiscard]] float GetStatusBarValue() const;
  void QueueMoneyScript(std::uint32_t cost,
                        std::optional<std::uint32_t> max_cost = std::nullopt);
  [[nodiscard]] std::optional<TooltipMoneyScriptArgs> TakePendingMoneyScript();
  [[nodiscard]] const openwow::data::dbc::DbcLoader* GetDbcLoader() const;

  [[nodiscard]] const openwow::game::ObjectManager* GetObjectManager() const;
  [[nodiscard]] openwow::game::WorldSession* GetWorldSession() const;

  void SetContentChangedCallback(std::function<void()> callback);
  void NotifyContentChanged();
  std::function<void(bool)> BindAsyncCallback(
      std::function<void(TooltipSystem&, bool)> callback);
 private:
  struct Lifetime {
    TooltipSystem* owner = nullptr;
  };
  struct PendingItemTemplateRefresh {
    std::uint32_t item_id = 0;
    std::int32_t random_property_id = 0;
    std::uint32_t suffix_factor = 0;
    std::uint32_t player_level = 0;
    std::uint64_t item_guid = 0;
    bool has_instance_data = false;
    openwow::ui::TooltipItemInstanceData instance_data;
    TooltipItemDisplayOptions display_options;
    std::uint32_t generation = 0;
    std::uint32_t callback_cookie = 0;
  };
  struct PendingWorldGameObjectRefresh {
    std::uint64_t guid = 0;
    std::uint32_t generation = 0;
  };
  void ResetPendingItemTemplateRefresh();
  void BeginPendingItemTemplateRefresh(std::uint32_t item_id,
                                       std::int32_t random_property_id,
                                       std::uint32_t suffix_factor,
                                       std::uint32_t player_level,
                                       std::uint64_t item_guid,
                                       const openwow::ui::TooltipItemInstanceData* instance_data,
                                       TooltipItemDisplayOptions display_options,
                                       std::uint32_t callback_cookie);
  void HandlePendingItemTemplateRefresh(std::uint32_t generation, bool success);
  [[nodiscard]] bool SetItemInternal(std::uint32_t itemId,
                                     std::int32_t randomPropertyId,
                                     std::uint32_t suffixFactor,
                                     std::uint32_t playerLevel,
                                     std::uint64_t itemGuid,
                                     const openwow::ui::TooltipItemInstanceData* instance_data,
                                     TooltipItemDisplayOptions display_options);
  void ResetPendingWorldGameObjectRefresh();
  void MarkPresentationChanged() noexcept;
  std::uint64_t clear_generation_{0};
  std::uint64_t presentation_revision_{0};
  std::vector<TooltipLine> lines_;
  std::string owner_;
  std::string anchor_ = "ANCHOR_LEFT";
  bool shown_ = false;
  std::uint32_t item_id_ = 0;
  std::uint64_t item_guid_ = 0;
  std::string item_name_;
  std::string item_link_;
  std::uint32_t spell_id_ = 0;
  std::uint32_t secondary_spell_id_ = 0;
  int pending_spell_async_count_ = 0;
  std::uint64_t unit_guid_ = 0;
  std::uint32_t quest_id_ = 0;
  std::uint32_t achievement_id_ = 0;
  std::uint64_t achievement_player_guid_ = 0;
  std::uint8_t achievement_completed_ = 0;
  std::array<std::uint32_t, 8> achievement_criteria_data_{};
  std::array<std::uint32_t, 4> achievement_criteria_bitmask_{};
  std::optional<std::uint64_t> world_game_object_guid_;
  bool world_game_object_rebuild_requested_ = false;
  bool has_status_bar_ = false;
  float status_bar_min_ = 0.0f;
  float status_bar_max_ = 1.0f;
  float status_bar_value_ = 0.0f;
  std::optional<TooltipMoneyScriptArgs> pending_money_script_;
  std::optional<PendingItemTemplateRefresh> pending_item_template_refresh_;
  std::optional<PendingWorldGameObjectRefresh> pending_world_game_object_refresh_;
  std::uint32_t next_pending_item_template_generation_ = 1;
  std::uint32_t next_pending_world_game_object_generation_ = 1;
  openwow::game::WorldSession* world_session_ = nullptr;
  const openwow::game::EquipmentSets* equipment_ = nullptr;
  const openwow::game::PlayerInventoryReplica* inventory_ = nullptr;
  openwow::game::ItemDefinitions* item_definitions_ = nullptr;
  const openwow::data::dbc::DbcLoader* dbc_ = nullptr;
  float min_width_ = 0.0f;
  bool force_min_width_ = false;
  float padding_ = 0.0f;
  bool fade_active_ = false;
  float fade_timer_ = 0.0f;
  std::vector<TooltipTextureData> textures_;
  std::function<void()> content_changed_callback_;
  std::shared_ptr<Lifetime> lifetime_;
};
}
