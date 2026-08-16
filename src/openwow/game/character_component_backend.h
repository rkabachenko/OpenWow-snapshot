#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace openwow::ui::game {
class CVarSystem;
}

namespace openwow::data::dbc {
template <typename T> class DbcStore;
struct CharSectionsEntry;
struct CharacterFacialHairStylesEntry;
}

namespace openwow::data::dbc {
struct ItemDisplayInfoEntry;
}

namespace openwow::game {

inline constexpr std::size_t kCharacterComponentStandardTextureCount = 10;
inline constexpr std::size_t kCharacterComponentMaxExtraTextureCount = 40;
inline constexpr std::size_t kCharacterComponentPageSlotCount = 15;
inline constexpr std::size_t kCharacterComponentCleanupSlotCount = 7;
inline constexpr std::size_t kCharacterModelComponentTableRowCount = 5;
inline constexpr std::size_t kCharacterModelComponentTableColumnCount = 3;
inline constexpr int kCharacterComponentTextureFormat = 5;

inline constexpr std::array<std::string_view, 8> kArmorRegionTextureSectionNames = {{
    "ArmUpperTexture",
    "ArmLowerTexture",
    "HandTexture",
    "TorsoUpperTexture",
    "TorsoLowerTexture",
    "LegUpperTexture",
    "LegLowerTexture",
    "FootTexture",
}};

inline constexpr std::array<char, 2> kGenderSuffixChar = {{'M', 'F'}};
inline constexpr char kUnisexSuffixChar = 'U';

struct CharacterComponentStartupSelection {
  int texture_format = kCharacterComponentTextureFormat;
  std::int32_t requested_texture_level = 8;
  bool component_thread = true;
  bool component_compress = true;
  std::uint32_t processor_count = 2;
};

struct CharacterComponentBackendConfig {
  int texture_format = 5;
  std::uint32_t selected_texture_level = 8;
  bool worker_thread_enabled = true;
  bool compression_enabled = true;
  std::uint32_t composite_texture_edge = 256;
};

struct CharacterComponentWorkerMipChainSeed {
  int allocation_format = 0;
  std::uint32_t texture_edge = 0;
};

struct CharacterComponentWorkerMipPoolState {
  std::vector<CharacterComponentWorkerMipChainSeed> general_free_mip_chains;
  std::vector<CharacterComponentWorkerMipChainSeed> special_free_mip_chains;
};

struct CharacterComponentWorkerBackendState {
  bool active = false;
  std::array<bool, kCharacterComponentStandardTextureCount> texture_assemblers_installed{};
  CharacterComponentWorkerMipPoolState mip_pools{};
  int wake_event_reset_calls = 0;
  bool evt_context_captured = false;
  std::uintptr_t evt_context_token = 0;
  bool worker_thread_started = false;
  std::string worker_thread_name;
  int worker_thread_create_calls = 0;
};

struct CharacterComponentBackendShutdownState;
struct CharacterComponentBaseSectionLookupRowState;

struct CharacterComponentBaseSectionEntry {
  std::array<std::string, 3> texture_names{};
  std::uint32_t flags = 0;
};

struct CharacterComponentBaseSectionTable {
  std::map<std::tuple<int, int, int>, std::string> texture_paths;
  std::map<std::tuple<int, int, int, int, int>, CharacterComponentBaseSectionEntry> entries;

  [[nodiscard]] const CharacterComponentBaseSectionEntry *
  FindEntry(int race, int gender, int section, int subsection, int choice) const;
  [[nodiscard]] const std::string *FindTexturePath(int race, int gender, int choice) const;
};

struct CharacterComponentTextureLoadState {

