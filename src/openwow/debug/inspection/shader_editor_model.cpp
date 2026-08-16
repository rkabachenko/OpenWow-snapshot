#include "openwow/debug/inspection/shader_editor_model.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace openwow::debug {
namespace {

std::optional<int> ParseSignedInteger(std::string_view& text) noexcept {
  int value = 0;
  const char* const begin = text.data();
  const char* const end = begin + text.size();
  const auto parsed = std::from_chars(begin, end, value);
  if (parsed.ec != std::errc{} || parsed.ptr == begin) {
    return std::nullopt;
  }
  text.remove_prefix(static_cast<std::size_t>(parsed.ptr - begin));
  return value;
}

std::optional<int> ParseArbErrorLine(const std::string_view log) noexcept {
  constexpr std::string_view kPrefix{"Error on line "};
  if (!log.starts_with(kPrefix)) {
    return std::nullopt;
  }
  std::string_view remainder = log.substr(kPrefix.size());
  return ParseSignedInteger(remainder);
}

struct GlslLocation {
  int source_index{0};
  int line{0};
};

std::optional<GlslLocation> ParseGlslErrorLine(
    const std::string_view log) noexcept {
  constexpr std::string_view kPrefix{"ERROR: "};
  if (!log.starts_with(kPrefix)) {
    return std::nullopt;
  }
  std::string_view remainder = log.substr(kPrefix.size());
  const auto source_index = ParseSignedInteger(remainder);
  if (!source_index.has_value() || remainder.empty() ||
      remainder.front() != ':') {
    return std::nullopt;
  }
  remainder.remove_prefix(1U);
  const auto line = ParseSignedInteger(remainder);
  if (!line.has_value()) {
    return std::nullopt;
  }
  return GlslLocation{*source_index, *line};
}

std::string BoundedStatus(std::string status, const std::size_t limit) {
  if (status.size() > limit) {
    status.resize(limit);
  }
  return status;
}

}

InspectorShaderTextRange RetailShaderLineRange(
    const std::string_view source, const int one_based_line,
    const std::size_t starting_offset) noexcept {
  const std::size_t bounded_start = std::min(starting_offset, source.size());
  std::size_t line_start = 0;
  if (bounded_start != 0U) {
    const std::size_t preceding_newline = source.rfind('\n', bounded_start - 1U);
    line_start = preceding_newline == std::string_view::npos
                     ? 0U
                     : preceding_newline + 1U;
  }
  const int target_line = one_based_line > 1 ? one_based_line : 1;
  for (int line = 1; line < target_line; ++line) {
    const std::size_t newline = source.find('\n', line_start);
    if (newline == std::string_view::npos) {
      line_start = source.size();
      break;
    }
    line_start = newline + 1U;
  }
  const std::size_t newline = source.find('\n', line_start);
  const std::size_t line_end = newline == std::string_view::npos
                                   ? source.size()
                                   : newline + 1U;
  return {.offset = line_start, .length = line_end - line_start};
}

InspectorShaderEditorModel::InspectorShaderEditorModel(
    const InspectorShaderStage stage, std::string installed_source,
    std::string reference_source, const std::size_t arb_starting_offset)
    : stage_(stage),
      edited_source_(
          std::make_shared<const std::string>(std::move(installed_source))),
      saved_source_(edited_source_),
      installed_source_(edited_source_),
      reference_source_(
          std::make_shared<const std::string>(std::move(reference_source))),
      arb_starting_offset_(arb_starting_offset),
      history_{edited_source_},
      history_bytes_(edited_source_->size()) {}

InspectorShaderEditorModel::~InspectorShaderEditorModel() {
  std::lock_guard lock(mutex_);
  CancelCompileLocked();
  CancelFileOperationsLocked();
}

