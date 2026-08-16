
#include "battlenet_login.h"

#include "openwow/game/battlenet_api.h"
#include "openwow/game/battlenet_events.h"
#include "openwow/game/battlenet_utf8.h"
#include "openwow/core/console.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/core/storm_error.h"
#include "openwow/core/storm_string.h"
#include "openwow/net/serialization/cdatastore_ops.h"
#include "openwow/net/net_client.h"
#include "openwow/net/transport/wow_connection.h"
#include "openwow/net/wotlk/protocol/auth_protocol.h"
#include "openwow/data/archive_system.h"
#include "openwow/data/startup_filesystem_state.h"
#include "openwow/data/streaming_init.h"
#include "openwow/platform/process/os_platform.h"
#include "openwow/platform/adapters/win32/win32_compat.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/xml/xml_tree.h"
#include "openwow/vfs/sfile_core.h"

#include <cassert>
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string_view>
#include <utility>

namespace openwow::game {

namespace {

std::uint32_t StringToFourCC(const char *str) {
  std::uint32_t result = 0;
  if (str) {
    for (const char *p = str; *p; ++p) {
      result = (result << 8) | static_cast<std::uint8_t>(*p);
    }
  }
  return result;
}

void FourCCToString(std::uint32_t fourcc, char *out, std::size_t out_size) {
  char *dst = out;
  const char *end = out + out_size - 1;
  for (int shift = 24; shift >= 0 && dst < end; shift -= 8) {
    const char c = static_cast<char>((fourcc >> shift) & 0xFF);
    if (c != '\0') {
      *dst++ = c;
    }
  }
  *dst = '\0';
}

constexpr const char *kLocaleNameByIndex[] = {
    "enUS", "koKR", "frFR", "deDE", "zhCN", "zhTW", "esES", "esMX", "ruRU",
};
constexpr std::size_t kLocaleNameCount =
    sizeof(kLocaleNameByIndex) / sizeof(kLocaleNameByIndex[0]);

bool s_platform_override_pending = false;

char s_platform_override_code[8] = {};

const char *FindXmlAttributeValue(const openwow::ui::xml::CXMLNode &node, const char *name) {
  for (const auto &attribute : node.attributes.entries) {
    if (openwow::core::SStrCmpNoCase(attribute.name.c_str(), name, 0x7FFFFFFFu) == 0) {
      return attribute.value.c_str();
    }
  }
  return nullptr;
}

std::uint32_t ParseStormDecimal(const char *value) {
  if (!value) {
    openwow::core::SErrSetLastError(87);
    return 0;
  }

  const char *cursor = value;
  bool negative = false;
  if (*cursor == '-') {
    negative = true;
    ++cursor;
  }

  const unsigned int first_digit = static_cast<unsigned int>(*cursor - '0');
  if (first_digit >= 10u) {
    return 0;
  }

  std::uint32_t result = first_digit;
  ++cursor;
  for (unsigned int digit = static_cast<unsigned int>(*cursor - '0'); digit < 10u;
       digit = static_cast<unsigned int>(*cursor - '0')) {
    result = result * 10u + digit;
    ++cursor;
  }

  if (negative) {
    return static_cast<std::uint32_t>(-static_cast<std::int32_t>(result));
  }
  return result;
}

GameAccountEntry NormalizeGameAccountEntry(const GameAccountEntry &entry) {
  GameAccountEntry normalized{};
  normalized.id = entry.id;
  CopyBoundedCStringPrefix(normalized.name, sizeof(normalized.name),
                           std::string_view(entry.name, sizeof(entry.name)),
                           sizeof(normalized.name) - 1);
  return normalized;
}

std::array<char, kBattlenetPatchInstructionSlotBytes>
NormalizePatchInstructionString(const std::string_view instruction) {
  std::array<char, kBattlenetPatchInstructionSlotBytes> normalized{};
  CopyBoundedCStringPrefix(normalized.data(), normalized.size(), instruction,
                           normalized.size() - 1);
  return normalized;
}

bool IsRedirectMonolithicInstruction(const char *instruction) {
  constexpr std::string_view kRedirectMonolithic = "redirect monolithic";
  return instruction != nullptr
         && openwow::core::SStrCmpI(instruction, kRedirectMonolithic.data(),
                                    kRedirectMonolithic.size())
                == 0;
}

std::array<std::string, 4> ParsePatchInstructionFields(const char *instruction) {
  constexpr std::size_t kFieldBufferBytes = 250;

  std::array<std::string, 4> fields{};
  if (instruction == nullptr) {
    return fields;
  }

  const char *cursor = instruction;
  for (auto &field : fields) {
    while (*cursor == ';') {
      ++cursor;
    }
    if (*cursor == '\0') {
      break;
    }

    const char *token_begin = cursor;
    while (*cursor != '\0' && *cursor != ';') {
      ++cursor;
    }

    const auto token_length =
        static_cast<std::size_t>(cursor - token_begin);
    field.assign(token_begin,
                 std::min(token_length, kFieldBufferBytes - 1));

    if (*cursor == ';') {
      ++cursor;
    }
  }

  return fields;
}

bool TryDerivePatchDestination(const std::string_view url,
                               std::string &destination) {
  const auto slash = url.find_last_of('/');
  if (slash == std::string_view::npos) {
    return false;
  }

  destination.assign(url.substr(slash + 1));
  return true;
}

int RealmListWriteWindow(net::CDataStore* ,
                         std::uint32_t offset, std::uint32_t length,
                         std::uint8_t** data_ptr,
                         std::uint32_t* window_base,
                         std::uint32_t* window_size,
                         const char* , std::uint32_t ) {
  const std::uint32_t needed = (offset + length + 255u) & ~255u;
  auto* p = static_cast<std::uint8_t*>(std::realloc(*data_ptr, needed));
  if (!p) return 0;
  *data_ptr = p;
  *window_base = 0;
  *window_size = needed;
  return 1;
}

const net::CDataStoreVTable kRealmListStoreVTable{
    nullptr,
    nullptr,
    nullptr,
    RealmListWriteWindow,
};

struct ScopedFlatStore {
  net::CDataStore store{};

  ScopedFlatStore() {
    store.vtable = &kRealmListStoreVTable;
    store.read_pos = 0xFFFFFFFF;
  }

  ~ScopedFlatStore() { std::free(store.data); }