  std::function<std::uintptr_t(const char *, std::uintptr_t)> load_texture;
  std::function<void(std::uintptr_t)> release_texture;
  std::uintptr_t status = 0;
  std::uintptr_t last_texture_handle = 0;
  int load_calls = 0;
  std::string requested_path;
  int release_calls = 0;
};

struct CharacterModelTextureReplacementState {
  int replace_calls = 0;
  int replaced_unit = -1;
  std::uintptr_t replaced_texture_handle = 0;
};

using CharacterComponentTextureUnit8State = CharacterModelTextureReplacementState;

[[nodiscard]] bool LoadTextureUnit8FromBaseComponentSection(
    const CharacterComponentBaseSectionTable &table, int race, int gender, int choice,
    CharacterComponentTextureLoadState &load_state,
    CharacterComponentTextureUnit8State &unit8_state, std::string &scratch_path);

[[nodiscard]] CharacterComponentBackendConfig
ResolveCharacterComponentStartupSelection(CharacterComponentStartupSelection selection);

[[nodiscard]] CharacterComponentBackendConfig
InitializeCharacterComponentBackend(int texture_format, std::uint32_t requested_texture_level,
                                    bool worker_thread_enabled, bool compression_enabled);

[[nodiscard]] std::uint32_t ResolveCharacterComponentLookupRowCount(
    const openwow::data::dbc::DbcStore<openwow::data::dbc::CharSectionsEntry> &char_sections,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::CharacterFacialHairStylesEntry>
        &facial_hair_styles);

[[nodiscard]] CharacterComponentBaseSectionTable BuildCharacterComponentBaseSectionTable(
    const openwow::data::dbc::DbcStore<openwow::data::dbc::CharSectionsEntry> &char_sections,
    std::uint32_t lookup_row_count,
    std::vector<CharacterComponentBaseSectionLookupRowState> *lookup_row_storage = nullptr);

[[nodiscard]] std::vector<std::uint32_t> BuildCharacterComponentFacialHairStyleCounts(
    const openwow::data::dbc::DbcStore<openwow::data::dbc::CharacterFacialHairStylesEntry>
        &facial_hair_styles,
    std::uint32_t lookup_row_count);

[[nodiscard]] CharacterComponentWorkerMipPoolState
PrimeCharacterComponentWorkerMipPools(const CharacterComponentBackendConfig &config);

[[nodiscard]] CharacterComponentWorkerBackendState
InitializeCharacterComponentWorkerBackend(const CharacterComponentBackendConfig &config,
                                          std::uintptr_t evt_context_token);

struct CharacterComponentBackendRuntimeState {
  bool initialized = false;
  CharacterComponentBackendConfig config{};
  CharacterComponentWorkerBackendState worker_backend{};
  std::shared_ptr<CharacterComponentBackendShutdownState> shutdown_state;
};

[[nodiscard]] CharacterComponentBackendRuntimeState
RegisterCharacterComponentCVarsAndInitializeBackend(
    openwow::ui::game::CVarSystem &cvars, std::uint32_t processor_count,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::CharSectionsEntry> *char_sections =
        nullptr,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::CharacterFacialHairStylesEntry>
        *facial_hair_styles = nullptr);

[[nodiscard]] CharacterComponentBackendRuntimeState GetCharacterComponentBackendRuntimeState();

void ResetCharacterComponentBackendRuntimeStateForTests();

struct CharacterComponentTextureHandle {
  int source_key = -1;
  std::vector<std::uint32_t> page_tokens;
  std::int32_t reference_count = 1;
  std::uint32_t release_count = 0;
  std::uint32_t cleanup_count = 0;
  std::uint32_t unload_count = 0;
  std::uint32_t type_handle_release_count = 0;

  bool live_row_resolvable = true;
  bool load_complete = true;
  bool async_load_queued = false;
  bool pending_stream_object = false;
  bool can_touch_pending_stream_object = true;
  bool complete_when_async_load_requested = false;
  std::string texture_path;
  std::string resolved_texture_path;
  std::uint32_t async_file_size = 0;
  std::uint32_t async_buffer_size = 0;
  bool primary_path_open_succeeds = true;
  bool alternate_extension_open_succeeds = false;
  bool async_open_failed = false;
  std::uint32_t metadata_word0 = 0;
  std::uint32_t metadata_word1 = 0x00010000;
  std::uint32_t metadata_word0_after_sync_wait = 0;
  std::uint32_t metadata_word1_after_sync_wait = 0x00010000;
  int load_async_call_count = 0;
  int primary_open_attempt_count = 0;
  int alternate_open_attempt_count = 0;
  int touch_pending_stream_object_call_count = 0;
  int sync_wait_call_count = 0;

