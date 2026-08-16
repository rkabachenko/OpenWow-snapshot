#pragma once

#include "openwow/vfs/retail/sfile_types.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace openwow::vfs {

void ResetSFileRuntimeHandlesForTests();
void ResetWrappedArchiveHandlesForTests();
std::size_t GetWrappedArchiveHandleCountForTests();
bool IsWrappedArchiveHandleRegisteredForTests(std::uint32_t token);
std::vector<std::uint32_t> GetWrappedArchiveTokensInStockOrderForTests();
bool WithRetainedWrappedArchiveForTests(
    const void *archive,
    const std::function<void(void *, const std::string &)> &visitor);
bool WithLockedRetainedWrappedArchiveForTests(
    const void *archive,
    const std::function<void(void *, const std::string &)> &visitor);
bool ReadArchiveListFileContentsForTests(const char *archive_name, std::string *out_contents);
void SetRawArchiveOpenHookForTests(
    std::function<bool(const char *, std::int32_t, std::uint32_t, void **)> hook);
void SetRawArchivePatchHookForTests(std::function<bool(void *, const char *, const char *)> hook);
void SetRawArchiveCloseHookForTests(std::function<bool(void *)> hook);
void ResetRawArchiveHooksForTests();
void SetSFileReadPrologueHookForTests(std::function<void()> hook);
void ResetSFileReadPrologueHookForTests();
bool OpenLooseFileHandleForTests(const char *native_path, const char *mode, int *out_handle);
bool OpenStreamingPartBackingHandleForTests(const char *logical_path, std::uint32_t open_flags,
                                            int *out_handle);
bool QueryRuntimeSFileHandleNativePositionForTests(int handle, std::int64_t *out_position);
void ResetSFileReadFatalFlagForTests();

}