InspectorShaderDocumentSnapshot InspectorShaderEditorModel::Snapshot() const {
  std::lock_guard lock(mutex_);
  return {
      .document_id = document_id_,
      .source_revision = source_revision_,
      .stage = stage_,
      .mode = mode_,
      .path = path_,
      .edited_source = *edited_source_,
      .installed_source = *installed_source_,
      .reference_source = *reference_source_,
      .storage_version = storage_version_,
      .status_text = status_text_,
      .highlights = highlights_,
      .dirty = *edited_source_ != *saved_source_,
      .can_undo = history_cursor_ != 0U,
      .can_redo = history_cursor_ + 1U < history_.size(),
      .compile_pending = pending_compile_.has_value(),
      .save_pending = pending_save_.has_value(),
      .reload_pending = pending_reload_.has_value(),
  };
}

InspectorShaderStage InspectorShaderEditorModel::stage() const {
  std::lock_guard lock(mutex_);
  return stage_;
}

InspectorShaderMode InspectorShaderEditorModel::mode() const {
  std::lock_guard lock(mutex_);
  return mode_;
}

std::string InspectorShaderEditorModel::installed_source() const {
  std::lock_guard lock(mutex_);
  return *installed_source_;
}

std::string InspectorShaderEditorModel::reference_source() const {
  std::lock_guard lock(mutex_);
  return *reference_source_;
}

std::string InspectorShaderEditorModel::edited_source() const {
  std::lock_guard lock(mutex_);
  return *edited_source_;
}

bool InspectorShaderEditorModel::dirty() const {
  std::lock_guard lock(mutex_);
  return *edited_source_ != *saved_source_;
}

bool InspectorShaderEditorModel::compile_pending() const {
  std::lock_guard lock(mutex_);
  return pending_compile_.has_value();
}

bool InspectorShaderEditorModel::ResolvePath(
    const std::filesystem::path& trusted_root,
    const std::filesystem::path& relative_path,
    std::filesystem::path& resolved) {
  if (trusted_root.empty() || !trusted_root.is_absolute() ||
      relative_path.empty() || relative_path.is_absolute() ||
      relative_path.has_root_name() || relative_path.has_root_directory()) {
    return false;
  }
  for (const auto& component : relative_path) {
    if (component.empty() || component == "." || component == "..") {
      return false;
    }
  }
  const auto root = trusted_root.lexically_normal();
  const auto candidate = (root / relative_path).lexically_normal();
  auto root_part = root.begin();
  auto candidate_part = candidate.begin();
  for (; root_part != root.end(); ++root_part, ++candidate_part) {
    if (candidate_part == candidate.end() || *root_part != *candidate_part) {
      return false;
    }
  }
  resolved = candidate;
  return true;
}

InspectorShaderDocumentStatus InspectorShaderEditorModel::OpenDocument(
    InspectorShaderDocumentDescriptor descriptor, std::string source,
    std::string reference_source, const bool discard_dirty) {
  std::filesystem::path path;
  if (!ResolvePath(descriptor.trusted_root, descriptor.relative_path, path)) {
    return InspectorShaderDocumentStatus::kInvalidPath;
  }
  std::lock_guard lock(mutex_);
  if (!discard_dirty && *edited_source_ != *saved_source_) {
    return InspectorShaderDocumentStatus::kConflict;
  }
  CancelCompileLocked();
  CancelFileOperationsLocked();
  if (document_epoch_ == std::numeric_limits<std::uint64_t>::max()) {
    throw std::overflow_error("shader document generation exhausted");
  }
  const std::uint64_t revision = NextRevisionLocked();
  ++document_epoch_;
  document_id_ = descriptor.document_id;
  stage_ = descriptor.stage;
  path_ = std::move(path);
  storage_version_ = std::move(descriptor.storage_version);
  edited_source_ = std::make_shared<const std::string>(std::move(source));
  saved_source_ = edited_source_;
  installed_source_ = edited_source_;
  reference_source_ =
      std::make_shared<const std::string>(std::move(reference_source));
  source_revision_ = revision;
  status_text_.clear();
  highlights_.clear();
  ResetHistoryLocked();
  return InspectorShaderDocumentStatus::kApplied;
}