  void Retain();
  void Unload();
  void Release();
};

struct CharacterComponentTexturePageState {

  std::uint32_t loading_slot_mask = 0;
  std::array<std::shared_ptr<CharacterComponentTextureHandle>, kCharacterComponentPageSlotCount>
      live_slots{};
  std::array<std::shared_ptr<CharacterComponentTextureHandle>, kCharacterComponentPageSlotCount>
      queued_slots{};
};

using CharacterComponentTexturePages =
    std::array<CharacterComponentTexturePageState, kCharacterComponentStandardTextureCount>;

[[nodiscard]] bool QueueCharacterComponentTexture(
    CharacterComponentTexturePageState &page_state, std::size_t page_index, std::size_t slot_index,
    const std::shared_ptr<CharacterComponentTextureHandle> &pending_texture,
    bool explicit_base_texture_mode);

void ClearQueuedCharacterComponentTexture(CharacterComponentTexturePageState &page_state,
                                          int slot_index);

inline constexpr std::size_t kCharacterModelVisualItemSlotCount = 12;

struct CharacterComponentItemDisplayRecordState {
  std::uint32_t geoset_control_1 = 0;
  std::uint32_t geoset_control_2 = 0;
  std::uint32_t geoset_control_3 = 0;
  std::array<std::shared_ptr<CharacterComponentTextureHandle>, 8> component_textures{};

  [[nodiscard]] bool HasComponentTexture(std::size_t component_index) const {
    return component_index < component_textures.size() &&
           static_cast<bool>(component_textures[component_index]);
  }
};

struct CharacterComponentRequest;
struct CharacterComponentMipChain;

enum class CharacterBaseSkinUploadSourceKind : std::uint8_t {
  None,
  PendingRequest,
  GeneralFallback,
  SpecialFallback,
};

struct CharacterBaseSkinRenderTargetState {
  bool available = false;
  int create_calls = 0;
  int last_requested_data_format = 0;
  int last_created_format = 0;
  int replace_calls = 0;
  int replaced_texture_type = -1;
  int release_calls = 0;
  std::shared_ptr<CharacterComponentMipChain> general_upload_snapshot;
  std::shared_ptr<CharacterComponentMipChain> special_upload_snapshot;
  CharacterBaseSkinUploadSourceKind last_upload_source = CharacterBaseSkinUploadSourceKind::None;
  std::uint32_t last_upload_pitch = 0;
  std::uintptr_t last_upload_data = 0;
  int last_upload_action = -1;
  int last_upload_mip_level = -1;
};

struct CharacterModelExplicitBaseTextureState {
  std::string configured_path;
  std::string scratch_path;
  CharacterComponentTextureLoadState texture_type1_load{};
  CharacterModelTextureReplacementState texture_type1_replacement{};
  std::string unit8_scratch_path;
  CharacterComponentTextureLoadState texture_unit8_load{};
  CharacterModelTextureReplacementState texture_unit8_replacement{};
};

struct CharacterModelGeosetVisibilityState {
  int apply_calls = 0;
};

struct CharacterModelCompositeFlushState {
  std::uint32_t processed_dirty_mask = 0;
  std::vector<std::uint8_t> processed_regions;
  std::vector<std::uint8_t> blitted_regions;
  std::vector<std::pair<std::size_t, int>> standard_passes;
  std::vector<std::pair<std::uint8_t, int>> extra_passes;
  bool full_texture_blit = false;
  bool special_atlas_flush = false;
};

struct CharacterModelRefreshListState {
  bool linked = false;
  int unlink_calls = 0;
};

struct CharacterComponentCompositeExtraTexture {
  std::uint8_t component_index = 0;
  std::shared_ptr<CharacterComponentTextureHandle> texture;
};

struct CharacterComponentCompositeRegionState {
  std::shared_ptr<CharacterComponentTextureHandle> standard_texture;
  std::vector<CharacterComponentCompositeExtraTexture> extra_textures;
};

inline constexpr std::uint32_t kCharacterModelFlagSkinDirty = 0x1u;
inline constexpr std::uint32_t kCharacterModelFlagComponentTableDirty = 0x2u;
inline constexpr std::uint32_t kCharacterModelFlagGeosetsDirty = 0x4u;
inline constexpr std::uint32_t kCharacterModelFlagRefreshReady = 0x8u;
inline constexpr std::uint32_t kCharacterModelFlagGuildTabardOverride = 0x10u;
inline constexpr std::uint32_t kCharacterModelDisplayBehaviorFlag0x20 = 0x20u;

inline constexpr std::uint32_t kCharacterModelFlagForceEquipmentRefresh = 0x40u;
inline constexpr std::uint32_t kCharacterModelInitFlags = kCharacterModelFlagSkinDirty |
                                                          kCharacterModelFlagComponentTableDirty |
                                                          kCharacterModelFlagGeosetsDirty;
inline constexpr std::uint32_t kCharacterModelInitDirtyRegionMask = 0xFFFFFFFFu;
inline constexpr int kCharacterModelInvalidRequestType = -1;

struct CharacterComponentTextureRefreshOptions {
  bool allow_synchronous_wait = false;
  bool streaming_mode_enabled = false;
};

struct CharacterModelRefreshState {
  std::uint32_t flags = kCharacterModelInitFlags;
  std::uint32_t dirty_region_mask = kCharacterModelInitDirtyRegionMask;

