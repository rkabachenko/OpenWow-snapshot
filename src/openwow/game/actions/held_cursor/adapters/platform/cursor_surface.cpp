
#include "openwow/game/actions/held_cursor/adapters/platform/cursor_surface.h"

#include "openwow/data/texture_cache.h"
#include "openwow/data/blp/blp_decoder.h"
#include "openwow/render/resources/textures/texture_manager.h"
#include "openwow/ui/cursor_gx_system.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/input/input_manager.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <string_view>
#include <unordered_map>

namespace openwow::game {

namespace {

CursorSurface* g_active_cursor_manager = nullptr;

constexpr std::size_t kCursorPixelsWide = 32u;
constexpr std::size_t kCursorPixelsHigh = 32u;
constexpr std::size_t kCursorPixelBytes = detail::kCursor32x32PixelBytes;

constexpr std::size_t kCustomCursorCacheMaximumBytes = 0x104u - 1u;

struct LuaCursorToken {
  const char* name;
  const char* stem;
};

constexpr std::array<LuaCursorToken, 52> kLuaCursorTokens{{
    {"POINT_CURSOR", "Point"},
    {"CAST_CURSOR", "Cast"},
    {"BUY_CURSOR", "Buy"},
    {"ATTACK_CURSOR", "Attack"},
    {"INTERACT_CURSOR", "Interact"},
    {"SPEAK_CURSOR", "Speak"},
    {"INSPECT_CURSOR", "Inspect"},
    {"PICKUP_CURSOR", "Pickup"},
    {"TAXI_CURSOR", "Taxi"},
    {"TRAINER_CURSOR", "Trainer"},
    {"MINE_CURSOR", "Mine"},
    {"SKIN_CURSOR", "Skin"},
    {"GATHER_CURSOR", "GatherHerbs"},
    {"LOCK_CURSOR", "PickLock"},
    {"MAIL_CURSOR", "Mail"},
    {"LOOT_ALL_CURSOR", "LootAll"},
    {"REPAIR_CURSOR", "Repair"},
    {"REPAIRNPC_CURSOR", "RepairNPC"},
    {"ITEM_CURSOR", "Item"},
    {"SKIN_HORDE_CURSOR", "SkinHorde"},
    {"SKIN_ALLIANCE_CURSOR", "SkinAlliance"},
    {"INNKEEPER_CURSOR", "Innkeeper"},
    {"QUEST_CURSOR", "Quest"},
    {"QUEST_REPEATABLE_CURSOR", "QuestRepeatable"},
    {"QUEST_TURNIN_CURSOR", "QuestTurnIn"},
    {"VEHICLE_CURSOR", "vehichleCursor"},
    {"POINT_ERROR_CURSOR", "UnablePoint"},
    {"CAST_ERROR_CURSOR", "UnableCast"},
    {"BUY_ERROR_CURSOR", "UnableBuy"},
    {"ATTACK_ERROR_CURSOR", "UnableAttack"},
    {"INTERACT_ERROR_CURSOR", "UnableInteract"},
    {"SPEAK_ERROR_CURSOR", "UnableSpeak"},
    {"INSPECT_ERROR_CURSOR", "UnableInspect"},
    {"PICKUP_ERROR_CURSOR", "UnablePickup"},
    {"TAXI_ERROR_CURSOR", "UnableTaxi"},
    {"TRAINER_ERROR_CURSOR", "UnableTrainer"},
    {"MINE_ERROR_CURSOR", "UnableMine"},
    {"SKIN_ERROR_CURSOR", "UnableSkin"},
    {"GATHER_ERROR_CURSOR", "UnableGatherHerbs"},
    {"LOCK_ERROR_CURSOR", "UnablePickLock"},
    {"MAIL_ERROR_CURSOR", "UnableMail"},
    {"LOOT_ALL_ERROR_CURSOR", "UnableLootAll"},
    {"REPAIR_ERROR_CURSOR", "UnableRepair"},
    {"REPAIRNPC_ERROR_CURSOR", "UnableRepairNPC"},
    {"ITEM_ERROR_CURSOR", "UnableItem"},
    {"SKIN_HORDE_ERROR_CURSOR", "UnableSkinHorde"},
    {"SKIN_ALLIANCE_ERROR_CURSOR", "UnableSkinAlliance"},
    {"INNKEEPER_ERROR_CURSOR", "UnableInnkeeper"},
    {"QUEST_ERROR_CURSOR", "UnableQuest"},
    {"QUEST_REPEATABLE_ERROR_CURSOR", "UnableQuestRepeatable"},
    {"QUEST_TURNIN_ERROR_CURSOR", "UnableQuestTurnIn"},
    {"VEHICLE_ERROR_CURSOR", "UnablevehichleCursor"},
}};

constexpr std::uint32_t kBlankRetailCursorType = 0u;
constexpr std::uint32_t kCustomRetailCursorType = 53u;

constexpr std::pair<int, int> kStockCursorHotspot{0, 0};

constexpr std::array<const char*, 54> kRetailCursorStems{{
    nullptr,
    "Point",
    "Cast",
    "Buy",
    "Attack",
    "Interact",
    "Speak",
    "Inspect",
    "Pickup",
    "Taxi",
    "Trainer",
    "Mine",
    "Skin",
    "GatherHerbs",
    "PickLock",
    "Mail",
    "LootAll",
    "Repair",
    "RepairNPC",
    "Item",
    "SkinHorde",
    "SkinAlliance",
    "Innkeeper",
    "Quest",
    "QuestRepeatable",
    "QuestTurnIn",
    "vehichleCursor",
    "UnablePoint",
    "UnableCast",
    "UnableBuy",
    "UnableAttack",
    "UnableInteract",
    "UnableSpeak",
    "UnableInspect",
    "UnablePickup",
    "UnableTaxi",
    "UnableTrainer",
    "UnableMine",
    "UnableSkin",
    "UnableGatherHerbs",
    "UnablePickLock",
    "UnableMail",
    "UnableLootAll",
    "UnableRepair",
    "UnableRepairNPC",
    "UnableItem",
    "UnableSkinHorde",
    "UnableSkinAlliance",
    "UnableInnkeeper",
    "UnableQuest",
    "UnableQuestRepeatable",
    "UnableQuestTurnIn",
    "UnablevehichleCursor",
    nullptr,
}};

bool EqualsNoCase(std::string_view lhs, std::string_view rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }

  for (std::size_t i = 0; i < lhs.size(); ++i) {
    const auto left = static_cast<unsigned char>(lhs[i]);
    const auto right = static_cast<unsigned char>(rhs[i]);
    if (std::tolower(left) != std::tolower(right)) {
      return false;
    }
  }

  return true;
}

std::uint8_t AverageChannel(std::uint8_t a,
                            std::uint8_t b,
                            std::uint8_t c,
                            std::uint8_t d) {
  return static_cast<std::uint8_t>(
      (static_cast<unsigned int>(a) + static_cast<unsigned int>(b) +
       static_cast<unsigned int>(c) + static_cast<unsigned int>(d) + 2u) /
      4u);
}

}

bool detail::CopyOrDownsampleCursorRgba32x32(
    const std::span<const std::uint8_t> source_rgba,
    const std::uint32_t width,
    const std::uint32_t height,
    std::array<std::uint8_t, kCursor32x32PixelBytes>* const destination) {
  if (destination == nullptr) {
    return false;
  }

  destination->fill(0);

  if (width == kCursorPixelsWide && height == kCursorPixelsHigh) {
    if (source_rgba.size() < kCursorPixelBytes) {
      return false;
    }

    std::memcpy(destination->data(), source_rgba.data(), kCursorPixelBytes);
    return true;
  }

  constexpr std::size_t kCursor64x64PixelsWide = 64u;
  constexpr std::size_t kCursor64x64PixelsHigh = 64u;
  constexpr std::size_t kCursor64x64PixelBytes =
      kCursor64x64PixelsWide * kCursor64x64PixelsHigh * 4u;
  if (width != kCursor64x64PixelsWide || height != kCursor64x64PixelsHigh) {
    return false;
  }
  if (source_rgba.size() < kCursor64x64PixelBytes) {
    return false;
  }

  for (std::size_t y = 0; y < kCursorPixelsHigh; ++y) {
    for (std::size_t x = 0; x < kCursorPixelsWide; ++x) {
      const auto sample_offset = [=](const std::size_t sx, const std::size_t sy) {
        return ((sy * kCursor64x64PixelsWide) + sx) * 4u;
      };

      const std::size_t s00 = sample_offset(x * 2u, y * 2u);
      const std::size_t s01 = sample_offset(x * 2u + 1u, y * 2u);
      const std::size_t s10 = sample_offset(x * 2u, y * 2u + 1u);
      const std::size_t s11 = sample_offset(x * 2u + 1u, y * 2u + 1u);
      const std::size_t dst = ((y * kCursorPixelsWide) + x) * 4u;

      (*destination)[dst + 0u] = AverageChannel(
          source_rgba[s00 + 0u], source_rgba[s01 + 0u], source_rgba[s10 + 0u], source_rgba[s11 + 0u]);
      (*destination)[dst + 1u] = AverageChannel(
          source_rgba[s00 + 1u], source_rgba[s01 + 1u], source_rgba[s10 + 1u], source_rgba[s11 + 1u]);
      (*destination)[dst + 2u] = AverageChannel(
          source_rgba[s00 + 2u], source_rgba[s01 + 2u], source_rgba[s10 + 2u], source_rgba[s11 + 2u]);
      (*destination)[dst + 3u] = 0xFFu;
    }
  }

  return true;
}

void detail::ComposeHeldCursorRgba32x32(
    const std::array<std::uint8_t, kCursor32x32PixelBytes>& item_cursor,
    const std::array<std::uint8_t, kCursor32x32PixelBytes>& runtime_image,
    std::array<std::uint8_t, kCursor32x32PixelBytes>* const destination) {
  if (destination == nullptr) {
    return;
  }

  *destination = runtime_image;
  for (std::size_t pixel = 0; pixel < kCursorPixelsWide * kCursorPixelsHigh;
       ++pixel) {
    const std::size_t offset = pixel * 4u;
    if (item_cursor[offset + 3u] == 0u) {
      continue;
    }
    std::memcpy(destination->data() + offset,
                item_cursor.data() + offset, 4u);
  }
}

std::uint32_t FindRetailCursorTypeByStem(const std::string_view stem) {
  if (stem.empty()) {
    return 0u;
  }

  for (std::uint32_t retail_type = 0u; retail_type < kRetailCursorStems.size(); ++retail_type) {
    const char* const candidate = kRetailCursorStems[retail_type];
    if (candidate != nullptr && EqualsNoCase(candidate, stem)) {
      return retail_type;
    }
  }

  return 0u;
}