InspectorShaderDocumentStatus InspectorShaderEditorModel::SetEditedSource(
    std::string source) {
  std::lock_guard lock(mutex_);
  if (source == *edited_source_) {
    return InspectorShaderDocumentStatus::kUnchanged;
  }
  ReplaceEditedSourceLocked(
      std::make_shared<const std::string>(std::move(source)), true);
  return InspectorShaderDocumentStatus::kApplied;
}

InspectorShaderDocumentStatus InspectorShaderEditorModel::Undo() {
  std::lock_guard lock(mutex_);
  if (history_cursor_ == 0U) {
    return InspectorShaderDocumentStatus::kUnchanged;
  }
  --history_cursor_;
  ReplaceEditedSourceLocked(history_[history_cursor_], false);
  return InspectorShaderDocumentStatus::kApplied;
}

InspectorShaderDocumentStatus InspectorShaderEditorModel::Redo() {
  std::lock_guard lock(mutex_);
  if (history_cursor_ + 1U >= history_.size()) {
    return InspectorShaderDocumentStatus::kUnchanged;
  }
  ++history_cursor_;
  ReplaceEditedSourceLocked(history_[history_cursor_], false);
  return InspectorShaderDocumentStatus::kApplied;
}

InspectorShaderModeEffect InspectorShaderEditorModel::SelectMode(
    const InspectorShaderMode mode) {
  std::lock_guard lock(mutex_);
  mode_ = mode;
  return {
      .arb_path_enabled = mode != InspectorShaderMode::kGlsl,
      .glsl_path_enabled = mode != InspectorShaderMode::kArb,
      .replay_selected_batch = true,
  };
}

InspectorShaderCompileRequest InspectorShaderEditorModel::BeginCompile(
    std::string edited_source) {
  std::lock_guard lock(mutex_);
  if (edited_source != *edited_source_) {
    ReplaceEditedSourceLocked(
        std::make_shared<const std::string>(std::move(edited_source)), true);
  }
  CancelCompileLocked();
  const std::uint64_t generation = NextGenerationLocked();
  compile_cancellation_ = openwow::core::stop_source{};
  pending_compile_ = PendingCompile{
      .generation = generation,
      .document_epoch = document_epoch_,
      .source_revision = source_revision_,
      .edited_source = edited_source_,
      .previous_source = installed_source_,
  };
  status_text_.clear();
  highlights_.clear();
  return {
      .generation = generation,
      .document_id = document_id_,
      .source_revision = source_revision_,
      .stage = stage_,
      .mode = mode_,
      .edited_source = *edited_source_,
      .previously_installed_source = *installed_source_,
      .cancellation = compile_cancellation_.get_token(),
  };
}

InspectorShaderCompileRequest InspectorShaderEditorModel::BeginCompile() {
  std::lock_guard lock(mutex_);
  CancelCompileLocked();
  const std::uint64_t generation = NextGenerationLocked();
  compile_cancellation_ = openwow::core::stop_source{};
  pending_compile_ = PendingCompile{
      .generation = generation,
      .document_epoch = document_epoch_,
      .source_revision = source_revision_,
      .edited_source = edited_source_,
      .previous_source = installed_source_,
  };
  status_text_.clear();
  highlights_.clear();
  return {
      .generation = generation,
      .document_id = document_id_,
      .source_revision = source_revision_,
      .stage = stage_,
      .mode = mode_,
      .edited_source = *edited_source_,
      .previously_installed_source = *installed_source_,
      .cancellation = compile_cancellation_.get_token(),
  };
}

