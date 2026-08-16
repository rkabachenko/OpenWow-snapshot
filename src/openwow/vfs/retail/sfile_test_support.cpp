#include "openwow/vfs/retail/sfile_test_support.h"

#include "openwow/vfs/retail/archive_registry.h"
#include "openwow/vfs/retail/file_stack/file_stack_provider.h"
#include "openwow/vfs/retail/runtime_file.h"
#include "openwow/vfs/retail/runtime_file_registry.h"
#include "openwow/vfs/retail/sfile_archive.h"
#include "openwow/vfs/retail/sfile_runtime.h"
#include "openwow/vfs/retail/streaming/streaming_file_adapter.h"

namespace openwow::vfs {

void ResetSFileRuntimeHandlesForTests() { RetailRuntimeFileRegistry().ResetForTests(); }
void ResetWrappedArchiveHandlesForTests() { RetailArchiveRegistry().ResetForTests(); }
std::size_t GetWrappedArchiveHandleCountForTests() { return RetailArchiveRegistry().SizeForTests(); }
bool IsWrappedArchiveHandleRegisteredForTests(std::uint32_t token) {
  return token != 0 && RetailArchiveRegistry().Contains(token);
}
std::vector<std::uint32_t> GetWrappedArchiveTokensInStockOrderForTests() {
  return RetailArchiveRegistry().TokensInStockOrderForTests();
}
bool WithRetainedWrappedArchiveForTests(
    const void *archive,
    const std::function<void(void *, const std::string &)> &visitor) {
  const auto *handle = static_cast<const SArchiveHandle *>(archive);
  return visitor && handle && handle->type == 0 &&
         RetailArchiveRegistry().VisitRawArchive(handle->archive_token, false, visitor);
}
bool WithLockedRetainedWrappedArchiveForTests(
    const void *archive,
    const std::function<void(void *, const std::string &)> &visitor) {
  const auto *handle = static_cast<const SArchiveHandle *>(archive);
  return visitor && handle && handle->type == 0 &&
         RetailArchiveRegistry().VisitRawArchive(handle->archive_token, true, visitor);
}
bool ReadArchiveListFileContentsForTests(const char *archive, std::string *contents) {
  return ReadRetailArchiveListFile(archive, contents);
}
void SetRawArchiveOpenHookForTests(
    std::function<bool(const char *, std::int32_t, std::uint32_t, void **)> hook) {
  RetailArchiveRegistry().SetRawOpenHookForTests(std::move(hook));
}
void SetRawArchivePatchHookForTests(std::function<bool(void *, const char *, const char *)> hook) {
  RetailArchiveRegistry().SetRawPatchHookForTests(std::move(hook));
}
void SetRawArchiveCloseHookForTests(std::function<bool(void *)> hook) {
  RetailArchiveRegistry().SetRawCloseHookForTests(std::move(hook));
}
void ResetRawArchiveHooksForTests() { RetailArchiveRegistry().ResetRawHooksForTests(); }
bool OpenLooseFileHandleForTests(const char *path, const char *mode, int *out_handle) {
  return OpenLooseFileHandle(path, mode, out_handle);
}
bool OpenStreamingPartBackingHandleForTests(const char *path, std::uint32_t flags,
                                            int *out_handle) {
  return TryOpenStreamingPartBackingHandle(GetActiveFileStackCallbackTable(), path, flags,
                                            out_handle);
}
bool QueryRuntimeSFileHandleNativePositionForTests(int handle, std::int64_t *out_position) {
  if (!out_position) return false;
  const auto file = RetailRuntimeFileRegistry().LookupRetained(handle);
  return file && file->handle.critical_section && file->file_io &&
         file->QueryNativePosition(out_position);
}
void ResetSFileReadFatalFlagForTests() { RuntimeFile::ResetFatalReadFlagForTests(); }

}