RetailCursorRequest ResolveRetailCursorRequestFromStem(const std::string_view stem,
                                                       const bool unavailable) {
  if (stem.empty()) {
    return {};
  }

  std::string resolved_stem;
  if (unavailable) {
    resolved_stem = "Unable";
    resolved_stem.append(stem);
  } else {
    resolved_stem.assign(stem);
  }

  if (const std::uint32_t retail_type = FindRetailCursorTypeByStem(resolved_stem);
      retail_type != 0u) {
    return {.retail_type = retail_type};
  }

  RetailCursorRequest request;
  request.retail_type = kCustomRetailCursorType;
  request.custom_texture_path = "Interface\\Cursor\\";
  request.custom_texture_path.append(resolved_stem);
  request.custom_texture_path.append(".blp");
  return request;
}

std::uint32_t ResolveQuestGiverRetailCursorType(const QuestGiverStatus status,
                                                const bool interaction_blocked) {
  switch (status) {
    case QuestGiverStatus::kLowLevelAvailable:
    case QuestGiverStatus::kLowLevelRewardRep:
    case QuestGiverStatus::kLowLevelAvailableRep:
    case QuestGiverStatus::kIncomplete:
    case QuestGiverStatus::kAvailableRep:
    case QuestGiverStatus::kAvailable:
      return interaction_blocked ? 49u : 23u;
    case QuestGiverStatus::kRewardRep:
      return interaction_blocked ? 50u : 24u;
    case QuestGiverStatus::kReward2:
    case QuestGiverStatus::kReward:
      return interaction_blocked ? 51u : 25u;
    default:
      return 0u;
  }
}

bool CursorSurface::Initialize(SDL_Window* window) {
  if (!window) {
    return false;
  }

  window_ = window;
  base_cursor_ = CursorType::kDefault;
  current_ = CursorType::kDefault;
  base_retail_type_ = 1u;
  current_retail_type_ = 1u;
  active_cursor_path_ = CursorTypeToTexturePath(CursorType::kDefault);
  active_hotspot_ = CursorTypeToHotspot(CursorType::kDefault);
  transient_override_active_ = false;
  custom_cursor_active_ = false;

  runtime_item_cursor_prepared_ = false;

  cached_custom_cursor_request_.clear();
  custom_cursor_rgba_.fill(0);
  custom_cursor_texture_dirty_ = true;

  SDL_Cursor* stale_builtin_cursor_handle = DetachPublishedBuiltInCursor();
  SDL_Cursor* stale_custom_cursor_handle = custom_cursor_handle_;
  custom_cursor_handle_ = nullptr;
  ResetRuntimeCursorTextureState();
  visible_ = true;
  RefreshPresentationMode();
  FreeCursorHandle(stale_builtin_cursor_handle);
  FreeCursorHandle(stale_custom_cursor_handle);

  SetActiveCursorSurface(this);

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo, "CursorSurface: initialized");
  return true;
}

void CursorSurface::Shutdown() {

  SDL_Cursor* stale_builtin_cursor_handle = DetachPublishedBuiltInCursor();
  SDL_Cursor* stale_custom_cursor_handle = custom_cursor_handle_;
  custom_cursor_handle_ = nullptr;
  SDL_Cursor* stale_runtime_cursor_handle = runtime_cursor_handle_;
  runtime_cursor_handle_ = nullptr;

  runtime_cursor_enabled_ = false;
  ResetRuntimeCursorTextureState();
  software_cursor_texture_leases_.clear();
  DestroyCustomCursorResources();
  software_renderer_.Shutdown();
  base_cursor_ = CursorType::kDefault;
  current_ = CursorType::kDefault;
  base_retail_type_ = 1u;
  current_retail_type_ = 1u;
  active_cursor_path_ = CursorTypeToTexturePath(CursorType::kDefault);
  active_hotspot_ = CursorTypeToHotspot(CursorType::kDefault);
  transient_override_active_ = false;
  custom_cursor_active_ = false;
  cached_custom_cursor_request_.clear();
  custom_cursor_rgba_.fill(0);
  visible_ = true;
  RefreshPresentationMode();
  FreeCursorHandle(stale_builtin_cursor_handle);
  FreeCursorHandle(stale_custom_cursor_handle);
  FreeCursorHandle(stale_runtime_cursor_handle);
  if (GetActiveCursorSurface() == this) {
    SetActiveCursorSurface(nullptr);
  }
  window_ = nullptr;
}

void CursorSurface::ReleaseRendererDeviceResources() {
  software_renderer_.Shutdown();
  runtime_cursor_texture_dirty_ = true;
}

void CursorSurface::SetCursor(CursorType type) {
  if (static_cast<std::size_t>(type) >= static_cast<std::size_t>(CursorType::kCount)) {
    type = CursorType::kDefault;
  }

  current_ = type;
  current_retail_type_ = FindRetailCursorTypeByStem(CursorTypeToBlpName(type));
  if (current_retail_type_ == 0u && type != CursorType::kDefault) {
    current_retail_type_ = kCustomRetailCursorType;
  }
  transient_override_active_ =
      !EqualsNoCase(CursorTypeToTexturePath(type), CursorTypeToTexturePath(base_cursor_));
  ActivateBuiltInCursor(CursorTypeToTexturePath(type), CursorTypeToHotspot(type));
}