InspectorShaderCompletion InspectorShaderEditorModel::CompleteCompile(
    const std::uint64_t generation,
    const InspectorShaderBackendCompileResult& result) {
  std::lock_guard lock(mutex_);
  if (!pending_compile_.has_value() ||
      pending_compile_->generation != generation ||
      pending_compile_->document_epoch != document_epoch_ ||
      pending_compile_->source_revision != source_revision_ ||
      pending_compile_->ready_to_install) {
    return {.generation = generation};
  }
  InspectorShaderCompletion completion{
      .generation = generation,
      .document_id = document_id_,
      .source_revision = pending_compile_->source_revision,
  };
  if (result.succeeded) {
    std::string success_status = FormatSuccessStatus(result.statistics);
    highlights_.clear();
    pending_compile_->ready_to_install = true;
    pending_compile_->success_status = success_status;
    completion.status = InspectorShaderCompletionStatus::kReadyToInstall;
    completion.source_to_install = *pending_compile_->edited_source;
    completion.source_to_restore = *pending_compile_->previous_source;
    completion.status_text = std::move(success_status);
    return completion;
  }
  PendingCompile pending = std::move(*pending_compile_);
  pending_compile_.reset();
  compile_cancellation_ = openwow::core::stop_source{};
  const std::string arb_prefix = result.arb_log.empty() ? "" : "(ARB) ";
  const std::string glsl_prefix = result.glsl_log.empty() ? "" : "(GLSL) ";
  status_text_ = BoundedStatus(arb_prefix + result.arb_log + " " +
                                  glsl_prefix + result.glsl_log,
                              kMaxStatusBytes);
  highlights_ = ParseHighlights(result, *pending.edited_source);
  completion.status = InspectorShaderCompletionStatus::kFailed;
  completion.source_to_restore = *pending.previous_source;
  completion.status_text = status_text_;
  completion.highlights = highlights_;
  return completion;
}

InspectorShaderCompletion InspectorShaderEditorModel::CompleteInstallation(
    const std::uint64_t generation, const bool succeeded,
    std::string status_text) {
  std::lock_guard lock(mutex_);
  if (!pending_compile_.has_value() ||
      pending_compile_->generation != generation ||
      !pending_compile_->ready_to_install) {
    return {.generation = generation};
  }
  if (pending_compile_->document_epoch != document_epoch_ ||
      pending_compile_->source_revision != source_revision_) {
    PendingCompile stale = std::move(*pending_compile_);
    pending_compile_.reset();
    compile_cancellation_ = openwow::core::stop_source{};
    return {
        .generation = generation,
        .document_id = document_id_,
        .source_revision = stale.source_revision,
        .source_to_restore = *stale.previous_source,
    };
  }
  PendingCompile pending = std::move(*pending_compile_);
  pending_compile_.reset();
  compile_cancellation_ = openwow::core::stop_source{};
  InspectorShaderCompletion completion{
      .generation = generation,
      .document_id = document_id_,
      .source_revision = pending.source_revision,
  };
  if (succeeded) {
    installed_source_ = pending.edited_source;
    status_text_ = std::move(pending.success_status);
    completion.status = InspectorShaderCompletionStatus::kSucceeded;
    completion.status_text = status_text_;
    completion.replay_selected_batch = true;
    return completion;
  }
  status_text_ = BoundedStatus(std::move(status_text), kMaxStatusBytes);
  completion.status = InspectorShaderCompletionStatus::kFailed;
  completion.source_to_restore = *pending.previous_source;
  completion.status_text = status_text_;
  return completion;
}

void InspectorShaderEditorModel::CancelCompile() {
  std::lock_guard lock(mutex_);
  CancelCompileLocked();
}

