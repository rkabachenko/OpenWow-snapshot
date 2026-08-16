
#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <string>
#include "openwow/foundation/compiler/printf_format.h"

namespace openwow::core {

class FatalErrorIntercepted : public std::runtime_error {
public:
  FatalErrorIntercepted(std::uint32_t message_id, std::string message);

  [[nodiscard]] std::uint32_t message_id() const noexcept;

private:
  std::uint32_t message_id_{0};
};

void SErrSetLastError(int code);

int SErrGetLastError();

int GetStormLastError();

void StormSetLastError(int code);

const char *SErrGetLocalizedString(int index);

void SErrRegisterModule(int16_t moduleId);

bool SErrGetErrorString(std::uint32_t messageId, char *buffer, std::size_t bufferSize);

void SErrEnterCritical(void *criticalSection);

void SErrPrepareAppAbort(const char *file, int line);

void InitMinidumpSettings();

bool IsDebuggerAttached();

std::FILE *CreateErrorFilePath(const struct tm *timeInfo, char *pathOut,
                               const char *typeName, const char *extension,
                               int maxPath);

std::FILE *CreateErrorLogFile(const struct tm *timeInfo, const char *typeName,
                              const char *errorText);

bool WriteTextToLogFile(std::FILE *file, const char *text);

std::string NormalizeTextForErrorLog(const char *text);

OPENWOW_PRINTF_FORMAT(1, 0) std::string FormatErrorLogLine(const char *format,
                                                                       std::va_list args);

const char *SErrGetErrorTitle(uint32_t errorCode);

void ExceptionCodeToString(char *buf, uint32_t bufSize, uint32_t exceptionCode);

void SetApplicationName(const char *name);

void SetCrashDumpCallback(void *callback);

bool SErrGetLastLogPath(char *buf, int bufSize);

bool ParseDecoratedName(int maxLen, char *output, const char *decorated);

void RegisterCrashCallback(void *callback);

void SErrShutdown();

void SErrAssertHandler(const char *expression, const char *message, const char *file, int line);

int SErrDisplayError(uint32_t messageId, const char *source, int exitCode, const char *extra,
                     int canRetry, uint32_t exitParam, int reserved);

bool IsSErrDisplayErrorActive();

[[noreturn]] OPENWOW_PRINTF_FORMAT(2, 0) void SErrFatalError(
    uint32_t messageId, const char *format, va_list args);

[[noreturn]] OPENWOW_PRINTF_FORMAT(2, 3) void SErrFatalError_VArgs(
    uint32_t messageId, const char *format, ...);

[[noreturn]] OPENWOW_PRINTF_FORMAT(1, 2) void SErrFatalCondition(
    const char *format, ...);

OPENWOW_PRINTF_FORMAT(6, 7) int SErrDisplayError_Fmt(
    uint32_t messageId, std::intptr_t source, int exitCode, int canRetry,
    uint32_t exitParam, const char *format, ...);

void SetExceptionFilter();

[[noreturn]] void ExitWithCode(int code);

[[noreturn]] void ExitProcessWithCode(int code);

int GetLastExitCode();

void CheckHandleRelease(const char *handleName, const char *context);

void SetFatalErrorInterceptorForTests(
    std::function<void(std::uint32_t, const std::string &)> interceptor);

namespace detail {

using SErrAssertSignalHandler = void (*)();
using SErrLocalizedStringLoader = bool (*)(std::uint32_t resource_id, char *buffer,
                                           std::size_t buffer_size);
using DebuggerAttachmentLibraryLoader = void *(*)();
using DebuggerAttachmentProbe = int (*)();
using DebuggerAttachmentProbeResolver = DebuggerAttachmentProbe (*)(void *module,
                                                                   const char *name);
using DebuggerAttachmentLibraryReleaser = void (*)(void *module);

void SetSErrAssertDebugBreakHandlerForTests(SErrAssertSignalHandler handler);
void SetSErrAssertAbortHandlerForTests(SErrAssertSignalHandler handler);
void SetSErrAssertAbortSuppressedForTests(bool suppressed);
void ResetSErrAssertHandlersForTests();
void SetSErrLocalizedStringLoaderForTests(SErrLocalizedStringLoader loader);
void ResetSErrLocalizedStringLoaderForTests();
void SetDebuggerAttachmentApiForTests(DebuggerAttachmentLibraryLoader loader,
                                      DebuggerAttachmentProbeResolver resolver,
                                      DebuggerAttachmentLibraryReleaser releaser);
void ResetDebuggerAttachmentApiForTests();

void *SErrStateCriticalSectionTokenForTests();
void *SErrDisplayCriticalSectionTokenForTests();
void SErrLeaveCriticalForTests(void *criticalSection);
void ClearRegisteredCrashCallbacksForTests();
int SErrCriticalInitCounterForTests();
std::size_t RegisteredErrorModuleCountForTests();

void SetSuppressDuplicateErrorsForTests(bool enabled);
void ResetSuppressDuplicateErrorStateForTests();

}

}