void CursorSurface::SetImmediateCursorType(const std::uint32_t retail_type) {
  if (retail_type == kCustomRetailCursorType || retail_type == current_retail_type_) {
    return;
  }

  std::string texture_path;
  std::pair<int, int> hotspot{0, 0};
  bool uses_blank_pixels = false;
  if (!TryResolveRetailCursorType(retail_type, &texture_path, &hotspot, &uses_blank_pixels)) {
    return;
  }

  if (uses_blank_pixels) {
    ActivateBlankCursor(CursorType::kDefault, true);
    return;
  }

  current_ = CursorType::kDefault;
  current_retail_type_ = retail_type;
  transient_override_active_ =
      !EqualsNoCase(texture_path, CursorTypeToTexturePath(base_cursor_));
  ActivateBuiltInCursor(texture_path, hotspot);
}

void CursorSurface::SetBaseCursor(CursorType type) {
  if (static_cast<std::size_t>(type) >= static_cast<std::size_t>(CursorType::kCount)) {
    type = CursorType::kDefault;
  }

  base_cursor_ = type;
  base_retail_type_ = FindRetailCursorTypeByStem(CursorTypeToBlpName(type));
  if (base_retail_type_ == 0u && type != CursorType::kDefault) {
    base_retail_type_ = kCustomRetailCursorType;
  }
}

void CursorSurface::SetBaseRetailCursorType(const std::uint32_t retail_type) {
  base_retail_type_ = retail_type;
}

void CursorSurface::ResetCursor() {
  RestoreBaseCursor();
}

void CursorSurface::RestoreBaseCursor() {
  if (base_retail_type_ == kCustomRetailCursorType ||
      base_retail_type_ == current_retail_type_) {
    return;
  }

  std::string texture_path;
  std::pair<int, int> hotspot{0, 0};
  bool uses_blank_pixels = false;
  if (!TryResolveRetailCursorType(base_retail_type_, &texture_path, &hotspot,
                                  &uses_blank_pixels)) {
    return;
  }

  if (uses_blank_pixels) {
    ActivateBlankCursor(base_cursor_, false);
  } else {
    transient_override_active_ = false;
    current_ = base_cursor_;
    current_retail_type_ = base_retail_type_;
    ActivateBuiltInCursor(texture_path, hotspot);
  }
}

void CursorSurface::SetFileReader(FileReader reader) {
  file_reader_ = std::move(reader);

  SDL_Cursor* stale_builtin_cursor_handle = DetachPublishedBuiltInCursor();
  runtime_item_cursor_prepared_ = false;
  PrepareRuntimeItemCursor();
  RefreshPresentationMode();
  FreeCursorHandle(stale_builtin_cursor_handle);
}

bool CursorSurface::SetRuntimeCursorTexture(
    const std::string_view texture_path) {
  runtime_requested_texture_path_.assign(texture_path);
  if (!runtime_cursor_enabled_) {
    CaptureRuntimeCursorPresentation();
    runtime_cursor_enabled_ = true;
  }
  PrepareRuntimeItemCursor();

  std::array<std::uint8_t, detail::kCursor32x32PixelBytes> decoded{};
  bool image_loaded = false;
  const bool converted =
      DecodeCursorPixels(texture_path, &decoded, &image_loaded);
  if (!image_loaded) {

    runtime_cursor_scratch_rgba_.fill(0);
    PublishRuntimeCursorScratch();
    RefreshPresentationMode();
    return false;
  }

  runtime_cursor_scratch_rgba_ = decoded;
  PublishRuntimeCursorScratch();
  RefreshPresentationMode();
  return converted;
}

void CursorSurface::ClearRuntimeCursorTexture() {
  if (!runtime_cursor_enabled_) {
    return;
  }

  runtime_cursor_enabled_ = false;
  runtime_cursor_pixels_valid_ = false;
  runtime_requested_texture_path_.clear();
  runtime_visible_cursor_path_.clear();
  runtime_visible_hotspot_ = {0, 0};
  FreeCursorHandle(runtime_cursor_handle_);
  runtime_cursor_texture_dirty_ = true;
  custom_cursor_texture_dirty_ = true;
  software_renderer_.ResetCustomTexture();
  RefreshPresentationMode();
}

void CursorSurface::ResetRuntimeCursorTextureState() {
  const bool was_enabled = runtime_cursor_enabled_;
  runtime_cursor_enabled_ = false;
  runtime_cursor_pixels_valid_ = false;
  runtime_requested_texture_path_.clear();
  runtime_visible_cursor_path_.clear();
  runtime_visible_hotspot_ = {0, 0};
  runtime_cursor_scratch_rgba_.fill(0);
  runtime_cursor_rgba_.fill(0);

  const bool released_live_handle = runtime_cursor_handle_ != nullptr;
  FreeCursorHandle(runtime_cursor_handle_);
  runtime_cursor_texture_dirty_ = true;
  custom_cursor_texture_dirty_ = true;
  software_renderer_.ResetCustomTexture();
  if (was_enabled || released_live_handle) {
    RefreshPresentationMode();
  }
}

