#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

namespace openwow::platform {

using LogWriterFn = int (*)(int handle, const char* fmt, ...);

namespace detail {

struct ModuleDebugInfo {
  std::array<char, 64>     pdb_file_name{};
  std::uint32_t            guid_data1 = 0;
  std::uint16_t            guid_data2 = 0;
  std::uint16_t            guid_data3 = 0;
  std::array<std::uint8_t, 8> guid_data4{};
  std::uint32_t            age = 0;
  std::uint32_t            timestamp = 0;
  std::uint32_t            stamped_version = 0;
};

struct LoadedModuleEntry {
  std::array<char, 256> module_name{};
  std::uint32_t         base_addr = 0;
  std::uint32_t         module_size = 0;
};

struct CrashReportCurrentContext {
  std::uintptr_t instruction_pointer = 0;
  std::uintptr_t frame_pointer = 0;
  std::uintptr_t stack_pointer = 0;
};

struct CrashReportDispatchState {
  int effective_flags = 0;
  CrashReportCurrentContext context{};
  std::uint32_t skip_frames = 0;
};

const char* FindModuleDebugPathLeafName(const char* path, std::size_t max_length);

ModuleDebugInfo ReadModuleDebugInfo(const void* module_base, std::size_t module_size);

bool ResolveAddressToImageSection(const void* module_base, std::size_t module_size,
                                  const void* address, std::uint32_t* section_index,
                                  std::uint32_t* section_offset);

void SortLoadedModuleEntries(LoadedModuleEntry* entries, std::size_t count);

void NormalizeLoadedModulePathInPlace(char* module_name);

std::string BuildModuleInfoLogLine(std::uint32_t base_addr, std::uint32_t module_size,
                                   const char* module_path, const ModuleDebugInfo& info);

CrashReportDispatchState BuildCrashReportDispatchState(
    int flags, std::uint32_t skip_frames, const void* context_record,
    const CrashReportCurrentContext& current_context);

struct CustomLogEntry {
  std::uint32_t reserved[4]{};
  std::int32_t  field_a = 0;
  std::int32_t  field_b = 0;
  std::uint32_t reserved2[2]{};
  std::uint32_t packed_line = 0;
  char          filename[260]{};
};

struct LogWriterContext {
  LogWriterFn   writer = nullptr;
  int           handle = 0;
};

}

int FormatCustomLogEntryCallback(detail::LogWriterContext* ctx,
                                 const detail::CustomLogEntry* entry);

void WriteLogSectionHeader(LogWriterFn writer, int handle, const char* title);

void HexDumpMemory(const uint8_t* addr, uint32_t size, LogWriterFn writer,
                   int handle, int show_pointer_marker);

const char* FileNameFromPath(const char* path);

int WriteLogExePath(LogWriterFn writer, int handle);

void WriteLogTimestamp(const uint16_t* system_time, LogWriterFn writer, int handle);

void WriteLogUsername(LogWriterFn writer, int handle);

void WriteLogComputerName(LogWriterFn writer, int handle);

void DumpMemory(LogWriterFn writer, int handle, int flags,
                const uint8_t* eip, const uint8_t* esp);

#if defined(_WIN32)
void ResolveAddressToModule(const void* addr, char* module_path, int max_len,
                            uint32_t* out_section, uint32_t* out_offset);
#endif

#if defined(_WIN32)
int SymGetInfo(uint32_t addr, char* module_name, char* symbol_name,
               uint32_t* symbol_offset, char* source_file, uint32_t* source_line);
#endif

#if defined(_WIN32)
void StackFrame_Symbolicate(const uint32_t* frame, int flags,
                            LogWriterFn writer, int handle);
#endif

#if defined(_WIN32)
int SymInit(void* process_handle);
#endif

void WriteLogCustomInfo(LogWriterFn writer, int handle);

#if defined(_WIN32)
void EnumerateThreads(uint32_t* out_count, uint32_t* thread_ids,
                      uint32_t max_threads, uint32_t* total_count);
#endif

#if defined(_WIN32)
void StackWalk_Loop(uint32_t* frame, void* process, void* thread,
                    int flags, LogWriterFn writer, int handle,
                    void* context_record, uint32_t skip_frames);
#endif

#if defined(_WIN32)
void CollectStackTrace_DbgHelp(int flags, LogWriterFn writer, int handle,
                               const void* eip, const void* ebp,
                               const void* esp, uint32_t skip_frames);
#endif

#if defined(_WIN32)
void CollectStackTrace_Manual(LogWriterFn writer, int handle,
                              const void* eip, void* const* ebp,
                              uint32_t skip_frames);
#endif

#if defined(_WIN32)
void DumpRegisters(int handle, LogWriterFn writer, const uint32_t* context);
#endif

int InitDbgHelp();

void ReleaseDbgHelp();

#if defined(_WIN32)
void WriteErrorLogHeader(int flags, LogWriterFn writer, int handle,
                         const char* error_text, const void* system_time);
#endif

#if defined(_WIN32)

unsigned long __stdcall WriteMiniDump_ThreadProc(void* parameter);
#endif

#if defined(_WIN32)
int WriteMiniDump(int file_handle, void* exception_pointers,
                  int custom_stream_count, const char** custom_streams);
#endif

bool IsMiniDumpAvailable();

#if defined(_WIN32)
void WriteModuleInfo(LogWriterFn writer, int handle, uint32_t base_addr,
                     const char* module_path, uint32_t module_size);
#endif

#if defined(_WIN32)
void CollectLoadedModules(LogWriterFn writer, int handle);
#endif

#if defined(_WIN32)
void CollectCrashReportData(int flags, LogWriterFn writer, int handle,
                            int skip_frames, const void* context_record);
#endif

}