std::optional<InspectorShaderSaveRequest>
InspectorShaderEditorModel::BeginSave() {
  std::lock_guard lock(mutex_);
  if (path_.empty() || *edited_source_ == *saved_source_) {
    return std::nullopt;
  }
  if (pending_save_.has_value()) {
    save_cancellation_.request_stop();
    pending_save_.reset();
  }
  const std::uint64_t generation = NextGenerationLocked();
  save_cancellation_ = openwow::core::stop_source{};
  pending_save_ = PendingSave{.generation = generation,
                              .document_epoch = document_epoch_,
                              .source_revision = source_revision_,
                              .source = edited_source_};
  return InspectorShaderSaveRequest{
      .generation = generation,
      .document_id = document_id_,
      .source_revision = source_revision_,
      .path = path_,
      .source = *edited_source_,
      .expected_storage_version = storage_version_,
      .cancellation = save_cancellation_.get_token(),
  };
}

InspectorShaderDocumentStatus InspectorShaderEditorModel::CompleteSave(
    const std::uint64_t generation, const InspectorShaderSaveResult& result) {
  std::lock_guard lock(mutex_);
  if (!pending_save_.has_value() || pending_save_->generation != generation ||
      pending_save_->document_epoch != document_epoch_) {
    return InspectorShaderDocumentStatus::kStale;
  }
  PendingSave pending = std::move(*pending_save_);
  pending_save_.reset();
  save_cancellation_ = openwow::core::stop_source{};
  if (result.conflict) {
    return InspectorShaderDocumentStatus::kConflict;
  }
  if (!result.succeeded) {
    return InspectorShaderDocumentStatus::kFailed;
  }
  saved_source_ = std::move(pending.source);
  storage_version_ = result.storage_version;
  return InspectorShaderDocumentStatus::kApplied;
}

std::optional<InspectorShaderReloadRequest>
InspectorShaderEditorModel::BeginReload() {
  std::lock_guard lock(mutex_);
  if (path_.empty()) {
    return std::nullopt;
  }
  if (pending_reload_.has_value()) {
    reload_cancellation_.request_stop();
    pending_reload_.reset();
  }
  const std::uint64_t generation = NextGenerationLocked();
  reload_cancellation_ = openwow::core::stop_source{};
  pending_reload_ = PendingReload{.generation = generation,
                                  .document_epoch = document_epoch_,
                                  .source_revision = source_revision_};
  return InspectorShaderReloadRequest{
      .generation = generation,
      .document_id = document_id_,
      .path = path_,
      .expected_storage_version = storage_version_,
      .cancellation = reload_cancellation_.get_token(),
  };
}

InspectorShaderDocumentStatus InspectorShaderEditorModel::CompleteReload(
    const std::uint64_t generation, std::string source,
    std::string storage_version, const bool discard_dirty) {
  std::lock_guard lock(mutex_);
  if (!pending_reload_.has_value() ||
      pending_reload_->generation != generation ||
      pending_reload_->document_epoch != document_epoch_) {
    return InspectorShaderDocumentStatus::kStale;
  }
  const PendingReload pending = *pending_reload_;
  pending_reload_.reset();
  reload_cancellation_ = openwow::core::stop_source{};
  if (!discard_dirty &&
      (pending.source_revision != source_revision_ ||
       *edited_source_ != *saved_source_)) {
    return InspectorShaderDocumentStatus::kConflict;
  }
  if (pending_save_.has_value()) {
    save_cancellation_.request_stop();
    pending_save_.reset();
  }
  CancelCompileLocked();
  const std::uint64_t revision = NextRevisionLocked();
  auto reloaded = std::make_shared<const std::string>(std::move(source));
  edited_source_ = reloaded;
  saved_source_ = std::move(reloaded);
  storage_version_ = std::move(storage_version);
  source_revision_ = revision;
  status_text_.clear();
  highlights_.clear();
  ResetHistoryLocked();
  return InspectorShaderDocumentStatus::kApplied;
}

void InspectorShaderEditorModel::CancelFileOperations() {
  std::lock_guard lock(mutex_);
  CancelFileOperationsLocked();
}

std::uint64_t InspectorShaderEditorModel::NextGenerationLocked() {
  if (next_generation_ == std::numeric_limits<std::uint64_t>::max()) {
    throw std::overflow_error("shader async generation exhausted");
  }
  return ++next_generation_;
}