bool CursorSurface::SetCursorFromLua(const std::string& cursor_name) {
  std::string texture_path;
  std::pair<int, int> hotspot{0, 0};
  if (TryResolveLuaCursorToken(cursor_name, &texture_path, &hotspot)) {
    current_ = LuaNameToCursorType(cursor_name);
    const auto slash = texture_path.find_last_of("/\\");
    const auto dot = texture_path.rfind('.');
    const std::string_view stem(
        texture_path.data() + (slash == std::string::npos ? 0u : slash + 1u),
        dot == std::string::npos
            ? texture_path.size() - (slash == std::string::npos ? 0u : slash + 1u)
            : dot - (slash == std::string::npos ? 0u : slash + 1u));
    current_retail_type_ = FindRetailCursorTypeByStem(stem);
    transient_override_active_ =
        custom_cursor_active_ || !EqualsNoCase(texture_path, CursorTypeToTexturePath(base_cursor_));
    ActivateBuiltInCursor(texture_path, hotspot);
    return true;
  }

  const bool same_request = EqualsNoCase(cached_custom_cursor_request_, cursor_name);
  bool result = same_request;
  SDL_Cursor* stale_custom_cursor_handle = nullptr;
  if (!same_request) {
    result = LoadCustomCursorPixels(cursor_name, &stale_custom_cursor_handle);
    cached_custom_cursor_request_.assign(
        cursor_name.data(),
        std::min(cursor_name.size(), kCustomCursorCacheMaximumBytes));
  }

  transient_override_active_ = true;
  current_retail_type_ = kCustomRetailCursorType;
  if (!custom_cursor_active_ || !same_request) {
    ActivateCustomCursor();
  }

  FreeCursorHandle(stale_custom_cursor_handle);
  return result;
}

void CursorSurface::SetCursorFromRetailStem(const std::string_view stem,
                                            const bool unavailable) {
  const RetailCursorRequest request =
      ResolveRetailCursorRequestFromStem(stem, unavailable);
  if (request.retail_type == 0u) {
    RestoreBaseCursor();
    return;
  }
  if (request.UsesCustomTexture()) {
    SetCursorFromLua(request.custom_texture_path);
    return;
  }
  SetImmediateCursorType(request.retail_type);
}

CursorType CursorSurface::LuaNameToCursorType(const std::string& name) {
  static const std::unordered_map<std::string, CursorType> kLuaMap = {
      {"ATTACK_CURSOR", CursorType::kAttack},
      {"CAST_CURSOR", CursorType::kCast},
      {"ITEM_CURSOR", CursorType::kItem},
      {"MAIL_CURSOR", CursorType::kMail},
      {"MINE_CURSOR", CursorType::kMine},
      {"SKIN_CURSOR", CursorType::kSkin},
      {"REPAIR_CURSOR", CursorType::kRepair},
      {"SPEAK_CURSOR", CursorType::kSpeak},
      {"QUEST_CURSOR", CursorType::kQuest},
      {"TRAINER_CURSOR", CursorType::kTrainer},
      {"TAXI_CURSOR", CursorType::kTaxi},
      {"INNKEEPER_CURSOR", CursorType::kInnkeeper},
      {"GATHER_CURSOR", CursorType::kGather},
      {"INSPECT_CURSOR", CursorType::kInspect},
      {"POINT_CURSOR", CursorType::kDefault},
      {"BUY_CURSOR", CursorType::kBuy},
      {"ATTACK_ERROR_CURSOR", CursorType::kUnableAttack},
  };

  for (const auto& [token, type] : kLuaMap) {
    if (EqualsNoCase(token, name)) {
      return type;
    }
  }
  return CursorType::kDefault;
}

bool CursorSurface::TryResolveLuaCursorToken(std::string_view name,
                                             std::string* texture_path,
                                             std::pair<int, int>* hotspot) {
  for (const auto& token : kLuaCursorTokens) {
    if (!EqualsNoCase(token.name, name)) {
      continue;
    }

    if (texture_path) {
      *texture_path = "Interface/Cursor/";
      texture_path->append(token.stem);
      texture_path->append(".blp");
    }
    if (hotspot) {
      *hotspot = kStockCursorHotspot;
    }
    return true;
  }

  return false;
}

bool CursorSurface::TryResolveRetailCursorType(const std::uint32_t retail_type,
                                               std::string* texture_path,
                                               std::pair<int, int>* hotspot,
                                               bool* uses_blank_pixels) {
  if (uses_blank_pixels) {
    *uses_blank_pixels = retail_type == kBlankRetailCursorType;
  }
  if (retail_type == kBlankRetailCursorType) {
    if (hotspot) {
      *hotspot = {0, 0};
    }
    if (texture_path) {
      texture_path->clear();
    }
    return true;
  }
  if (retail_type >= kRetailCursorStems.size()) {
    return false;
  }

  const char* stem = kRetailCursorStems[retail_type];
  if (!stem) {
    return false;
  }

  if (texture_path) {
    *texture_path = "Interface/Cursor/";
    texture_path->append(stem);
    texture_path->append(".blp");
  }
  if (hotspot) {
    *hotspot = kStockCursorHotspot;
  }
  return true;
}