  std::uint32_t display_behavior_flags = 0;

  bool explicit_base_texture_mode = false;

  bool request_type_2_upload_mode = false;
  int request_type = kCharacterModelInvalidRequestType;
  std::array<std::array<std::shared_ptr<CharacterComponentTextureHandle>,
                        kCharacterModelComponentTableColumnCount>,
             kCharacterModelComponentTableRowCount>
      component_table{};
  std::array<std::shared_ptr<CharacterComponentItemDisplayRecordState>,
             kCharacterModelVisualItemSlotCount>
      item_display_records{};
  CharacterBaseSkinRenderTargetState base_skin_render_target{};
  CharacterModelExplicitBaseTextureState explicit_base_texture{};
  CharacterModelGeosetVisibilityState geoset_visibility{};
  CharacterComponentTexturePages texture_pages{};
  std::array<CharacterComponentCompositeRegionState, kCharacterComponentStandardTextureCount>
      composite_regions{};
  CharacterModelCompositeFlushState composite_flush{};
  CharacterModelRefreshListState pending_link{};
  std::shared_ptr<CharacterComponentRequest> pending_request;
};

struct CharacterModelRefreshRuntimeState {
  bool finalize_refresh_in_progress = false;
};

struct CharacterModelRefreshQueueState {
  std::deque<CharacterModelRefreshState *> pending_models;
};

enum class CharacterModelSpecialItemRefreshTarget : std::uint8_t {
  Head,
  Shoulders,
  Cape,
  Quiver,
};

struct CharacterModelSpecialItemRefreshOperations {
  std::function<bool(CharacterModelSpecialItemRefreshTarget)> is_current;
  std::function<void(CharacterModelSpecialItemRefreshTarget)> clear_loaded;
  std::function<void(CharacterModelSpecialItemRefreshTarget, std::uint32_t)> load;
};

using CharacterModelItemDisplayLookupFn =
    std::function<const openwow::data::dbc::ItemDisplayInfoEntry *(std::uint32_t display_id)>;
using CharacterModelItemDisplayRecordFactory =
    std::function<std::shared_ptr<CharacterComponentItemDisplayRecordState>(
        const openwow::data::dbc::ItemDisplayInfoEntry &display_info)>;

struct CharacterModelRefreshQueueOperations {
  std::function<bool(CharacterModelRefreshState &model)> refresh_component_table;
  std::function<bool(CharacterModelRefreshState &model)> rebuild_component_slots;
  std::function<void(CharacterModelRefreshState &model)> queue_component_request;
  std::function<void(int force)> drain_completed_requests;
};

void EnsureCharacterBaseSkinRenderTarget(CharacterBaseSkinRenderTargetState &base_skin,
                                         int request_type);

void MarkCharacterModelDirty(CharacterModelRefreshState &model, std::uint32_t dirty_mask);

void MarkCharacterModelSkinDirty(CharacterModelRefreshState &model);

using SectionComponentTextureLoaderFn =
    std::function<std::shared_ptr<CharacterComponentTextureHandle>(const std::string &path)>;

[[nodiscard]] bool QueueSectionComponentTexture(
    CharacterModelRefreshState &model,
    const CharacterComponentBaseSectionTable &table,
    int race, int gender,
    unsigned int sectionType, int textureSlot,
    int variation, int color,
    unsigned int dirtyBit,
    const SectionComponentTextureLoaderFn &load_texture = nullptr);

using ComponentTexturePathResolverFn = std::function<bool(const std::string &path)>;

[[nodiscard]] std::string
BuildComponentTexturePath(std::size_t page_index,
                          std::string_view texture_name,
                          char suffix);

[[nodiscard]] std::string
ResolveComponentTexturePath(std::size_t page_index,
                            std::string_view texture_name,
                            std::uint8_t gender,
                            const ComponentTexturePathResolverFn &can_resolve);

[[nodiscard]] std::shared_ptr<CharacterComponentTextureHandle>
CreateLiveComponentTextureRow(std::size_t page_index,
                              std::string_view texture_name,
                              std::uint8_t gender,
                              const ComponentTexturePathResolverFn &can_resolve);

[[nodiscard]] bool InitializeCharacterModelExplicitBaseTextureMode(
    CharacterModelRefreshState &model, const CharacterComponentBaseSectionTable &base_section_table,
    int race, int gender, int choice);

void UpdateCharacterModelComponentTexturePage0(
    CharacterModelRefreshState &model, std::size_t item_slot,
    const CharacterComponentItemDisplayRecordState *item_record, bool queue_texture);

void UpdateCharacterModelComponentTexturePage1(
    CharacterModelRefreshState &model, std::size_t item_slot,
    const CharacterComponentItemDisplayRecordState *item_record, bool queue_texture);

void UpdateCharacterModelComponentTexturePage2(
    CharacterModelRefreshState &model, std::size_t item_slot,
    const CharacterComponentItemDisplayRecordState *item_record, bool queue_texture);

void UpdateCharacterModelComponentTexturePage3(
    CharacterModelRefreshState &model, std::size_t item_slot,
    const CharacterComponentItemDisplayRecordState *item_record, bool queue_texture);

void UpdateCharacterModelComponentTexturePage4(
    CharacterModelRefreshState &model, std::size_t item_slot,
    const CharacterComponentItemDisplayRecordState *item_record, bool queue_texture);

void UpdateCharacterModelComponentTexturePage5(
    CharacterModelRefreshState &model, std::size_t item_slot,
    const CharacterComponentItemDisplayRecordState *item_record, bool queue_texture);

void UpdateCharacterModelComponentTexturePage6(
    CharacterModelRefreshState &model, std::size_t item_slot, int default_slot_index,
    const CharacterComponentItemDisplayRecordState *item_record, bool queue_texture);

void UpdateCharacterModelComponentTexturePage7(
    CharacterModelRefreshState &model, std::size_t item_slot,
    const CharacterComponentItemDisplayRecordState *item_record, bool queue_texture);

[[nodiscard]] bool SetCharacterModelItemDisplayRecordAndRefresh(
    CharacterModelRefreshState &model, std::size_t item_slot,
    const std::shared_ptr<CharacterComponentItemDisplayRecordState> &item_record,
    std::uint32_t display_flags, std::uint32_t special_refresh_token,
    const CharacterModelSpecialItemRefreshOperations &special_refresh_ops = {});

[[nodiscard]] bool ResolveCharacterModelItemDisplayRecordAndRefresh(
    CharacterModelRefreshState &model, std::size_t item_slot, std::uint32_t display_id,
    const CharacterModelItemDisplayLookupFn &lookup_display_info,
    const CharacterModelItemDisplayRecordFactory &record_factory,
    std::uint32_t special_refresh_token = 0,
    const CharacterModelSpecialItemRefreshOperations &special_refresh_ops = {});

[[nodiscard]] bool SetCharacterModelEquipmentSlotDisplayId(
    CharacterModelRefreshState &model, std::uint32_t equipment_slot,
    std::uint32_t display_id,
    const CharacterModelItemDisplayLookupFn &lookup_display_info,
    const CharacterModelItemDisplayRecordFactory &record_factory,
    std::uint32_t special_refresh_token = 0,
    const CharacterModelSpecialItemRefreshOperations &special_refresh_ops = {});

void QueueCharacterModelRefreshAtHead(CharacterModelRefreshQueueState &queue,
                                      CharacterModelRefreshState &model);

void HandleCharacterModelInitRefreshGate(CharacterModelRefreshQueueState &queue,
                                         CharacterModelRefreshState &model);

void ShutdownCharacterModelRefreshState(CharacterModelRefreshState &model);

[[nodiscard]] bool
RefreshCharacterModelComponentTable(CharacterModelRefreshState &model,
                                    CharacterComponentTextureRefreshOptions options);

[[nodiscard]] bool
RebuildCharacterModelComponentSlots(CharacterModelRefreshState &model,
                                    CharacterComponentTextureRefreshOptions options);

void FullRefreshCharacterModelFromBaseSkinCallback(CharacterModelRefreshRuntimeState &runtime,
                                                   CharacterModelRefreshState &model);

void CharacterBaseSkinRenderTargetUploadCallback(CharacterModelRefreshRuntimeState &runtime,
                                                 CharacterModelRefreshState &model, int action,
                                                 std::uint32_t width, std::uint32_t height,
                                                 int cube_face, int mip_level,
                                                 std::uint32_t *pitch_out, const void **data_out);

void ReleaseLiveCharacterComponentTexturesForQueuedSlots(
    CharacterComponentTexturePages &texture_pages);

void FlushCharacterModelCompositeTexture(CharacterModelRefreshState &model,
                                         bool process_dirty_handlers);

void PopulateScalpCompositeRegion(
    CharacterModelRefreshState &model,
    const CharacterComponentBaseSectionTable &base_section_table,
    int race, int gender, int skin_color,
    std::uint8_t region_index,
    const std::shared_ptr<CharacterComponentTextureHandle> &standard_texture,
    const std::array<std::shared_ptr<CharacterComponentTextureHandle>, 3> &extra_textures);

void FinalizeCharacterModelRefresh(CharacterModelRefreshRuntimeState &runtime,
                                   CharacterModelRefreshState &model);

void FinalizeQueuedCharacterModelRefresh(CharacterModelRefreshRuntimeState &runtime,
                                         CharacterModelRefreshState &model);

void ProcessCharacterModelRefreshQueue(CharacterModelRefreshQueueState &queue,
                                       CharacterModelRefreshRuntimeState &runtime,
                                       const CharacterModelRefreshQueueOperations &operations,
                                       bool render_enabled, bool worker_thread_enabled);

struct CharacterComponentMipChain {
  struct MipLevelSurface {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint32_t> pixels;
  };