  ScopedFlatStore(const ScopedFlatStore&) = delete;
  ScopedFlatStore& operator=(const ScopedFlatStore&) = delete;
};

}

namespace detail {

BattlenetDescriptorMessageEnvelope::BattlenetDescriptorMessageEnvelope(
    const std::uint32_t schema_id,
    void *payload) noexcept
    : binding_{.schema_id = schema_id},
      cursor_{.binding = &binding_, .payload = payload} {}

BattlenetDescriptorMessageCursor *BattlenetDescriptorMessageEnvelope::GetEmbeddedVariant() noexcept {
  return &cursor_;
}

const BattlenetDescriptorMessageCursor *
BattlenetDescriptorMessageEnvelope::GetEmbeddedVariant() const noexcept {
  return &cursor_;
}

BattlenetDescriptorMessageBinding *BattlenetDescriptorMessageEnvelope::GetEmbeddedBinding() noexcept {
  return &binding_;
}

const BattlenetDescriptorMessageBinding *
BattlenetDescriptorMessageEnvelope::GetEmbeddedBinding() const noexcept {
  return &binding_;
}

BattlenetRecoveredResponseEnvelope::BattlenetRecoveredResponseEnvelope() noexcept
    : envelope_(kSchemaId, &payload_storage_) {}

BattlenetDescriptorMessageCursor *BattlenetRecoveredResponseEnvelope::GetEmbeddedVariant() noexcept {
  return envelope_.GetEmbeddedVariant();
}

const BattlenetDescriptorMessageCursor *
BattlenetRecoveredResponseEnvelope::GetEmbeddedVariant() const noexcept {
  return envelope_.GetEmbeddedVariant();
}

BattlenetDescriptorMessageBinding *BattlenetRecoveredResponseEnvelope::GetEmbeddedBinding() noexcept {
  return envelope_.GetEmbeddedBinding();
}

const BattlenetDescriptorMessageBinding *
BattlenetRecoveredResponseEnvelope::GetEmbeddedBinding() const noexcept {
  return envelope_.GetEmbeddedBinding();
}

BattlenetRecoveredResponsePayloadStorage &
BattlenetRecoveredResponseEnvelope::payload_storage() noexcept {
  return payload_storage_;
}

const BattlenetRecoveredResponsePayloadStorage &
BattlenetRecoveredResponseEnvelope::payload_storage() const noexcept {
  return payload_storage_;
}

}

BattlenetDispatchValue BattlenetDispatchValue::String(std::string value) {
  BattlenetDispatchValue result;
  result.type = BattlenetDispatchValueType::kString;
  result.string_value = std::move(value);
  return result;
}

BattlenetDispatchValue BattlenetDispatchValue::UInt8(
    const std::uint8_t value) {
  BattlenetDispatchValue result;
  result.type = BattlenetDispatchValueType::kUInt8;
  result.integer_value = value;
  return result;
}

BattlenetDispatchValue BattlenetDispatchValue::UInt32(
    const std::uint32_t value) {
  BattlenetDispatchValue result;
  result.type = BattlenetDispatchValueType::kUInt32;
  result.integer_value = value;
  return result;
}

BattlenetDispatchValue BattlenetDispatchValue::Bytes(
    const void *data, const std::size_t size) {
  BattlenetDispatchValue result;
  result.type = BattlenetDispatchValueType::kBytes;
  if (data != nullptr && size != 0u) {
    const auto *bytes = static_cast<const std::uint8_t *>(data);
    result.bytes_value.assign(bytes, bytes + size);
  }
  return result;
}

detail::BattlenetLoginTransportService::~BattlenetLoginTransportService() {
  if (connection_ != nullptr) {
    connection_->Close();
    connection_->SetHandlerSynchronously(nullptr);
    openwow::net::WowConnection_Release(connection_);
    connection_ = nullptr;
  }

  pending_state_ = nullptr;
  owner_ = nullptr;
}

void detail::BattlenetLoginTransportService::OnConnected(
    const void *connection_data, [[maybe_unused]] std::size_t data_size) {
  assert(connection_data != nullptr);
  assert(data_size >= 24);

  const auto *bytes = static_cast<const std::uint8_t *>(connection_data);

  server_ip_[0] = bytes[20];
  server_ip_[1] = bytes[21];
  server_ip_[2] = bytes[22];
  server_ip_[3] = bytes[23];

  std::memcpy(&server_port_raw_, &bytes[18], sizeof(server_port_raw_));
  server_port_ = static_cast<std::uint16_t>(
      (static_cast<std::uint16_t>(bytes[18]) << 8u) | bytes[19]);

  assert(owner_ != nullptr);

  owner_->QueueEvent(1, bytes, 16);
}

void detail::BattlenetLoginTransportService::OnDisconnected() {

  if (owner_) {
    owner_->QueueEvent(2);
  }
}

void detail::BattlenetLoginTransportService::OnConnectionClosed() {

  if (owner_) {
    owner_->QueueEvent(3);
  }
}

void detail::BattlenetLoginTransportService::OnDataReceived(
    const void *data, std::size_t size) {
  assert(owner_ != nullptr);
  owner_->QueueEvent(4, data, size);
}

int detail::BattlenetLoginTransportService::SendData(
    [[maybe_unused]] std::uint32_t session_id, uint8_t *buf, std::size_t len) {
  assert(session_id == connection_session_id_);
  assert(connection_ != nullptr);
  return connection_->SendRawBuffer(buf, len, true);
}

BattlenetLogin::~BattlenetLogin() {
  Cleanup();
}

bool BattlenetLogin::OnConnected(const void *address) {
  login_byte_5_flag_ = true;
  if (transport_service_ == nullptr || !dispatcher_available_) {
    openwow::core::SErrFatalCondition("%s", "");
  }

  const auto &ip = transport_service_->server_ip();
  const std::uint16_t port = transport_service_->server_port();
  char connected_message[64]{};
  std::snprintf(connected_message, sizeof(connected_message),
                "Connected to %u.%u.%u.%u:%u",
                static_cast<unsigned>(ip[0]), static_cast<unsigned>(ip[1]),
                static_cast<unsigned>(ip[2]), static_cast<unsigned>(ip[3]),
                static_cast<unsigned>(port));
  openwow::core::ida::ConsoleAddLine(connected_message,
                                     openwow::core::ida::COLOR_DEFAULT);
  if (dispatcher_backend_ != nullptr && transport_service_ != nullptr) {
    dispatcher_backend_->OnConnected(transport_service_->connection(), address);
  }
  return true;
}

bool BattlenetLogin::OnDisconnected() {
  if (transport_service_ == nullptr || !dispatcher_available_) {
    openwow::core::SErrFatalCondition("%s", "");
  }
  if (dispatcher_backend_ != nullptr && transport_service_ != nullptr) {
    dispatcher_backend_->OnDisconnected(transport_service_->connection());
  }
  return false;
}

void *BattlenetLogin::Alloc(int size) {

  return std::malloc(static_cast<size_t>(size));
}

void BattlenetLogin::Free(void *block) {

  std::free(block);
}

[[noreturn]] void BattlenetLogin::AssertAndCrash(const char *message, const char *file,
                                                 uint32_t line) {

  (void)file;
  (void)line;
  fprintf(stderr, "Internal Battle.net Error: %s\n", message ? message : "");
  std::abort();
}

std::uint8_t BattlenetLogin::LookupRealmRecommended(std::uint32_t category_id,
                                                     std::uint32_t sort_key1,
                                                     std::uint32_t sort_key2) const {
  for (const auto &entry : realm_recommended_table_) {
    if (entry.category_id == category_id &&
        entry.sort_key1 == sort_key1 &&
        entry.sort_key2 == sort_key2) {
      return static_cast<std::uint8_t>(entry.recommended);
    }
  }
  return 0;
}

void BattlenetLogin::SetRealmRecommendedTable(std::vector<RealmRecommendedEntry> entries) {
  realm_recommended_table_ = std::move(entries);
}

int BattlenetLogin::ParseComponentVersion(const char *component_name, uint32_t *out_version,
                                          const void *xml_data, size_t xml_size) {
  openwow::ui::xml::CXMLTree *tree = openwow::ui::xml::XMLTree_Parse(xml_data, xml_size);
  if (!tree || !tree->root) {
    openwow::ui::xml::XMLTree_Free(tree);
    return 0;
  }

  for (openwow::ui::xml::CXMLNode *node = tree->root->first_child; node != nullptr;
       node = node->right_sibling) {
    if (openwow::core::SStrCmpNoCase(node->tag.c_str(), "component", 0x7FFFFFFFu) != 0) {
      continue;
    }

    const char *name = FindXmlAttributeValue(*node, "name");
    const char *version = FindXmlAttributeValue(*node, "version");
    if (!name || !version) {
      continue;
    }

    if (openwow::core::SStrCmpNoCase(name, component_name, 0x7FFFFFFFu) == 0) {
      *out_version = ParseStormDecimal(version);
    }
  }

  openwow::ui::xml::XMLTree_Free(tree);
  return 1;
}

int BattlenetLogin::GetWowDataVersion() {
  constexpr std::uint32_t kFallbackVersion = 12340;

  int xml_size = 0;
  void *xml_data = nullptr;
  std::uint32_t version = kFallbackVersion;
  if (!openwow::vfs::SFileReadFileToBuffer_SetLastError(nullptr, "component.wow-data.txt",
                                                        &xml_data, &xml_size, 0, 0, 0) ||
      !xml_data) {
    return static_cast<int>(kFallbackVersion);
  }

  ParseComponentVersion("wow-data", &version, xml_data, static_cast<std::size_t>(xml_size));
  openwow::vfs::SFileFreeLoadedData(xml_data);
  return static_cast<int>(version);
}

int BattlenetLogin::GetLocaleComponentVersion(const char *locale) {
  constexpr std::uint32_t kFallbackVersion = 12340;

  char archive_path[256] = {};
  char component_filename[32] = {};
  char component_name[32] = {};
  std::uint32_t version = kFallbackVersion;
  std::size_t xml_size = 0;
  void *xml_data = nullptr;
  void *archive = nullptr;

  const std::string current_locale = openwow::ui::game::CVarSystem::Instance().GetCVar("locale");
  if (openwow::core::SStrCmpI(locale, current_locale.c_str(), 0x7FFFFFFFu) != 0) {
    std::snprintf(archive_path, sizeof(archive_path), "Data/%s/patch-%s-2.MPQ", locale, locale);
    if (!openwow::vfs::FileSystem_IsRegularFile(archive_path)) {
      std::snprintf(archive_path, sizeof(archive_path), "Data/%s/patch-%s.MPQ", locale, locale);
      if (!openwow::vfs::FileSystem_IsRegularFile(archive_path)) {
        std::snprintf(archive_path, sizeof(archive_path), "Data/%s/locale-%s.MPQ", locale, locale);
      }
    }

    if (!openwow::vfs::SFileOpenArchiveWrapped(archive_path, 0, 0x800u, &archive)) {
      return static_cast<int>(kFallbackVersion);
    }
  }

  std::snprintf(component_filename, sizeof(component_filename), "component.wow-%s.txt", locale);
  if (!openwow::vfs::SFileOpenFileAndLoadData(archive, component_filename, &xml_data, &xml_size, 0,
                                              0, 0) ||
      !xml_data) {
    if (archive) {
      (void)openwow::vfs::SFileCloseArchiveWrapped(archive);
    }
    return static_cast<int>(kFallbackVersion);
  }

  std::snprintf(component_name, sizeof(component_name), "wow-%s", locale);
  ParseComponentVersion(component_name, &version, xml_data, xml_size);
  openwow::vfs::SFileFreeLoadedData(xml_data);
  if (archive) {
    (void)openwow::vfs::SFileCloseArchiveWrapped(archive);
  }
  return static_cast<int>(version);
}

void BattlenetLogin::ParseMatrixCardChallenge(const char *params_string,
                                              const char *cell_ids_string) {

  auto parse_uint = [](const char *s) -> std::uint32_t {
    if (!s) return 0;
    return static_cast<std::uint32_t>(std::strtoul(s, nullptr, 10));
  };
  auto find_comma = [](const char *s) -> const char * {
    if (!s) return nullptr;
    const char *p = std::strchr(s, ',');
    return p;
  };

  matrix_num_rows_ = static_cast<std::uint8_t>(parse_uint(params_string));
  const char *sep = find_comma(params_string);
  assert(sep && "BattlenetLogin_func5: expected comma after field 1");

  const char *p = sep + 1;
  matrix_num_cols_ = static_cast<std::uint8_t>(parse_uint(p));
  sep = find_comma(p);
  assert(sep && "BattlenetLogin_func5: expected comma after field 2");

  p = sep + 1;
  matrix_digit_count_ = static_cast<std::uint8_t>(parse_uint(p));
  sep = find_comma(p);
  assert(sep && "BattlenetLogin_func5: expected comma after field 3");

  p = sep + 1;
  matrix_challenge_size_ = static_cast<std::uint8_t>(parse_uint(p));
  sep = find_comma(p);
  assert(sep && "BattlenetLogin_func5: expected comma after field 4");

  p = sep + 1;
  matrix_has_challenge_ = (parse_uint(p) != 0);
  sep = find_comma(p);
  assert(sep && "BattlenetLogin_func5: expected comma after field 5");

  p = sep + 1;
  matrix_cell_count_ = static_cast<std::uint8_t>(parse_uint(p));
  sep = find_comma(p);
  assert(!sep && "BattlenetLogin_func5: unexpected trailing comma after field 6");

  assert(matrix_num_rows_ && "BattlenetLogin_func5: num_rows must be non-zero");
  assert(matrix_num_cols_ && "BattlenetLogin_func5: num_cols must be non-zero");
  assert(matrix_digit_count_ && "BattlenetLogin_func5: digit_count must be non-zero");
  assert(matrix_challenge_size_ && "BattlenetLogin_func5: challenge_size must be non-zero");
  assert(matrix_cell_count_ && "BattlenetLogin_func5: cell_count must be non-zero");

  matrix_cell_ids_.resize(matrix_cell_count_);
  matrix_entries_remaining_ = matrix_cell_count_;

  const char *cursor = cell_ids_string;
  for (std::uint8_t i = 0; i < matrix_cell_count_; ++i) {
    assert(cursor && "BattlenetLogin_func5: cell_ids_string ended prematurely");
    if (i > 0) {
      ++cursor;
    }
    matrix_cell_ids_[i] = parse_uint(cursor);
    cursor = find_comma(cursor);
  }
  assert(!cursor && "BattlenetLogin_func5: unexpected trailing data in cell_ids_string");

  matrix_challenge_active_ = true;

  DispatchStatus(10, 0);
}

bool BattlenetLogin::GetMatrixCardInfo(std::uint32_t &out_num_rows,
                                       std::uint32_t &out_num_cols,
                                       std::uint32_t &out_digit_count,
                                       std::uint32_t &out_challenge_size,
                                       bool &out_has_challenge,
                                       std::uint32_t &out_cell_count) const {
  if (!matrix_challenge_active_) {
    return false;
  }
  out_num_rows = matrix_num_rows_;
  out_num_cols = matrix_num_cols_;
  out_digit_count = matrix_digit_count_;
  out_challenge_size = matrix_challenge_size_;
  out_has_challenge = matrix_has_challenge_;
  out_cell_count = matrix_cell_count_;
  return true;
}

bool BattlenetLogin::GetMatrixCardCellCoordinate(std::uint32_t index,
                                                 std::uint32_t &out_row,
                                                 std::uint32_t &out_col) const {
  if (!matrix_challenge_active_) {
    return false;
  }
  if (matrix_cell_ids_.empty()) {
    return false;
  }
  if (index >= matrix_cell_count_) {
    return false;
  }
  const std::uint32_t cell_id = matrix_cell_ids_[index];
  out_row = cell_id % matrix_num_rows_;
  out_col = cell_id / matrix_num_rows_;
  if (out_col >= matrix_num_cols_) {
    assert(false && "BattlenetLogin: matrix card cell coordinate out of grid bounds");
    return false;
  }
  return true;
}

void BattlenetLogin::InitiateAuth(const char *redirect_url) {
  if (login_byte_5_flag_) {
    return;
  }

  if (!dispatcher_available_) {

    DispatchStatus(5, 29);
    return;
  }

  const auto at_pos = account_name_.find('@');
  if (at_pos == std::string::npos) {

    return;
  }

  if (account_name_.size() > 320) {

    return;
  }

  login_byte_4_flag_ = false;
  state_ = 4;
  connect_tick_ = 0xFFFFFFFF;

  DispatchStatus(1, 0);

  if (redirect_url && redirect_url[0] != '\0') {

    BNetLoginRedirectEventBuf redirect_buf;
    redirect_buf.Init();

    auto *sentinel_ptr = &redirect_buf.sentinel;
    auto *data = BNetEventField_InitTag(sentinel_ptr, 0);
    if (data) {
      BNetEventField_CopyBoundedString(data, redirect_url,
                                        kBNetFieldMaxChars31);
    }

    (void)DispatchEvent(BattlenetDispatchEvent{
        .event_type = kBNetLoginRedirectEventType,
        .values = {BattlenetDispatchValue::String(redirect_url)},
    });
  }

  game_account_selection_pending_ = false;

  BNetEventBufHeader auth_header;
  auth_header.Init();

  auto *module_type_ptr =
      reinterpret_cast<std::int32_t *>(&auth_header.game_account_module.module_type);
  auto *field_data = BNetEventField_InitTag(module_type_ptr, 1);

  std::transform(account_name_.begin(), account_name_.end(),
                 account_name_.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  if (field_data) {
    BNetEventField_CopyBoundedString(field_data, account_name_.c_str(),
                                      kBNetFieldMaxChars320);
  }

  (void)DispatchEvent(BattlenetDispatchEvent{
      .event_type = auth_header.event_type,
      .module_type = auth_header.game_account_module.module_type,
      .request_id = CurrentRequestId(),
      .values = {BattlenetDispatchValue::String(account_name_)},
  });
}

int BattlenetLogin::Connect(const uint8_t *address, int ) {
  connect_tick_ = openwow::core::GameClock::GetTickCount32();

  std::memcpy(connect_addr_raw_.data(), address, 6);

  if (!transport_service_) {
    transport_service_ = std::make_unique<detail::BattlenetLoginTransportService>();
  }

  if (!transport_service_->connection()) {
    auto *conn = new openwow::net::WowConnection();
    openwow::net::WowConnection_Initialize(conn, 0, 0);
    openwow::net::WowConnection_SetReceiveDispatchMode(conn, 1);
    transport_service_->SetConnection(conn);
  }
  transport_service_->SetConnectionSessionId(connect_tick_);

  char buffer[64];
  const int port = (static_cast<int>(address[4]) << 8) | address[5];
  std::snprintf(buffer, sizeof(buffer), "%d.%d.%d.%d:%d",
                address[0], address[1], address[2], address[3], port);

  openwow::core::ida::ConsoleLog("Connecting to %s", buffer);

  openwow::net::WowConnection_ConnectByHostPortString(
      transport_service_->connection(), buffer, 5000);

  return static_cast<int>(connect_tick_);
}

int BattlenetLogin::CreateDispatcher() {

  const char *program_name = "WoW";
  if (openwow::data::IsOnlineModeActive()) {
    program_name = "WoWS";
  }

  WoWSDispatcherPayload payload;

  if (payload.random_length < kWoWSChallengeRandomSize) {
    std::memset(&payload.random_bytes[payload.random_length], 0,
                kWoWSChallengeRandomSize - payload.random_length);
  }
  payload.random_length = kWoWSChallengeRandomSize;
  openwow::platform::OsSecureRandom(payload.random_bytes.data(),
                                    kWoWSChallengeRandomSize);

  payload.program = StringToFourCC("WoW");

  payload.platform = StringToFourCC("Mac");

  const auto &locale_info = openwow::data::GetCurrentLocaleInfo();
  const char *locale_code = "enUS";
  if (locale_info.locale_index >= 0 &&
      static_cast<std::size_t>(locale_info.locale_index) < kLocaleNameCount) {
    locale_code = kLocaleNameByIndex[locale_info.locale_index];
  }
  const std::uint32_t locale_fourcc = StringToFourCC(locale_code);
  payload.country = locale_fourcc;
  payload.language = locale_fourcc;

  openwow::core::ida::ConsoleLog("Login program=%s platform=%s locale=%s",
                            "WoW", "Mac", locale_code);

  constexpr std::uint32_t kBuildNumber = 12340;

  if (s_platform_override_pending) {

    s_platform_override_pending = false;

    s_platform_override_code[0] = 'M';
    const char *lc = locale_code;
    if (lc[0] != '\0') {

      std::size_t i = 1;
      for (const char *p = lc + 1; *p && i < 4; ++p, ++i) {
        s_platform_override_code[i] = *p;
      }
      s_platform_override_code[i] = '\0';
    } else {
      s_platform_override_code[1] = '\0';
    }

    if (payload.component_count < kWoWSMaxComponents) {
      auto &comp = payload.components[payload.component_count++];
      comp.program = StringToFourCC(program_name);
      comp.platform = StringToFourCC(s_platform_override_code);
      comp.build = kBuildNumber;
    }
  } else {

    if (payload.component_count >= kWoWSMaxComponents) {
      assert(false && "Component overflow in CreateDispatcher");
      return 0;
    }
    {
      auto &comp = payload.components[payload.component_count++];
      comp.program = StringToFourCC(program_name);
      comp.platform = StringToFourCC("Mac");
      comp.build = kBuildNumber;
    }

    if (payload.component_count >= kWoWSMaxComponents) {
      assert(false && "Component overflow in CreateDispatcher");
      return 0;
    }
    {
      auto &comp = payload.components[payload.component_count++];
      comp.program = StringToFourCC("WoW");
      comp.platform = StringToFourCC("base");
      comp.build = static_cast<std::uint32_t>(GetWowDataVersion());
    }

    const auto &locale_ring = openwow::data::GetStartupLocaleRing();
    const auto &locale_avail = openwow::data::GetStartupLocaleAvailability();
    std::uint32_t locale_components_added = 0;

    for (std::size_t ring_idx = 0;
         ring_idx < openwow::data::kStartupLocaleRingSize; ++ring_idx) {
      if (!locale_avail[ring_idx]) {
        continue;
      }
      if (payload.component_count >= kWoWSMaxComponents) {
        assert(false && "Component overflow in CreateDispatcher");
        return 0;
      }
      const char *ring_locale = locale_ring[ring_idx];
      auto &comp = payload.components[payload.component_count++];
      comp.program = StringToFourCC("WoW");
      comp.platform = StringToFourCC(ring_locale);
      comp.build =
          static_cast<std::uint32_t>(GetLocaleComponentVersion(ring_locale));
      ++locale_components_added;
    }

    if (locale_components_added == 0) {
      if (payload.component_count >= kWoWSMaxComponents) {
        assert(false && "Component overflow in CreateDispatcher");
        return 0;
      }
      auto &comp = payload.components[payload.component_count++];
      comp.program = StringToFourCC("WoW");
      comp.platform = StringToFourCC(locale_code);
      comp.build =
          static_cast<std::uint32_t>(GetLocaleComponentVersion(locale_code));
    }

    if (payload.component_count >= kWoWSMaxComponents) {
      assert(false && "Component overflow in CreateDispatcher");
      return 0;
    }
    {
      auto &comp = payload.components[payload.component_count++];
      comp.program = StringToFourCC("Tool");
      comp.platform = StringToFourCC("Mac");
      openwow::platform::BundleVersionInfo launcher_version;
      comp.build = openwow::platform::GetBundleVersionInfo(
                       "World of Warcraft Launcher.app", launcher_version)
                       ? static_cast<std::uint32_t>(launcher_version.build)
                       : 0u;
    }
  }

  ClearPatchInstructionStrings();

  for (std::uint32_t i = 0; i < payload.component_count; ++i) {
    const auto &comp = payload.components[i];
    char prog_str[8];
    char plat_str[8];
    FourCCToString(comp.program, prog_str, sizeof(prog_str));
    FourCCToString(comp.platform, plat_str, sizeof(plat_str));
    openwow::core::ida::ConsoleLog("Component %s.%s.%d", prog_str, plat_str,
                              static_cast<int>(comp.build));
  }

  if (!dispatcher_available_) {
    return 0;
  }
  if (dispatcher_backend_ != nullptr &&
      !dispatcher_backend_->Create(payload)) {
    return 0;
  }

  last_dispatcher_payload_ = std::move(payload);
  return 1;
}

const std::optional<WoWSDispatcherPayload>&
BattlenetLogin::GetLastDispatcherPayloadForTests() const noexcept {
  return last_dispatcher_payload_;
}

uint32_t BattlenetLogin::GetNumGameAccounts() const {
  return static_cast<uint32_t>(game_accounts_.size());
}

const char *BattlenetLogin::GetGameAccountName(uint32_t index) const {
  if (index >= game_accounts_.size()) {
    return nullptr;
  }

  return game_accounts_[index].name;
}

uint32_t BattlenetLogin::GetGameAccountId(uint32_t index) const {
  if (index >= game_accounts_.size()) {
    return 0;
  }

  return game_accounts_[index].id;
}

const char *BattlenetLogin::GetPatchInstructionString(uint32_t index) const {
  if (index >= patch_instruction_strings_.size()) {
    return nullptr;
  }

  return patch_instruction_strings_[index].data();
}

void BattlenetLogin::ResizeGameAccountArray(uint32_t new_capacity) {
  if (new_capacity <= game_accounts_.capacity()) {
    return;
  }

  game_accounts_.reserve(new_capacity);
}

GameAccountEntry *BattlenetLogin::AppendGameAccount() {

  game_accounts_.emplace_back();
  return &game_accounts_.back();
}

void BattlenetLogin::ClearGameAccounts() {
  game_accounts_.clear();
  std::vector<GameAccountEntry>().swap(game_accounts_);
  pending_game_account_selection_.reset();
  game_account_selection_pending_ = false;
}

void BattlenetLogin::SetGameAccounts(std::vector<GameAccountEntry> accounts) {
  game_accounts_.clear();
  game_accounts_.reserve(accounts.size());
  for (const auto &entry : accounts) {
    game_accounts_.push_back(NormalizeGameAccountEntry(entry));
  }
  pending_game_account_selection_.reset();
  game_account_selection_pending_ = false;
}

void BattlenetLogin::ClearPatchInstructionStrings() {
  patch_instruction_strings_.clear();
  std::vector<std::array<char, kBattlenetPatchInstructionSlotBytes>>().swap(
      patch_instruction_strings_);
}

void BattlenetLogin::SetPatchInstructionStrings(
    std::vector<std::string> instructions) {
  ClearPatchInstructionStrings();
  patch_instruction_strings_.reserve(instructions.size());
  for (const auto &instruction : instructions) {
    patch_instruction_strings_.push_back(
        NormalizePatchInstructionString(instruction));
  }
}

PatchDownloadManifestPlan BattlenetLogin::BuildPatchDownloadPlan() const {
  PatchDownloadManifestPlan plan;

  for (std::uint32_t index = 0;; ++index) {
    const char *instruction = GetPatchInstructionString(index);
    if (instruction == nullptr) {
      break;
    }

    if (IsRedirectMonolithicInstruction(instruction)) {
      plan.uses_redirect_monolithic = true;
      plan.downloads.clear();
      return plan;
    }
  }

  for (std::uint32_t index = 0;; ++index) {
    const char *instruction = GetPatchInstructionString(index);
    if (instruction == nullptr) {
      break;
    }

    auto fields = ParsePatchInstructionFields(instruction);
    if (fields[0].empty()) {
      continue;
    }

    if (fields[1].empty()
        && !TryDerivePatchDestination(fields[0], fields[1])) {
      continue;
    }

    plan.downloads.push_back(
        PatchDownloadManifestEntry{std::move(fields[0]),
                                   std::move(fields[1]),
                                   std::move(fields[2]),
                                   std::move(fields[3])});
  }

  return plan;
}

void BattlenetLogin::SetDispatcherAvailable(bool available) {
  dispatcher_available_ = available;
}

bool BattlenetLogin::IsDispatcherAvailable() const {
  return dispatcher_available_;
}

void BattlenetLogin::SetDispatcherBackend(
    BattlenetDispatcherBackend *backend) {
  if (dispatcher_backend_ == backend) {
    return;
  }
  if (dispatcher_backend_ != nullptr && dispatcher_available_) {
    dispatcher_backend_->Shutdown();
  }
  dispatcher_backend_ = backend;
}

void BattlenetLogin::SetDispatcherRequestId(
    const std::uint32_t request_id) {
  dispatcher_request_id_ = request_id;
}

std::vector<BattlenetDispatchEvent>
BattlenetLogin::TakeDispatchedEvents() {
  std::lock_guard lock(dispatched_events_mutex_);
  std::vector<BattlenetDispatchEvent> events;
  events.swap(dispatched_events_);
  return events;
}

std::uint32_t BattlenetLogin::CurrentRequestId() const {
  return dispatcher_request_id_;
}

bool BattlenetLogin::DispatchEvent(BattlenetDispatchEvent event) {
  if (!dispatcher_available_) {
    return false;
  }

  {
    std::lock_guard lock(dispatched_events_mutex_);
    dispatched_events_.push_back(event);
  }
  return dispatcher_backend_ == nullptr || dispatcher_backend_->Send(event);
}

void BattlenetLogin::SetStatusCallback(StatusCallback callback) {
  status_callback_ = std::move(callback);
}

void BattlenetLogin::SetRealmListPacketCallback(RealmListPacketCallback callback) {
  realm_list_packet_callback_ = std::move(callback);
}

void BattlenetLogin::SetGlueEventCallback(GlueEventCallback callback) {
  glue_event_callback_ = std::move(callback);
}

void BattlenetLogin::SetAccountName(std::string name) {
  account_name_ = std::move(name);
}

const std::string &BattlenetLogin::account_name() const {
  return account_name_;
}

std::uint32_t BattlenetLogin::login_state() const {
  return login_state_;
}

std::uint32_t BattlenetLogin::login_result() const {
  return login_result_;
}

std::uint32_t BattlenetLogin::translated_auth_result_code() const {
  return translated_auth_result_code_;
}

bool BattlenetLogin::HasSunkenConnectFailure() const {
  return sunken_connect_failure_latched_;
}

void BattlenetLogin::SetLoginStateForTesting(const std::uint32_t state) {
  login_state_ = state;
}

void BattlenetLogin::SetLoginByte5FlagForTesting(bool flag) {
  login_byte_5_flag_ = flag;
}

void BattlenetLogin::RequestGameLogin() {
  if (!dispatcher_available_ || !login_byte_5_flag_) {
    return;
  }

  if (game_login_requested_) {
    QueueEvent(5);
    return;
  }

  if (!DispatchEvent(BattlenetDispatchEvent{.event_type = 9u})) {
    return;
  }
  game_login_requested_ = true;
}

bool BattlenetLogin::game_login_requested() const {
  return game_login_requested_;
}

void BattlenetLogin::SetRidFeatureBlockFlag(bool blocked) {
  constexpr std::uint32_t kRidFeatureBlockMask = 0x8;
  if (blocked) {
    rid_feature_gate_flags_ |= kRidFeatureBlockMask;
    return;
  }

  rid_feature_gate_flags_ &= ~kRidFeatureBlockMask;
}

bool BattlenetLogin::HasRidFeatureBlockFlag() const {
  constexpr std::uint32_t kRidFeatureBlockMask = 0x8;
  return (rid_feature_gate_flags_ & kRidFeatureBlockMask) != 0;
}

bool BattlenetLogin::SetGameAccount(uint32_t index) {
  if (index >= game_accounts_.size() || game_account_selection_pending_) {
    return false;
  }

  if (!dispatcher_available_) {
    return false;
  }

  const auto &account = game_accounts_[index];
  if (!DispatchEvent(BattlenetDispatchEvent{
          .event_type = 0u,
          .module_type = 2u,
          .request_id = CurrentRequestId(),
          .command = "SetGameAccount",
          .values = {
              BattlenetDispatchValue::UInt32(account.id),
              BattlenetDispatchValue::String(account.name),
          },
      })) {
    return false;
  }
  pending_game_account_selection_ = PendingGameAccountSelection{account.id, account.name};
  game_account_selection_pending_ = true;
  return true;
}

int BattlenetLogin::SubmitAuthenticator(const uint8_t *code) {

  if (!code) {
    return 0;
  }

  std::size_t len = 0;
  while (code[len] != 0) {
    ++len;
  }
  if (len != 10) {
    return 0;
  }

  std::array<std::uint8_t, 10> digits{};
  for (std::size_t i = 0; i < 10; ++i) {
    if (code[i] < '0' || code[i] > '9') {
      return 0;
    }
    digits[i] = static_cast<std::uint8_t>(code[i] - '0');
  }

  authenticator_digits_ = digits;
  authenticator_submitted_ = true;

  DispatchStatus(8, 0);
  return 1;
}

bool BattlenetLogin::has_authenticator_submission() const {
  return authenticator_submitted_;
}

std::array<std::uint8_t, 10> BattlenetLogin::authenticator_digits() const {
  return authenticator_digits_;
}

void BattlenetLogin::ClearAuthenticatorSubmission() {
  authenticator_submitted_ = false;
}

void BattlenetLogin::SerializeRealmList() {
  ScopedFlatStore flat;
  net::CDataStore& ds = flat.store;

  net::CDataStore_PutUInt16(ds, 0);

  std::uint16_t realm_count = 0;

  {
    const std::lock_guard<std::mutex> lock(realm_list_mutex_);

    for (const auto& entry : realm_entries_) {
      net::CDataStore_PutInt8(ds, static_cast<std::int8_t>(entry.type));
      net::CDataStore_PutInt8(ds, static_cast<std::int8_t>(entry.locked));
      net::CDataStore_PutInt8(ds, static_cast<std::int8_t>(entry.flags));
      net::CDataStore_PutString(ds, entry.name);
      net::CDataStore_PutString(ds, entry.address);
      net::CDataStore_PutFloat(ds, entry.population);
      net::CDataStore_PutInt8(ds, static_cast<std::int8_t>(entry.recommended));
      net::CDataStore_PutInt8(ds, static_cast<std::int8_t>(entry.timezone));
      net::CDataStore_PutUInt32(ds, entry.category_id);
      net::CDataStore_PutUInt32(ds, entry.sort_key1);
      net::CDataStore_PutUInt32(ds, entry.sort_key2);
      net::CDataStore_PutUInt32(ds, entry.num_characters);

      if (entry.flags & 0x04) {
        net::CDataStore_PutInt8(ds, static_cast<std::int8_t>(entry.version_major));
        net::CDataStore_PutInt8(ds, static_cast<std::int8_t>(entry.version_minor));
        net::CDataStore_PutInt8(ds, static_cast<std::int8_t>(entry.version_patch));
        net::CDataStore_PutUInt16(ds, entry.version_build);
      }

      ++realm_count;
    }
  }

  net::CDataStore_PutUInt16(ds, 0);

  net::CDataStore_PutUInt16At(ds, 0, realm_count);

  ds.read_pos = 0;

  if (realm_list_packet_callback_) {
    realm_list_packet_callback_(ds.data, ds.write_pos);
  }

}

void BattlenetLogin::SubmitPassword() {
  if (pending_password_.empty()) {
    return;
  }

  const std::string password = pending_password_;

  BNetEventBufHeader header;
  header.Init();

  header.game_account_module.request_id = CurrentRequestId();

  if (!pending_password_.empty()) {
    volatile char *p = pending_password_.data();
    for (std::size_t i = 0; i < pending_password_.size(); ++i) {
      p[i] = '\0';
    }
  }
  pending_password_.clear();

  if (!dispatcher_available_) {
    openwow::core::SErrFatalCondition("%s", "");
  }
  (void)DispatchEvent(BattlenetDispatchEvent{
      .event_type = header.event_type,
      .module_type = header.game_account_module.module_type,
      .request_id = header.game_account_module.request_id,
      .command = "SubmitPassword",
      .values = {BattlenetDispatchValue::String(password)},
  });
}

void BattlenetLogin::SetPendingPassword(std::string password) {

  if (!pending_password_.empty()) {
    volatile char *p = pending_password_.data();
    for (std::size_t i = 0; i < pending_password_.size(); ++i) {
      p[i] = '\0';
    }
  }
  pending_password_ = std::move(password);
}

bool BattlenetLogin::HasPendingPassword() const {
  return !pending_password_.empty();
}

void *BattlenetLogin::GetServiceNotificationField(void *repeated_field,
                                                   uint32_t index) {
  auto *const words = static_cast<std::uint32_t *>(repeated_field);
  if (index >= words[0]) {
    openwow::core::SErrFatalCondition("%s", "");
  }

  return words + 260u * index + 1;
}

void *BattlenetLogin::GetServiceResponse(void *table, uint32_t index) {
  auto *const words = static_cast<std::uint32_t *>(table);
  if (index >= words[0]) {
    openwow::core::SErrFatalCondition("%s", "");
  }

  return words + 3 * index + 1;
}

void *BattlenetLogin::ValidateServiceState(void *service) {
  auto *const words = static_cast<std::uint32_t *>(service);
  if (words[0] != 3u) {
    openwow::core::SErrFatalCondition("%s", "");
  }

  return words + 1;
}

void BattlenetLogin::TranslateAuthResult(const std::uint32_t auth_result_code) {
  translated_auth_result_code_ = auth_result_code;

  switch (auth_result_code) {
  case 0:
    DispatchStatus(4, 0);
    break;
  case 103:
    DispatchStatus(5, 4);
    break;
  case 104:
  case 108:
  case 116:
  case 201:
    DispatchStatus(5, 7);
    break;
  case 105:
  case 106:
    DispatchStatus(5, 21);
    break;
  case 107:
    DispatchStatus(5, 26);
    break;
  case 109:
  case 110:
  case 111:
  case 112:
    DispatchStatus(5, 14);
    break;
  case 113:
  case 200:
    DispatchStatus(5, 27);
    break;
  case 115:
  case 205:
    DispatchStatus(5, 15);
    break;
  case 123:
    DispatchStatus(5, 33);
    break;
  case 124:
    DispatchStatus(5, 32);
    break;
  case 140:
    DispatchStatus(5, 39);
    break;
  case 141:
    DispatchStatus(5, 40);
    break;
  case 202:
    DispatchStatus(5, 13);
    break;
  case 203:
    DispatchStatus(5, 18);
    break;
  case 204:
  case 212:
    DispatchStatus(5, 28);
    break;
  case 206:
    DispatchStatus(5, 16);
    break;
  case 207:
  case 208:
    DispatchStatus(5, 24);
    break;
  case 209:
    DispatchStatus(5, 19);
    break;
  case 210:
    DispatchStatus(5, 25);
    break;
  case 211:
    DispatchStatus(5, 23);
    break;
  default:
    DispatchStatus(5, 11);
    break;
  }
}

void BattlenetLogin::HandleSunkenConnectFailure() {
  openwow::core::ida::ConsoleAddLine("BattlenetLogin was unable to connect to Sunken",
                                     openwow::core::ida::COLOR_DEFAULT);

  DispatchStatus(5, 12);
  sunken_connect_failure_latched_ = true;
  TranslateAuthResult(2);
  login_byte_5_flag_ = false;
  login_byte_4_flag_ = false;
}

void *BattlenetLogin::ConstructService(void *self) {
  if (self == nullptr) {
    return nullptr;
  }

  return std::construct_at(static_cast<detail::BattlenetLoginTransportService *>(self));
}

void BattlenetLogin::DestroyDispatcher(void * , void **dispatcher) {
  if (dispatcher && *dispatcher) {

    *dispatcher = nullptr;
  }
}

void BattlenetLogin::QueueEvent(int event_type) {

  std::lock_guard<std::mutex> lock(event_queue_mutex_);
  event_queues_[current_event_queue_index_].push_front({event_type, {}});
}

void BattlenetLogin::QueueEvent(int event_type, const void *data,
                                std::size_t data_size) {

  std::vector<std::uint8_t> payload;
  if (data != nullptr && data_size > 0) {
    const auto *src = static_cast<const std::uint8_t *>(data);
    payload.assign(src, src + data_size);
  }
  std::lock_guard<std::mutex> lock(event_queue_mutex_);
  event_queues_[current_event_queue_index_].push_front(
      {event_type, std::move(payload)});
}

void BattlenetLogin::HandleRealmUpdate(int category_id, int sort_key1, int sort_key2,
                                       std::intptr_t server_data, uint8_t flags, bool remove) {
  std::lock_guard<std::mutex> lock(realm_list_mutex_);

  auto it = std::find_if(realm_entries_.begin(), realm_entries_.end(),
      [&](const BnRealmEntry &e) {
        return e.category_id == static_cast<std::uint32_t>(category_id) &&
               e.sort_key1 == static_cast<std::uint32_t>(sort_key1) &&
               e.sort_key2 == static_cast<std::uint32_t>(sort_key2);
      });

  if (remove && it != realm_entries_.end()) {
    realm_entries_.erase(it);
    return;
  }

  if (!server_data) {
    return;
  }

  if (it == realm_entries_.end()) {
    realm_entries_.emplace_back();
    it = realm_entries_.end() - 1;
  }

  BnRealmEntry &entry = *it;

  auto *sd = reinterpret_cast<const std::uint8_t *>(server_data);

  entry.type = sd[1036];
  entry.locked = sd[1044];
  entry.flags = flags;

  std::memset(entry.name, 0, sizeof(entry.name));
  std::strncpy(entry.name, reinterpret_cast<const char *>(sd + 12),
               sizeof(entry.name) - 1);

  std::strncpy(entry.address, "0.0.0.0", sizeof(entry.address) - 1);

  std::memcpy(&entry.population, sd + 1048, sizeof(float));

  entry.recommended = LookupRealmRecommended(
      static_cast<std::uint32_t>(category_id),
      static_cast<std::uint32_t>(sort_key1),
      static_cast<std::uint32_t>(sort_key2));

  entry.timezone = sd[1040];
  entry.category_id = static_cast<std::uint32_t>(category_id);
  entry.sort_key1 = static_cast<std::uint32_t>(sort_key1);
  entry.sort_key2 = static_cast<std::uint32_t>(sort_key2);

  if (sd[1052]) {
    std::uint32_t num_chars = 0;
    std::memcpy(&num_chars, sd + 1084, sizeof(std::uint32_t));
    entry.num_characters = num_chars;

    const char *ver_str = reinterpret_cast<const char *>(sd + 1060);
    entry.version_major = static_cast<std::uint8_t>(std::atoi(ver_str));

    const char *dot1 = std::strchr(ver_str, '.');
    if (dot1) {
      entry.version_minor = static_cast<std::uint8_t>(std::atoi(dot1 + 1));
      const char *dot2 = std::strchr(dot1 + 1, '.');
      if (dot2) {
        entry.version_patch = static_cast<std::uint8_t>(std::atoi(dot2 + 1));
        const char *dot3 = std::strchr(dot2 + 1, '.');
        if (dot3) {
          entry.version_build = static_cast<std::uint16_t>(std::atoi(dot3 + 1));
          entry.flags |= 0x04;
        }
      }
    }

    std::snprintf(entry.address, sizeof(entry.address), "%u.%u.%u.%u:%u",
                  sd[1088], sd[1089], sd[1090], sd[1091],
                  (static_cast<unsigned>(sd[1092]) << 8) + sd[1093]);
  } else {
    entry.num_characters = 0;
    entry.version_major = 0;
    entry.version_minor = 0;
    entry.version_patch = 0;
    entry.version_build = 0;
  }
}

bool BattlenetLogin::RemoveRealmEntry(std::uint32_t category_id, std::uint32_t sort_key1,
                                      std::uint32_t sort_key2) {
  std::lock_guard<std::mutex> lock(realm_list_mutex_);
  auto it = std::find_if(realm_entries_.begin(), realm_entries_.end(),
      [&](const BnRealmEntry &e) {
        return e.category_id == category_id &&
               e.sort_key1 == sort_key1 &&
               e.sort_key2 == sort_key2;
      });
  if (it != realm_entries_.end()) {
    realm_entries_.erase(it);
    return true;
  }
  return false;
}

void BattlenetLogin::Init() {

  state_ = 4;
  current_event_queue_index_ = 0;
}

void BattlenetLogin::Cleanup() {

  BattleNetUI::Shutdown();
  BattleNetApi::Instance().Clear();
  if (dispatcher_backend_ != nullptr && dispatcher_available_) {
    dispatcher_backend_->Shutdown();
  }
  transport_service_.reset();
  ClearGameAccounts();
  ClearPatchInstructionStrings();
  dispatcher_available_ = false;
  dispatcher_request_id_ = 0;
  last_dispatcher_payload_.reset();
  {
    std::lock_guard lock(dispatched_events_mutex_);
    dispatched_events_.clear();
  }
  {
    std::lock_guard lock(event_queue_mutex_);
    for (auto &queue : event_queues_) {
      queue.clear();
    }
    current_event_queue_index_ = 0;
  }
  matrix_challenge_active_ = false;
  matrix_entries_remaining_ = 0;
  translated_auth_result_code_ = 0;
  login_state_ = 0;
  login_result_ = 0;
  login_byte_4_flag_ = false;
  login_byte_5_flag_ = false;
  sunken_connect_failure_latched_ = false;
  rid_feature_gate_flags_ = 0;
  survey_requested_ = false;
  survey_request_tick_ = 0;
  state_ = 0;
}

void BattlenetLogin::StartLogin(int ) {

  translated_auth_result_code_ = 0;

  state_ = 4;

  {
    std::lock_guard<std::mutex> lock(event_queue_mutex_);
    for (auto &queue : event_queues_) {
      queue.clear();
    }
  }

  ClearGameAccounts();

  reconnect_pending_ = false;
  reconnect_tick_ = 0;

  login_byte_4_flag_ = false;
  login_byte_5_flag_ = false;
  sunken_connect_failure_latched_ = false;

  transport_service_.reset();

  transport_service_.reset(new (std::nothrow)
                               detail::BattlenetLoginTransportService());
  if (transport_service_ == nullptr) {
    if (dispatcher_backend_ != nullptr && dispatcher_available_) {
      dispatcher_backend_->Shutdown();
    }
    dispatcher_available_ = false;
    last_dispatcher_payload_.reset();
    DispatchStatus(5, 30);
    return;
  }

  transport_service_->SetOwner(this);

  const int dispatcher_result = CreateDispatcher();
  dispatcher_available_ = (dispatcher_result != 0);

  if (dispatcher_available_) {
    BattleNetUI::Init();
  } else {

    DispatchStatus(5, 29);
  }
}

void BattlenetLogin::ProcessEventQueue(const int queue_index) {
  if (queue_index < 0 || queue_index >= kBnEventQueueCount) {
    return;
  }

  std::deque<BnEventNode> events;
  {
    std::lock_guard lock(event_queue_mutex_);
    events.swap(event_queues_[static_cast<std::size_t>(queue_index)]);
  }

  bool handled = false;
  bool realm_list_ready = false;
  for (const BnEventNode &event : events) {
    if (event.event_type == 9) {
      handled = true;
      realm_list_ready = true;
      break;
    }
    if (event.event_type == 10) {
      login_byte_5_flag_ = false;
      handled = true;
    }
  }

  if (!handled) {
    for (const BnEventNode &event : events) {
      switch (event.event_type) {
      case 1:
        if (event.payload.size() != 16u) {
          openwow::core::SErrFatalCondition("%s", "");
        }
        (void)OnConnected(event.payload.data());
        break;
      case 2:
        (void)OnDisconnected();
        break;
      case 3:
        if (dispatcher_backend_ == nullptr || transport_service_ == nullptr) {
          openwow::core::SErrFatalCondition("%s", "");
        }
        dispatcher_backend_->OnConnectionClosed(transport_service_->connection());
        break;
      case 4:
        if (dispatcher_backend_ == nullptr || transport_service_ == nullptr ||
            !dispatcher_available_) {
          openwow::core::SErrFatalCondition("%s", "");
        }
        dispatcher_backend_->OnData(transport_service_->connection(),
                                    event.payload.data(), event.payload.size());
        break;
      case 5:
        SerializeRealmList();
        break;
      case 6:
        DispatchStatus(15u, 0u);
        BattleNetApi::FireEvent(675, nullptr);
        break;
      case 7:
        BattleNetApi::FireEvent(676, "%b", false);
        break;
      case 8:
        BattleNetApi::FireEvent(675, nullptr);
        break;
      case 11:
        if (glue_event_callback_) {
          glue_event_callback_(BattlenetGlueEvent::kGameAccountsUpdated);
        }
        break;
      case 12:
        HandleSunkenConnectFailure();
        break;
      case 13:
        survey_requested_ = true;
        survey_request_tick_ = openwow::core::GameClock::GetTickCount32();
        if (glue_event_callback_) {
          glue_event_callback_(BattlenetGlueEvent::kSurveyRequested);
        }
        break;
      case 14:
        if (!dispatcher_available_) {
          openwow::core::SErrFatalCondition("%s", "");
        }
        (void)DispatchEvent(BattlenetDispatchEvent{
            .event_type = 0,
            .module_type = 2,
            .request_id = CurrentRequestId(),
            .command = "Survey",
        });
        break;
      default:
        openwow::core::SErrFatalCondition("%s", "");
      }
    }
    return;
  }

  BattleNetApi::FireEvent(676, "%b", true);

  if (!login_byte_4_flag_) {
    state_ = 1u;
  }
  if (realm_list_ready) {
    DispatchStatus(16u, 0u);
  } else {
    TranslateAuthResult(state_ == 0u ? 105u : state_);
  }
  login_byte_5_flag_ = false;
  login_byte_4_flag_ = false;

  if (dispatcher_backend_ != nullptr && dispatcher_available_) {
    dispatcher_backend_->Shutdown();
  }
  dispatcher_available_ = CreateDispatcher() != 0;
  if (dispatcher_available_) {
    BattleNetUI::Init();
  }
}

[[noreturn]] void BattlenetLogin::GruntLoginBuildPacket() {

  std::abort();
}

void BattlenetLogin::RequestVirtualKeypadPIN(const unsigned int digit_count,
                                             const std::uint8_t *digits) {
  if (!authenticator_submitted_) {
    return;
  }

  authenticator_submitted_ = false;

  if (digit_count > 10) {
    openwow::core::SErrFatalCondition("%s", "");
  }

  char pin_ascii_buf[12]{};
  for (unsigned int i = 0; i < digit_count; ++i) {
    pin_ascii_buf[i] = static_cast<char>(digits[i] + '0');
  }
  pin_ascii_buf[digit_count] = '\0';

  BNetEventBufHeader header;
  header.Init();

  header.game_account_module.request_id = CurrentRequestId();

  if (!dispatcher_available_) {
    openwow::core::SErrFatalCondition("%s", "");
  }
  (void)DispatchEvent(BattlenetDispatchEvent{
      .event_type = header.event_type,
      .module_type = header.game_account_module.module_type,
      .request_id = header.game_account_module.request_id,
      .command = "RequestVirtualKeypadPIN",
      .values = {BattlenetDispatchValue::String(pin_ascii_buf)},
  });
}

void BattlenetLogin::CommitMatrixCard() {

  if (!matrix_challenge_active_) {
    openwow::core::SErrFatalCondition("%s", "");
  }
  if (!dispatcher_available_) {
    openwow::core::SErrFatalCondition("%s", "");
  }
  (void)DispatchEvent(BattlenetDispatchEvent{
      .event_type = 0u,
      .module_type = 2u,
      .request_id = CurrentRequestId(),
      .command = "CommitMatrixCard",
  });
}

void BattlenetLogin::EnterMatrixCard(const std::uint8_t digit) {

  if (!matrix_challenge_active_) {
    openwow::core::SErrFatalCondition("%s", "");
  }
  if (!dispatcher_available_) {
    openwow::core::SErrFatalCondition("%s", "");
  }
  (void)DispatchEvent(BattlenetDispatchEvent{
      .event_type = 0u,
      .module_type = 2u,
      .request_id = CurrentRequestId(),
      .command = "EnterMatrixCard",
      .values = {BattlenetDispatchValue::UInt8(digit)},
  });
}

void BattlenetLogin::RevertMatrixCard() {

  if (!matrix_challenge_active_) {
    openwow::core::SErrFatalCondition("%s", "");
  }
  if (!dispatcher_available_) {
    openwow::core::SErrFatalCondition("%s", "");
  }
  (void)DispatchEvent(BattlenetDispatchEvent{
      .event_type = 0u,
      .module_type = 2u,
      .request_id = CurrentRequestId(),
      .command = "RevertMatrixCard",
  });
}

void BattlenetLogin::SetMatrixChallengeActive(const bool active) {
  matrix_challenge_active_ = active;
  if (!active) {
    matrix_entries_remaining_ = 0;
  }
}

bool BattlenetLogin::HasPendingMatrixCardEntry() const {
  return matrix_challenge_active_ && matrix_entries_remaining_ != 0;
}

bool BattlenetLogin::GetPendingMatrixCardCoordinates(
    std::uint32_t &out_first, std::uint32_t &out_second) const {
  if (!HasPendingMatrixCardEntry()) {
    return false;
  }

  const std::uint32_t current_index =
      static_cast<std::uint32_t>(matrix_cell_count_ - matrix_entries_remaining_);
  return GetMatrixCardCellCoordinate(current_index, out_first, out_second);
}

bool BattlenetLogin::CommitPendingMatrixCardEntry() {
  if (!HasPendingMatrixCardEntry()) {
    return false;
  }

  CommitMatrixCard();
  --matrix_entries_remaining_;
  if (matrix_entries_remaining_ != 0) {
    return false;
  }

  FinalizeMatrixCard();
  return true;
}

void BattlenetLogin::FinalizeMatrixCard() {
  if (!matrix_challenge_active_) {
    openwow::core::SErrFatalCondition("%s", "");
  }
  matrix_challenge_active_ = false;
  matrix_entries_remaining_ = 0;
  if (!dispatcher_available_) {
    openwow::core::SErrFatalCondition("%s", "");
  }
  (void)DispatchEvent(BattlenetDispatchEvent{
      .event_type = 0u,
      .module_type = 2u,
      .request_id = CurrentRequestId(),
      .command = "FinalizeMatrixCard",
  });
}

bool BattlenetLogin::IsMatrixChallengeActive() const {
  return matrix_challenge_active_;
}

void BattlenetLogin::ParseTokenChallenge(const bool active,
                                          const std::uint8_t token_type) {
  if (!active) {
    return;
  }
  token_challenge_active_ = true;
  token_challenge_type_ = token_type;
  DispatchStatus(12, 0);
}

void BattlenetLogin::SubmitToken(const char *token_text) {
  if (!token_challenge_active_) {
    openwow::core::SErrFatalCondition("%s", "");
  }

  token_challenge_active_ = false;

  BNetEventBufHeader header;
  header.Init();

  header.game_account_module.request_id = CurrentRequestId();

  if (!dispatcher_available_) {
    openwow::core::SErrFatalCondition("%s", "");
  }
  (void)DispatchEvent(BattlenetDispatchEvent{
      .event_type = header.event_type,
      .module_type = header.game_account_module.module_type,
      .request_id = header.game_account_module.request_id,
      .command = "SubmitToken",
      .values = {BattlenetDispatchValue::String(
          token_text != nullptr ? token_text : "")},
  });
}

void BattlenetLogin::SetTokenChallengeActive(const bool active) {
  token_challenge_active_ = active;
}

bool BattlenetLogin::IsTokenChallengeActive() const {
  return token_challenge_active_;
}

void BattlenetLogin::SetTokenChallengeType(const std::uint8_t type) {
  token_challenge_type_ = type;
}

std::uint8_t BattlenetLogin::token_challenge_type() const {
  return token_challenge_type_;
}

int BattlenetLogin::GetPresenceValueSize() {
  return 16;
}

int BattlenetLogin::GetPresenceFieldCount(const void *self) {

  if (!self)
    return 0;
  return static_cast<const int *>(self)[3];
}

void RealmList_SortRealms(void * ) {

}

void BattlenetLogin::DispatchStatus(const std::uint32_t state,
                                    const std::uint32_t result,
                                    const std::int32_t error_code,
                                    const std::int32_t extra) {
  login_state_ = state;
  login_result_ = result;

  if (!status_callback_) {
    return;
  }

  status_callback_(state,
                   result,
                   error_code,
                   openwow::net::wotlk::ResolveLoginStateKey(state),
                   openwow::net::wotlk::ResolveLoginResultKey(result),
                   extra);
}

void BNetEventBuf_InitVariantSlots(BNetVariantSlotArray &slots) {
  slots.count = 0;
  for (auto &slot : slots.slots) {
    slot.type = -1;
    slot.data.fill(0);
  }
}

void BNetEventBuf_InitGameAccountModule(BNetGameAccountModule &mod) {
  mod.module_type = 2;
  mod.request_id = 0;
  mod.reserved_dword = 0;
  mod.reserved_flag = 0;
  mod.pad_to_dword_4_.fill(0);
  mod.command_descriptor_tail.fill(0);
  BNetEventBuf_InitVariantSlots(mod.variant_slots);
}

void BNetEventBuf_InitAuthHeader(BNetEventBufHeader &header) {
  header.event_type = 0;
  BNetEventBuf_InitGameAccountModule(header.game_account_module);
}

void BNetGameAccountModule::Init() {
  BNetEventBuf_InitGameAccountModule(*this);
}

void BNetEventBufHeader::Init() {
  BNetEventBuf_InitAuthHeader(*this);
}

std::int32_t *BNetEventBuf_InitLoginRedirectEvent(
    BNetLoginRedirectEventBuf &buf) {
  buf.event_type = kBNetLoginRedirectEventType;
  buf.sentinel   = -1;
  buf.reserved.fill(0);
  return &buf.sentinel;
}

void BNetLoginRedirectEventBuf::Init() {
  BNetEventBuf_InitLoginRedirectEvent(*this);
}

std::uint8_t* BNetEventField_InitTag(std::int32_t* tag_ptr,
                                      std::int32_t tag) {
  auto* data = reinterpret_cast<std::uint8_t*>(tag_ptr + 1);
  *tag_ptr = tag;
  if (!data) return nullptr;
  std::memset(data, 0, 5);
  return data;
}

void BNetEventField_CopyBoundedString(std::uint8_t* data_area,
                                       const char* src,
                                       std::size_t max_chars) {
  auto* str_dst = reinterpret_cast<char*>(data_area + 4);
  const std::string_view source(src ? src : "");
  std::size_t copied = CopyBoundedCStringPrefix(
      str_dst, max_chars + 1, source, max_chars);
  std::int32_t count = static_cast<std::int32_t>(copied);
  std::memcpy(data_area, &count, sizeof(count));
}

void BattlenetLogin::TickAndReconnect() {
  int read_queue = 0;
  {
    std::lock_guard lock(event_queue_mutex_);
    read_queue = current_event_queue_index_;
    current_event_queue_index_ = 1 - current_event_queue_index_;
  }
  ProcessEventQueue(read_queue);

  const std::uint32_t now = openwow::core::GameClock::GetTickCount32();
  if (dispatcher_backend_ != nullptr && dispatcher_available_) {
    dispatcher_backend_->Tick(now);
  }

  if (!reconnect_pending_) {
    return;
  }

  if (login_state_ == 2 || login_state_ == 5) {
    return;
  }

  if (static_cast<std::int32_t>(now - reconnect_tick_) <= 1000) {
    return;
  }

  reconnect_pending_ = false;

  DispatchStatus(5, 0);

  if (!dispatcher_available_) {
    return;
  }

  BNetEventBufHeader header;
  header.Init();

  header.game_account_module.request_id = CurrentRequestId();

  (void)DispatchEvent(BattlenetDispatchEvent{
      .event_type = header.event_type,
      .module_type = header.game_account_module.module_type,
      .request_id = header.game_account_module.request_id,
      .command = "Reconnect",
  });
}

}