std::string CursorSurface::CursorTypeToBlpName(CursorType type) {

  switch (type) {
    case CursorType::kDefault:         return "Point";
    case CursorType::kAttack:          return "Attack";
    case CursorType::kCast:            return "Cast";
    case CursorType::kItem:            return "Item";
    case CursorType::kLoot:            return "LootAll";
    case CursorType::kMail:            return "Mail";
    case CursorType::kMine:            return "Mine";
    case CursorType::kSkin:            return "Skin";
    case CursorType::kRepair:          return "Repair";
    case CursorType::kSpeak:           return "Speak";
    case CursorType::kQuest:           return "Quest";
    case CursorType::kQuestComplete:   return "QuestTurnIn";
    case CursorType::kTrainer:         return "Trainer";
    case CursorType::kTaxi:            return "Taxi";
    case CursorType::kInnkeeper:       return "Innkeeper";
    case CursorType::kGather:          return "GatherHerbs";
    case CursorType::kInspect:         return "Inspect";
    case CursorType::kPickLock:        return "PickLock";
    case CursorType::kUnableAttack:    return "UnableAttack";
    case CursorType::kBuy:             return "Buy";
    case CursorType::kPickUp:          return "Pickup";
    case CursorType::kInteract:        return "Interact";
    case CursorType::kQuestRepeatable: return "QuestRepeatable";
    case CursorType::kCount:           break;
  }
  return "Point";
}

std::string CursorSurface::CursorTypeToTexturePath(CursorType type) {
  return "Interface/Cursor/" + CursorTypeToBlpName(type) + ".blp";
}

std::pair<int, int> CursorSurface::CursorTypeToHotspot(CursorType ) {
  return kStockCursorHotspot;
}

bool CursorSurface::WantsHardwareCursor() const {
  const auto& cvars = openwow::ui::game::CVarSystem::Instance();
  return !cvars.Exists("gxCursor") || cvars.GetCVarBool("gxCursor");
}

bool CursorSurface::ShouldRenderSoftwareCursor() const {
  return visible_ && !WantsHardwareCursor();
}

std::string CursorSurface::GetActiveCursorTexturePath() const {
  return runtime_cursor_enabled_ ? runtime_visible_cursor_path_
                                 : active_cursor_path_;
}

std::pair<int, int> CursorSurface::GetActiveCursorHotspot() const {
  return runtime_cursor_enabled_ ? runtime_visible_hotspot_ : active_hotspot_;
}

void CursorSurface::PreloadCursorTextures() {
  const auto retain = [this](std::string path) {
    const std::uint32_t row_hash =
        openwow::data::HashTextureCachePath(path);
    auto lease = texture_manager_.AcquireTexture(path);
    if (lease) {
      software_cursor_texture_leases_.insert_or_assign(
          row_hash, std::move(lease));
    }
  };
  for (std::size_t retail_type = 1u;
       retail_type < kCustomRetailCursorType; ++retail_type) {
    const char* const stem = kRetailCursorStems[retail_type];
    if (stem != nullptr) {
      retain(std::string("Interface/Cursor/") + stem + ".blp");
    }
  }
}

void CursorSurface::RenderOverlay(std::uint8_t view_id, float screen_w, float screen_h) {
  RefreshPresentationMode();
  if (!ShouldRenderSoftwareCursor() || !window_ || screen_w <= 0.0f || screen_h <= 0.0f) {
    return;
  }

  const auto [mouse_x, mouse_y] =
      openwow::input::InputManager::Get().GetMousePosition();
  if (mouse_x < 0 || mouse_y < 0 || static_cast<float>(mouse_x) >= screen_w ||
      static_cast<float>(mouse_y) >= screen_h) {
    return;
  }

  bool rendered = false;
  if (runtime_cursor_enabled_ && runtime_cursor_pixels_valid_) {
    rendered = software_renderer_.RenderCustomPixels(
        view_id,
        screen_w,
        screen_h,
        mouse_x,
        mouse_y,
        runtime_visible_hotspot_.first,
        runtime_visible_hotspot_.second,
        runtime_cursor_rgba_,
        runtime_cursor_texture_dirty_);
    runtime_cursor_texture_dirty_ = false;
  } else if (runtime_cursor_enabled_) {
    rendered = software_renderer_.RenderTexturePath(
        view_id,
        screen_w,
        screen_h,
        mouse_x,
        mouse_y,
        runtime_visible_hotspot_.first,
        runtime_visible_hotspot_.second,
        runtime_visible_cursor_path_);
  } else if (custom_cursor_active_) {
    rendered = software_renderer_.RenderCustomPixels(
        view_id,
        screen_w,
        screen_h,
        mouse_x,
        mouse_y,
        active_hotspot_.first,
        active_hotspot_.second,
        custom_cursor_rgba_,
        custom_cursor_texture_dirty_);
    custom_cursor_texture_dirty_ = false;
  } else {
    rendered = software_renderer_.RenderTexturePath(
        view_id,
        screen_w,
        screen_h,
        mouse_x,
        mouse_y,
        active_hotspot_.first,
        active_hotspot_.second,
        active_cursor_path_);
  }
  (void)rendered;
}