std::uint64_t InspectorShaderEditorModel::NextRevisionLocked() {
  if (source_revision_ == std::numeric_limits<std::uint64_t>::max()) {
    throw std::overflow_error("shader source revision exhausted");
  }
  return source_revision_ + 1U;
}

void InspectorShaderEditorModel::ReplaceEditedSourceLocked(
    std::shared_ptr<const std::string> source, const bool record_history) {
  if (pending_compile_.has_value() && pending_compile_->ready_to_install) {
    compile_cancellation_.request_stop();
  } else {
    CancelCompileLocked();
  }
  const std::uint64_t revision = NextRevisionLocked();
  edited_source_ = std::move(source);
  source_revision_ = revision;
  status_text_.clear();
  highlights_.clear();
  if (!record_history) {
    return;
  }
  while (history_.size() > history_cursor_ + 1U) {
    history_bytes_ -= history_.back()->size();
    history_.pop_back();
  }
  history_.push_back(edited_source_);
  history_bytes_ += edited_source_->size();
  history_cursor_ = history_.size() - 1U;
  TrimHistoryLocked();
}

void InspectorShaderEditorModel::ResetHistoryLocked() {
  history_.assign(1U, edited_source_);
  history_cursor_ = 0;
  history_bytes_ = edited_source_->size();
}

void InspectorShaderEditorModel::TrimHistoryLocked() {
  while (history_.size() > 1U &&
         (history_.size() > kMaxHistoryEntries ||
          history_bytes_ > kMaxHistoryBytes)) {
    history_bytes_ -= history_.front()->size();
    history_.erase(history_.begin());
    --history_cursor_;
  }
}

void InspectorShaderEditorModel::CancelCompileLocked() noexcept {
  if (pending_compile_.has_value()) {
    compile_cancellation_.request_stop();
    pending_compile_.reset();
  }
}

void InspectorShaderEditorModel::CancelFileOperationsLocked() noexcept {
  if (pending_save_.has_value()) {
    save_cancellation_.request_stop();
    pending_save_.reset();
  }
  if (pending_reload_.has_value()) {
    reload_cancellation_.request_stop();
    pending_reload_.reset();
  }
}

std::string InspectorShaderEditorModel::FormatSuccessStatus(
    const InspectorShaderCompileStatistics& statistics) const {
  if (stage_ == InspectorShaderStage::kVertex) {
    return "(ARB) " + std::to_string(statistics.instructions) + " instrs, " +
           std::to_string(statistics.temporaries) + " temps, " +
           std::to_string(statistics.parameters) + " params, " +
           std::to_string(statistics.attributes) + " attrs, " +
           std::to_string(statistics.address_registers) + " addrs";
  }
  return "(ARB) " + std::to_string(statistics.alu_instructions) +
         " ALU instrs, " + std::to_string(statistics.texture_instructions) +
         " TEX instrs, " + std::to_string(statistics.texture_indirections) +
         " TEX indirect";
}

std::vector<InspectorShaderHighlight>
InspectorShaderEditorModel::ParseHighlights(
    const InspectorShaderBackendCompileResult& result,
    const std::string_view edited_source) const {
  std::vector<InspectorShaderHighlight> highlights;
  if (const auto line = ParseArbErrorLine(result.arb_log); line.has_value()) {
    highlights.push_back({
        .language = InspectorShaderLanguage::kArb,
        .reported_line = *line,
        .range = RetailShaderLineRange(edited_source, *line,
                                       arb_starting_offset_),
    });
  }
  if (const auto location = ParseGlslErrorLine(result.glsl_log);
      location.has_value()) {
    highlights.push_back({
        .language = InspectorShaderLanguage::kGlsl,
        .reported_line = location->line,
        .reported_source_index = location->source_index,
        .range = RetailShaderLineRange(edited_source, location->line, 0),
    });
  }
  return highlights;
}

}