  struct Bc1MipLevel {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> blocks;
  };

  std::uint32_t chain_id = 0;
  std::vector<MipLevelSurface> mip_levels;
  std::vector<Bc1MipLevel> bc1_mip_levels;
  std::vector<std::pair<std::size_t, int>> standard_passes;
  std::vector<std::pair<std::uint8_t, int>> extra_passes;
  bool special_finalize_called = false;
  std::uintptr_t special_finalize_source_token = 0;
};

struct CharacterComponentMipFillRegion {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
};

struct CharacterComponentMipOrigin {
  std::int32_t x = 0;
  std::int32_t y = 0;
};

struct CharacterComponentPalettedTexture {
  std::uint16_t width = 0;
  std::uint16_t height = 0;
  std::uint8_t mip_count = 0;
  std::uint8_t alpha_depth = 0;
  bool has_palette = true;
  std::array<std::uint32_t, 256> palette_bgra{};
  std::vector<std::vector<std::uint8_t>> mip_payloads;
};

struct CharacterComponentRequest {
  static constexpr std::uint32_t kNeedsComposite = 0x1;
  static constexpr std::uint32_t kCompleted = 0x2;

  std::uint32_t flags = 0;
  int request_type = 0;
  std::array<int, kCharacterComponentStandardTextureCount> standard_texture_ids{};
  std::array<std::shared_ptr<CharacterComponentTextureHandle>,
             kCharacterComponentStandardTextureCount>
      standard_texture_handles{};
  std::vector<std::uint8_t> extra_texture_indices;
  std::vector<int> extra_texture_ids;
  std::vector<std::shared_ptr<CharacterComponentTextureHandle>> extra_texture_handles;
  std::shared_ptr<CharacterComponentMipChain> output_chain;
  std::uintptr_t source_token = 0;
};

using CharacterComponentAssemblerHandler = void (*)(CharacterModelRefreshState &model,
                                                    CharacterComponentRequest &request);

struct CharacterComponentAssemblerDispatchTable {
  std::array<CharacterComponentAssemblerHandler, kCharacterComponentStandardTextureCount>
      handlers{};
};

[[nodiscard]] bool AppendCharacterComponentExtraTexture(CharacterComponentRequest &request,
                                                        std::uint8_t component_index,
                                                        int texture_id);

void AssembleCharacterComponentRequestRegion(CharacterModelRefreshState &model,
                                             std::uint8_t region_index,
                                             CharacterComponentRequest &request);

void ResetCharacterComponentRequestForQueue(CharacterComponentRequest &request);

[[nodiscard]] std::shared_ptr<CharacterComponentMipChain> AcquireFreeMipChainForRequestType(
    std::deque<std::shared_ptr<CharacterComponentMipChain>> &general_free_list,
    std::deque<std::shared_ptr<CharacterComponentMipChain>> &special_free_list, int request_type);

[[nodiscard]] std::uint32_t
FillMissingPaletteCharacterComponentMipRegion(CharacterComponentMipChain &chain,
                                              CharacterComponentMipFillRegion destination_region,
                                              std::uint32_t start_level, std::uint8_t mip_count);

[[nodiscard]] std::uint32_t CompositePalettedCharacterComponentMipRegion(
    CharacterComponentMipChain &chain, const CharacterComponentPalettedTexture &source_texture,
    CharacterComponentMipFillRegion destination_region, CharacterComponentMipOrigin source_origin,
    std::uint32_t start_level);

[[nodiscard]] bool CompositeSmallPalettedCharacterComponentMipRegion(
    CharacterComponentMipChain &chain, const CharacterComponentPalettedTexture &source_texture,
    CharacterComponentMipFillRegion destination_region, CharacterComponentMipOrigin source_origin);

[[nodiscard]] bool CompositeExtraComponentTextureMipRegion(
    CharacterComponentMipChain &chain, const CharacterComponentPalettedTexture &source_texture,
    CharacterComponentMipFillRegion destination_region);

[[nodiscard]] bool CompositeStandardComponentTextureMipRegion(
    CharacterComponentMipChain &chain, const CharacterComponentPalettedTexture &source_texture,
    CharacterComponentMipFillRegion destination_region, std::uint32_t atlas_edge);

void CompositeComponentRequest(CharacterComponentRequest &request,
                               CharacterComponentMipChain *special_destination);

enum class CharacterComponentWorkerStep {
  Idle,
  CompletedWithoutComposite,
  RequeuedForMipChain,
  CompletedComposite,
};

struct CharacterComponentWorkerQueues {
  std::deque<std::shared_ptr<CharacterComponentRequest>> pending_requests;
  std::deque<std::shared_ptr<CharacterComponentRequest>> completed_requests;
  std::deque<std::shared_ptr<CharacterComponentMipChain>> general_free_mip_chains;
  std::deque<std::shared_ptr<CharacterComponentMipChain>> special_free_mip_chains;
};

struct CharacterComponentWorkerAllocations {
  std::vector<std::shared_ptr<CharacterComponentRequest>> owned_requests;
  std::deque<std::shared_ptr<CharacterComponentRequest>> reusable_requests;
};

struct CharacterComponentWorkerTeardownSummary {
  std::size_t freed_general_mip_chains = 0;
  std::size_t freed_special_mip_chains = 0;
  std::size_t freed_request_allocations = 0;
};

inline constexpr std::size_t kCharacterComponentBaseSectionCount = 5;

struct CharacterComponentTypeHandleStorageState {
  bool allocated = false;
  std::uint32_t type_index = 0;
  int release_calls = 0;
  std::uint32_t last_released_handle = 0;
  int free_calls = 0;
};

struct CharacterComponentCompositeTextureState {
  bool allocated = false;
  int mip_format = 0;
  std::uint32_t texture_edge = 0;
  int release_calls = 0;
};

struct CharacterComponentBaseSectionLookupBucketState {
  std::vector<std::uintptr_t> entry_arrays;
  int release_calls = 0;
};

struct CharacterComponentBaseSectionLookupRowState {
  std::array<CharacterComponentBaseSectionLookupBucketState, kCharacterComponentBaseSectionCount>
      sections{};
};

struct CharacterComponentRefreshQueueHandlerState {
  bool registered = false;
  std::uint32_t event_id = 7;
  int unregister_calls = 0;
};

struct CharacterComponentBackendShutdownState {
  std::shared_ptr<CharacterModelRefreshState> active_model;
  std::uint32_t active_model_type_handle = 0;
  CharacterComponentTypeHandleStorageState type_handle_storage{};
  CharacterComponentWorkerQueues worker_queues{};
  CharacterComponentWorkerAllocations worker_allocations{};
  std::array<CharacterComponentCompositeTextureState, 3> composite_textures{};
  CharacterComponentBaseSectionTable base_section_table{};
  std::vector<CharacterComponentBaseSectionLookupRowState> base_section_lookup_rows;
  int base_section_lookup_storage_release_calls = 0;
  std::vector<std::uint32_t> facial_hair_style_counts;
  bool facial_hair_style_counts_allocated = false;
  int facial_hair_style_counts_release_calls = 0;
  bool hair_geoset_storage_allocated = false;
  int hair_geoset_storage_release_calls = 0;
  CharacterComponentRefreshQueueHandlerState refresh_queue_handler{};
};

struct CharacterComponentBackendShutdownSummary {
  bool model_shutdown = false;
  bool worker_backend_shutdown = false;
  int worker_shutdown_signal_calls = 0;
  int worker_shutdown_wait_calls = 0;
  std::size_t flushed_pending_requests = 0;
  std::size_t drained_completed_requests = 0;
  CharacterComponentWorkerTeardownSummary worker_teardown{};
  std::size_t released_composite_textures = 0;
  std::size_t released_section_lookup_entry_arrays = 0;
  bool released_section_lookup_storage = false;
  bool released_facial_hair_style_counts = false;
  bool released_hair_geosets = false;
  bool released_type_handle_storage = false;
  bool refresh_queue_handler_unregistered = false;
};

struct CharacterComponentWorkerWakeState {
  int set_event_calls = 0;
};

void RegisterCharacterComponentRequestAllocation(
    CharacterComponentWorkerAllocations &allocations,
    const std::shared_ptr<CharacterComponentRequest> &request);

void QueueCharacterModelComponentRequest(
    CharacterModelRefreshState &model, std::uint32_t component_mask,
    const CharacterComponentAssemblerDispatchTable &dispatch_table,
    CharacterComponentWorkerQueues &queues, CharacterComponentWorkerWakeState &wake_state,
    CharacterComponentWorkerAllocations *allocations = nullptr);

[[nodiscard]] std::size_t
DrainCompletedCharacterComponentRequests(CharacterComponentWorkerQueues &queues,
                                         CharacterComponentWorkerAllocations &allocations,
                                         bool force_all);

[[nodiscard]] CharacterComponentWorkerTeardownSummary
FreeCharacterComponentWorkerMipPoolsAndRequests(CharacterComponentWorkerQueues &queues,
                                                CharacterComponentWorkerAllocations &allocations);

[[nodiscard]] CharacterComponentBackendShutdownSummary
ShutdownCharacterComponentBackend(CharacterComponentBackendRuntimeState &state);

[[nodiscard]] CharacterComponentBackendShutdownSummary
ShutdownCharacterComponentBackendRuntimeState();

[[nodiscard]] CharacterComponentWorkerStep
ProcessOneCharacterComponentRequest(CharacterComponentWorkerQueues &queues,
                                    CharacterComponentMipChain *special_destination);

}