void CursorSurface::ApplyHardwareCursor() {
  if (runtime_cursor_enabled_ && runtime_cursor_pixels_valid_) {
    if (runtime_cursor_handle_ == nullptr) {
      runtime_cursor_handle_ = CreateCursorFromPixels(
          runtime_cursor_rgba_, runtime_visible_hotspot_.first,
          runtime_visible_hotspot_.second);
    }
    if (runtime_cursor_handle_ != nullptr) {
      SDL_SetCursor(runtime_cursor_handle_);
    }
    return;
  }

  if (!runtime_cursor_enabled_ && custom_cursor_active_) {
    if (!custom_cursor_handle_) {
      custom_cursor_handle_ =
          CreateCursorFromCustomPixels(active_hotspot_.first, active_hotspot_.second);
    }
    if (custom_cursor_handle_) {
      SDL_SetCursor(custom_cursor_handle_);
    }
    return;
  }

  const std::string& presented_path =
      runtime_cursor_enabled_ ? runtime_visible_cursor_path_
                              : active_cursor_path_;
  const auto presented_hotspot =
      runtime_cursor_enabled_ ? runtime_visible_hotspot_ : active_hotspot_;
  if (presented_path.empty()) {
    return;
  }

  if (published_builtin_cursor_.handle != nullptr &&
      published_builtin_cursor_.hotspot == presented_hotspot &&
      EqualsNoCase(published_builtin_cursor_.texture_path, presented_path)) {
    SDL_SetCursor(published_builtin_cursor_.handle);
    return;
  }

  SDL_Cursor* fresh_handle = CreateCursorFromBLP(
      presented_path, presented_hotspot.first, presented_hotspot.second);
  if (fresh_handle == nullptr) {

    std::array<std::uint8_t, detail::kCursor32x32PixelBytes> transparent_rgba{};
    fresh_handle = CreateCursorFromPixels(
        transparent_rgba, presented_hotspot.first, presented_hotspot.second);
  }
  if (fresh_handle == nullptr) {
    return;
  }

  SDL_SetCursor(fresh_handle);
  FreeCursorHandle(published_builtin_cursor_.handle);
  published_builtin_cursor_.handle = fresh_handle;
  published_builtin_cursor_.texture_path = presented_path;
  published_builtin_cursor_.hotspot = presented_hotspot;
}

void CursorSurface::FreeCursorHandle(SDL_Cursor*& handle) {
  if (handle == nullptr) {
    return;
  }

  SDL_FreeCursor(handle);
  handle = nullptr;
}

SDL_Cursor* CursorSurface::DetachPublishedBuiltInCursor() {
  SDL_Cursor* stale_handle = published_builtin_cursor_.handle;
  published_builtin_cursor_ = {};
  return stale_handle;
}

void CursorSurface::ActivateBuiltInCursor(const std::string& texture_path,
                                          std::pair<int, int> hotspot) {
  const bool changed = custom_cursor_active_ || !EqualsNoCase(active_cursor_path_, texture_path) ||
                       active_hotspot_ != hotspot;
  custom_cursor_active_ = false;
  active_cursor_path_ = texture_path;
  active_hotspot_ = hotspot;
  if (changed && runtime_cursor_enabled_) {
    PublishRuntimeCursorScratch();
  }
  RefreshPresentationMode();
  if (changed) {
    openwow::ui::game::ScriptEventDispatch::Get().FireCursorUpdate();
  }
}

void CursorSurface::ActivateBlankCursor(const CursorType semantic_type,
                                        const bool transient_override) {
  const bool changed = !custom_cursor_active_ || !active_cursor_path_.empty() ||
                       active_hotspot_ != std::pair<int, int>{0, 0};
  custom_cursor_rgba_.fill(0);
  custom_cursor_texture_dirty_ = true;

  SDL_Cursor* stale_custom_cursor_handle = custom_cursor_handle_;
  custom_cursor_handle_ = nullptr;
  current_ = semantic_type;
  current_retail_type_ = kBlankRetailCursorType;
  transient_override_active_ = transient_override;
  custom_cursor_active_ = true;
  active_cursor_path_.clear();
  active_hotspot_ = {0, 0};
  if (changed && runtime_cursor_enabled_) {
    PublishRuntimeCursorScratch();
  }
  RefreshPresentationMode();
  FreeCursorHandle(stale_custom_cursor_handle);
  if (changed) {
    openwow::ui::game::ScriptEventDispatch::Get().FireCursorUpdate();
  }
}

bool CursorSurface::LoadCustomCursorPixels(const std::string& cursor_path,
                                           SDL_Cursor** const stale_handle) {
  custom_cursor_rgba_.fill(0);
  custom_cursor_texture_dirty_ = true;

  *stale_handle = custom_cursor_handle_;
  custom_cursor_handle_ = nullptr;

  bool image_loaded = false;
  return DecodeCursorPixels(cursor_path, &custom_cursor_rgba_, &image_loaded);
}

bool CursorSurface::DecodeCursorPixels(
    const std::string_view cursor_path,
    std::array<std::uint8_t, detail::kCursor32x32PixelBytes>* const destination,
    bool* const image_loaded) const {
  if (image_loaded != nullptr) {
    *image_loaded = false;
  }
  if (destination == nullptr || !file_reader_ || cursor_path.empty()) {
    return false;
  }

  auto bytes_opt = file_reader_(std::string(cursor_path));
  if (!bytes_opt || bytes_opt->empty()) {
    return false;
  }

  const auto image = openwow::data::blp::DecodeBlp(*bytes_opt);
  if (!image.ok || image.pixels_rgba.size() != image.width * image.height * 4u) {
    return false;
  }

  if (image_loaded != nullptr) {
    *image_loaded = true;
  }
  return detail::CopyOrDownsampleCursorRgba32x32(
      image.pixels_rgba, image.width, image.height, destination);
}

void CursorSurface::PrepareRuntimeItemCursor() {
  if (runtime_item_cursor_prepared_) {
    return;
  }

  runtime_item_cursor_rgba_.fill(0);
  bool image_loaded = false;
  (void)DecodeCursorPixels("Interface/Cursor/Item.blp",
                           &runtime_item_cursor_rgba_, &image_loaded);

  runtime_item_cursor_prepared_ = image_loaded;
}

void CursorSurface::CaptureRuntimeCursorPresentation() {
  runtime_visible_cursor_path_ = active_cursor_path_;
  runtime_visible_hotspot_ = active_hotspot_;
  runtime_cursor_pixels_valid_ = false;

  if (custom_cursor_active_) {
    runtime_cursor_rgba_ = custom_cursor_rgba_;
    runtime_cursor_pixels_valid_ = true;
  }

  FreeCursorHandle(runtime_cursor_handle_);
  runtime_cursor_texture_dirty_ = true;
}

void CursorSurface::PublishRuntimeCursorScratch() {
  detail::ComposeHeldCursorRgba32x32(
      runtime_item_cursor_rgba_, runtime_cursor_scratch_rgba_,
      &runtime_cursor_rgba_);
  runtime_cursor_pixels_valid_ = true;
  runtime_visible_cursor_path_ = runtime_requested_texture_path_;
  runtime_visible_hotspot_ = {0, 0};
  FreeCursorHandle(runtime_cursor_handle_);
  runtime_cursor_texture_dirty_ = true;
}

void CursorSurface::ActivateCustomCursor() {
  const bool changed =
      !custom_cursor_active_ || !EqualsNoCase(active_cursor_path_, cached_custom_cursor_request_);
  custom_cursor_active_ = true;
  active_cursor_path_ = cached_custom_cursor_request_;
  active_hotspot_ = {0, 0};
  if (changed && runtime_cursor_enabled_) {
    PublishRuntimeCursorScratch();
  }
  RefreshPresentationMode();
  if (changed) {
    openwow::ui::game::ScriptEventDispatch::Get().FireCursorUpdate();
  }
}

void CursorSurface::DestroyCustomCursorResources() {
  FreeCursorHandle(custom_cursor_handle_);
  software_renderer_.ResetCustomTexture();
  custom_cursor_texture_dirty_ = true;
}

void CursorSurface::RefreshPresentationMode() {
  if (!window_) {
    return;
  }

  if (!visible_ || ShouldRenderSoftwareCursor()) {
    SDL_ShowCursor(SDL_DISABLE);
    return;
  }

  SDL_ShowCursor(SDL_ENABLE);
  ApplyHardwareCursor();
}

void CursorSurface::ReassertPresentation() {
  if (window_ == nullptr) {
    return;
  }

  RefreshPresentationMode();

  if (!visible_ || ShouldRenderSoftwareCursor()) {

    return;
  }

  SDL_SetCursor(nullptr);
}

SDL_Cursor* CursorSurface::CreateCursorFromBLP(
    const std::string& blp_path, int hotspot_x, int hotspot_y) {
  if (!file_reader_) {
    return nullptr;
  }

  auto bytes_opt = file_reader_(blp_path);
  if (!bytes_opt || bytes_opt->empty()) {
    return nullptr;
  }

  auto image = openwow::data::blp::DecodeBlp(*bytes_opt);
  if (!image.ok || image.width == 0 || image.height == 0 ||
      image.pixels_rgba.size() != image.width * image.height * 4u) {
    return nullptr;
  }

  SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(
      image.pixels_rgba.data(),
      static_cast<int>(image.width),
      static_cast<int>(image.height),
      32,
      static_cast<int>(image.width * 4),
      0x000000FF,
      0x0000FF00,
      0x00FF0000,
      0xFF000000);
  if (!surface) {
    return nullptr;
  }

  SDL_Cursor* cursor = SDL_CreateColorCursor(surface, hotspot_x, hotspot_y);
  SDL_FreeSurface(surface);
  return cursor;
}

SDL_Cursor* CursorSurface::CreateCursorFromCustomPixels(int hotspot_x, int hotspot_y) {
  return CreateCursorFromPixels(custom_cursor_rgba_, hotspot_x, hotspot_y);
}

SDL_Cursor* CursorSurface::CreateCursorFromPixels(
    std::array<std::uint8_t, detail::kCursor32x32PixelBytes>& pixels,
    const int hotspot_x, const int hotspot_y) {
  SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(
      pixels.data(),
      static_cast<int>(kCursorPixelsWide),
      static_cast<int>(kCursorPixelsHigh),
      32,
      static_cast<int>(kCursorPixelsWide * 4u),
      0x000000FF,
      0x0000FF00,
      0x00FF0000,
      0xFF000000);
  if (!surface) {
    return nullptr;
  }

  SDL_Cursor* cursor = SDL_CreateColorCursor(surface, hotspot_x, hotspot_y);
  SDL_FreeSurface(surface);
  return cursor;
}

void CursorSurface::ShowCursor(bool show) {

  if (visible_ == show) {
    return;
  }
  visible_ = show;
  RefreshPresentationMode();
}

void SetActiveCursorSurface(CursorSurface* manager) {
  g_active_cursor_manager = manager;
  if (manager != nullptr) {
    openwow::ui::SyncRuntimeCursorPresentation();
  }
}

CursorSurface* GetActiveCursorSurface() {
  return g_active_cursor_manager;
}

}
