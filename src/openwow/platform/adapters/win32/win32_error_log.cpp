
#include "openwow/platform/adapters/win32/win32_error_log.h"

#include "openwow/platform/process/os_platform.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>

#if defined(_WIN32)

#  include <windows.h>

#  include <dbghelp.h>
#  include <psapi.h>
#  include <tlhelp32.h>
#  include <winternl.h>
#  pragma comment(lib, "dbghelp.lib")
#  pragma comment(lib, "psapi.lib")
#else
#  include <unistd.h>
#endif

namespace openwow::platform {

#if defined(_WIN32)
static LONG volatile s_reentrant_guard = 0;
static int s_dbghelp_refcount = 0;
#endif

namespace {

constexpr std::uint32_t kCodeViewSignatureNb10 = 0x3031424Eu;
constexpr std::uint32_t kCodeViewSignatureRsds = 0x53445352u;
constexpr std::uint16_t kDosSignatureMZ = 0x5A4Du;
constexpr std::uint32_t kPeSignature = 0x00004550u;
constexpr std::uint16_t kPe32Magic = 0x010Bu;
constexpr std::uint16_t kPe32PlusMagic = 0x020Bu;
constexpr std::uint16_t kStampedVersionMarker0 = 0xB74Fu;
constexpr std::uint16_t kStampedVersionMarker1 = 0x2D98u;
constexpr std::uint32_t kStampedVersionMultiplier = 0xFDCA75BBu;
constexpr std::size_t kImageDebugDirectorySize = 0x1Cu;
#if defined(_WIN32)

constexpr std::size_t kMaxLoggedModules = 256u;
#endif
constexpr std::size_t kNb10PathSearchLimit = 0x40u;
constexpr std::size_t kPeFileHeaderNumberOfSectionsOffset = 0x06u;
constexpr std::size_t kPeFileHeaderSizeOfOptionalHeaderOffset = 0x14u;
constexpr std::size_t kPeSectionHeaderSize = 0x28u;
constexpr std::size_t kPeSectionHeadersOffset = 0x18u;
constexpr std::size_t kPeSectionVirtualSizeOffset = 0x08u;
constexpr std::size_t kPeSectionVirtualAddressOffset = 0x0Cu;
constexpr std::size_t kPeSectionRawSizeOffset = 0x10u;
constexpr std::size_t kRsdsPathSearchLimit = 0x104u;
constexpr std::size_t kX86ContextEbpOffset = 0xB4u;
constexpr std::size_t kX86ContextEipOffset = 0xB8u;
constexpr std::size_t kX86ContextEspOffset = 0xC4u;

#if defined(_WIN32)
struct PebLoadedModuleEntry {
    LIST_ENTRY in_load_order_links;
    LIST_ENTRY in_memory_order_links;
    LIST_ENTRY in_initialization_order_links;
    void* dll_base;
    void* entry_point;
    ULONG size_of_image;
    UNICODE_STRING full_dll_name;
    UNICODE_STRING base_dll_name;
};
#endif

template <typename T>
bool ReadImageValue(const std::uint8_t* image, const std::size_t image_size,
                    const std::size_t offset, T& value) {
    if (offset > image_size || sizeof(T) > image_size - offset) {
        return false;
    }

    std::memcpy(&value, image + offset, sizeof(T));
    return true;
}

bool IsImageSpanValid(const std::size_t image_size, const std::size_t offset,
                      const std::size_t length) {
    return offset <= image_size && length <= image_size - offset;
}

template <typename T>
bool ReadPossiblyUnboundedImageValue(const std::uint8_t* image, const std::size_t image_size,
                                     const std::size_t offset, T& value) {
    if (image_size == 0u) {
        std::memcpy(&value, image + offset, sizeof(T));
        return true;
    }

    return ReadImageValue(image, image_size, offset, value);
}

template <std::size_t N>
void CopyBoundedCString(std::array<char, N>& destination, const char* source) {
    destination.fill('\0');
    if (source == nullptr || N == 0) {
        return;
    }

    std::size_t index = 0;
    for (; index + 1 < N && source[index] != '\0'; ++index) {
        destination[index] = source[index];
    }
    destination[index] = '\0';
}

void ParseCodeViewRecord(const std::uint8_t* image, const std::size_t image_size,
                         const std::size_t record_offset, const std::uint32_t record_size,
                         detail::ModuleDebugInfo& info) {
    std::uint32_t signature = 0;
    if (!ReadImageValue(image, image_size, record_offset, signature)) {
        return;
    }

    if (signature == kCodeViewSignatureNb10 && record_size >= 0x14u) {
        std::uint32_t timestamp = 0;
        std::uint32_t age = 0;
        if (!ReadImageValue(image, image_size, record_offset + 8u, timestamp)
            || !ReadImageValue(image, image_size, record_offset + 12u, age)
            || record_offset + 16u > image_size) {
            return;
        }

        info.timestamp = timestamp;
        info.age = age;

        const std::size_t available_bytes = image_size - (record_offset + 16u);
        const std::size_t search_limit = std::min(kNb10PathSearchLimit, available_bytes);
        const char* const path =
            reinterpret_cast<const char*>(image + record_offset + 16u);
        CopyBoundedCString(info.pdb_file_name,
                           detail::FindModuleDebugPathLeafName(path, search_limit));
        return;
    }

    if (signature != kCodeViewSignatureRsds || record_size < 0x1Cu) {
        return;
    }

    if (!IsImageSpanValid(image_size, record_offset + 4u, 24u)) {
        return;
    }

    std::memcpy(&info.guid_data1, image + record_offset + 4u, sizeof(info.guid_data1));
    std::memcpy(&info.guid_data2, image + record_offset + 8u, sizeof(info.guid_data2));
    std::memcpy(&info.guid_data3, image + record_offset + 10u, sizeof(info.guid_data3));
    std::memcpy(info.guid_data4.data(), image + record_offset + 12u, info.guid_data4.size());
    std::memcpy(&info.age, image + record_offset + 20u, sizeof(info.age));

    const std::size_t available_bytes = image_size - (record_offset + 24u);
    const std::size_t search_limit = std::min(kRsdsPathSearchLimit, available_bytes);
    const char* const path =
        reinterpret_cast<const char*>(image + record_offset + 24u);
    CopyBoundedCString(info.pdb_file_name,
                       detail::FindModuleDebugPathLeafName(path, search_limit));
}

void ParseDebugDirectoryTable(const std::uint8_t* image, const std::size_t image_size,
                              const std::size_t table_offset, const std::uint32_t table_size,
                              detail::ModuleDebugInfo& info) {
    if (!IsImageSpanValid(image_size, table_offset, table_size) || table_size < kImageDebugDirectorySize) {
        return;
    }

    const std::size_t entry_count = table_size / kImageDebugDirectorySize;
    for (std::size_t index = 0; index < entry_count; ++index) {
        const std::size_t entry_offset = table_offset + index * kImageDebugDirectorySize;
        std::uint32_t type = 0;
        std::uint32_t size_of_data = 0;
        std::uint32_t address_of_raw_data = 0;
        if (!ReadImageValue(image, image_size, entry_offset + 12u, type)
            || !ReadImageValue(image, image_size, entry_offset + 16u, size_of_data)
            || !ReadImageValue(image, image_size, entry_offset + 20u, address_of_raw_data)) {
            return;
        }

        if (type != 2u) {
            continue;
        }

        if (address_of_raw_data >= image_size) {
            return;
        }

        ParseCodeViewRecord(image, image_size, address_of_raw_data, size_of_data, info);
        return;
    }
}

void StampModuleVersion(const std::uint8_t* image, const std::size_t image_size,
                        detail::ModuleDebugInfo& info) {
    std::uint16_t marker0 = 0;
    std::uint16_t marker1 = 0;
    std::uint16_t version_low = 0;
    std::uint16_t version_high = 0;
    if (!ReadImageValue(image, image_size, 0x38u, marker0)
        || !ReadImageValue(image, image_size, 0x3Au, marker1)
        || marker0 != kStampedVersionMarker0
        || marker1 != kStampedVersionMarker1
        || !ReadImageValue(image, image_size, 0x34u, version_low)
        || !ReadImageValue(image, image_size, 0x36u, version_high)) {
        return;
    }

    const std::uint32_t stamped_value =
        static_cast<std::uint32_t>(version_low)
        | (static_cast<std::uint32_t>(version_high) << 16u);
    info.stamped_version = stamped_value;
    if (stamped_value != 0u) {
        info.stamped_version *= kStampedVersionMultiplier;
    }
}

}

namespace detail {

const char* FindModuleDebugPathLeafName(const char* path, const std::size_t max_length) {
    static constexpr char kEmptyString[] = "";
    if (path == nullptr) {
        return kEmptyString;
    }

    if (std::memchr(path, '\0', max_length) == nullptr) {
        return kEmptyString;
    }

    const char* last_forward_slash = std::strrchr(path, '/');
    const char* last_back_slash = std::strrchr(path, '\\');
    if (last_forward_slash == nullptr || (last_back_slash != nullptr && last_forward_slash < last_back_slash)) {
        last_forward_slash = last_back_slash;
    }

    return last_forward_slash != nullptr ? last_forward_slash + 1 : path;
}

ModuleDebugInfo ReadModuleDebugInfo(const void* module_base, const std::size_t module_size) {
    ModuleDebugInfo info{};
    const auto* const image = static_cast<const std::uint8_t*>(module_base);
    if (image == nullptr || module_size < 0x40u) {
        return info;
    }

    std::uint16_t dos_signature = 0;
    if (!ReadImageValue(image, module_size, 0u, dos_signature) || dos_signature != kDosSignatureMZ) {
        return info;
    }

    StampModuleVersion(image, module_size, info);

    std::uint32_t pe_offset = 0;
    std::uint32_t pe_signature = 0;
    if (!ReadImageValue(image, module_size, 0x3Cu, pe_offset)
        || !ReadImageValue(image, module_size, pe_offset, pe_signature)
        || pe_signature != kPeSignature) {
        return info;
    }

    std::uint16_t optional_magic = 0;
    if (!ReadImageValue(image, module_size, pe_offset + 24u, optional_magic)) {
        return info;
    }

    if (optional_magic == kPe32Magic) {
        std::uint32_t number_of_rva_and_sizes = 0;
        std::uint32_t debug_table_rva = 0;
        std::uint32_t debug_table_size = 0;
        if (!ReadImageValue(image, module_size, pe_offset + 116u, number_of_rva_and_sizes)
            || number_of_rva_and_sizes < 7u
            || !ReadImageValue(image, module_size, pe_offset + 168u, debug_table_rva)
            || !ReadImageValue(image, module_size, pe_offset + 172u, debug_table_size)) {
            return info;
        }

        ParseDebugDirectoryTable(image, module_size, debug_table_rva, debug_table_size, info);
        return info;
    }

    if (optional_magic == kPe32PlusMagic) {
        std::uint32_t number_of_rva_and_sizes = 0;
        std::uint32_t debug_table_rva = 0;
        std::uint32_t debug_table_size = 0;
        if (!ReadImageValue(image, module_size, pe_offset + 132u, number_of_rva_and_sizes)
            || number_of_rva_and_sizes < 7u
            || !ReadImageValue(image, module_size, pe_offset + 184u, debug_table_rva)
            || !ReadImageValue(image, module_size, pe_offset + 188u, debug_table_size)) {
            return info;
        }

        ParseDebugDirectoryTable(image, module_size, debug_table_rva, debug_table_size, info);
    }

    return info;
}

bool ResolveAddressToImageSection(const void* module_base, const std::size_t module_size,
                                  const void* address, std::uint32_t* section_index,
                                  std::uint32_t* section_offset) {
    if (section_index != nullptr) {
        *section_index = 0;
    }
    if (section_offset != nullptr) {
        *section_offset = 0;
    }
    if (module_base == nullptr || address == nullptr || section_index == nullptr
        || section_offset == nullptr) {
        return false;
    }

    const auto* const image = static_cast<const std::uint8_t*>(module_base);

    std::uint16_t dos_signature = 0;
    std::uint32_t pe_offset = 0;
    std::uint32_t pe_signature = 0;
    std::uint16_t section_count = 0;
    std::uint16_t optional_header_size = 0;
    if (!ReadPossiblyUnboundedImageValue(image, module_size, 0u, dos_signature)
        || dos_signature != kDosSignatureMZ
        || !ReadPossiblyUnboundedImageValue(image, module_size, 0x3Cu, pe_offset)
        || pe_offset == 0u
        || !ReadPossiblyUnboundedImageValue(image, module_size, pe_offset, pe_signature)
        || pe_signature != kPeSignature
        || !ReadPossiblyUnboundedImageValue(
            image, module_size, pe_offset + kPeFileHeaderNumberOfSectionsOffset, section_count)
        || !ReadPossiblyUnboundedImageValue(
            image, module_size, pe_offset + kPeFileHeaderSizeOfOptionalHeaderOffset,
            optional_header_size)) {
        return false;
    }

    const std::size_t section_table_offset =
        static_cast<std::size_t>(pe_offset) + kPeSectionHeadersOffset + optional_header_size;
    const auto module_base_value = reinterpret_cast<std::uintptr_t>(module_base);
    const auto address_value = reinterpret_cast<std::uintptr_t>(address);
    const std::uint32_t rva = static_cast<std::uint32_t>(address_value - module_base_value);

    for (std::uint32_t section_index_value = 0; section_index_value < section_count;
         ++section_index_value) {
        const std::size_t header_offset =
            section_table_offset + section_index_value * kPeSectionHeaderSize;
        std::uint32_t virtual_size = 0;
        std::uint32_t virtual_address = 0;
        std::uint32_t raw_size = 0;
        if (!ReadPossiblyUnboundedImageValue(
                image, module_size, header_offset + kPeSectionVirtualSizeOffset, virtual_size)
            || !ReadPossiblyUnboundedImageValue(
                image, module_size, header_offset + kPeSectionVirtualAddressOffset,
                virtual_address)
            || !ReadPossiblyUnboundedImageValue(
                image, module_size, header_offset + kPeSectionRawSizeOffset, raw_size)) {
            return false;
        }

        const std::uint32_t section_size = std::max(virtual_size, raw_size);
        if (rva < virtual_address || rva > virtual_address + section_size) {
            continue;
        }

        *section_index = section_index_value + 1u;
        *section_offset = rva - virtual_address;
        return true;
    }

    return false;
}

void SortLoadedModuleEntries(LoadedModuleEntry* entries, const std::size_t count) {
    if (entries == nullptr || count < 2u) {
        return;
    }

    std::sort(entries, entries + count,
              [](const LoadedModuleEntry& lhs, const LoadedModuleEntry& rhs) {
                  return lhs.base_addr < rhs.base_addr;
              });
}

void NormalizeLoadedModulePathInPlace(char* module_name) {
    if (module_name == nullptr) {
        return;
    }

    char* last_forward_slash = std::strrchr(module_name, '/');
    char* last_back_slash = std::strrchr(module_name, '\\');
    if (last_forward_slash == nullptr
        || (last_back_slash != nullptr && last_forward_slash < last_back_slash)) {
        last_forward_slash = last_back_slash;
    }

    if (last_forward_slash != nullptr) {
        std::memmove(module_name, last_forward_slash + 1u,
                     std::strlen(last_forward_slash + 1u) + 1u);
    }
}

std::string BuildModuleInfoLogLine(const std::uint32_t base_addr, const std::uint32_t module_size,
                                   const char* module_path, const ModuleDebugInfo& info) {
    char version_suffix[12]{};
    if (info.stamped_version != 0u) {
        std::snprintf(version_suffix, sizeof(version_suffix), " %08lX",
                      static_cast<unsigned long>(info.stamped_version));
    }

    std::array<char, 768> line{};
    std::snprintf(
        line.data(), line.size(),
        "DBG-MODULE<%08llX %08lX \"%s\" \"%s\" %lu {%08lx-%04hx-%04hx-%02x%02x%02x%02x%02x%02x%02x%02x} %lu %lu%s>",
        static_cast<unsigned long long>(base_addr),
        static_cast<unsigned long>(module_size),
        module_path != nullptr ? module_path : "",
        info.pdb_file_name.data(),
        static_cast<unsigned long>(info.age),
        static_cast<unsigned long>(info.guid_data1),
        static_cast<unsigned int>(info.guid_data2),
        static_cast<unsigned int>(info.guid_data3),
        static_cast<unsigned int>(info.guid_data4[0]),
        static_cast<unsigned int>(info.guid_data4[1]),
        static_cast<unsigned int>(info.guid_data4[2]),
        static_cast<unsigned int>(info.guid_data4[3]),
        static_cast<unsigned int>(info.guid_data4[4]),
        static_cast<unsigned int>(info.guid_data4[5]),
        static_cast<unsigned int>(info.guid_data4[6]),
        static_cast<unsigned int>(info.guid_data4[7]),
        static_cast<unsigned long>(info.timestamp),
        static_cast<unsigned long>(info.stamped_version),
        version_suffix);
    return std::string(line.data());
}

CrashReportDispatchState BuildCrashReportDispatchState(
    const int flags, const std::uint32_t skip_frames, const void* context_record,
    const CrashReportCurrentContext& current_context) {
    CrashReportDispatchState state{};
    state.effective_flags = flags;

    if (flags == 0 || (flags & 1) != 0) {
        state.effective_flags |= 0x6A0000;
        if (context_record != nullptr) {
            state.effective_flags |= 0x110000;
        }
    }

    if (context_record != nullptr) {
        std::uint32_t eip = 0;
        std::uint32_t ebp = 0;
        std::uint32_t esp = 0;
        const auto* const bytes = static_cast<const std::uint8_t*>(context_record);

        std::memcpy(&eip, bytes + kX86ContextEipOffset, sizeof(eip));
        std::memcpy(&ebp, bytes + kX86ContextEbpOffset, sizeof(ebp));
        std::memcpy(&esp, bytes + kX86ContextEspOffset, sizeof(esp));

        state.context.instruction_pointer = static_cast<std::uintptr_t>(eip);
        state.context.frame_pointer = static_cast<std::uintptr_t>(ebp);
        state.context.stack_pointer = static_cast<std::uintptr_t>(esp);
        return state;
    }

    state.effective_flags &= ~0x110000;
    state.context = current_context;
    state.skip_frames = skip_frames + 1u;
    return state;
}

}

#if defined(_WIN32)
namespace {

detail::CrashReportCurrentContext CaptureCurrentCrashReportContext() {
    detail::CrashReportCurrentContext context{};

#if defined(_M_IX86) && defined(_MSC_VER)
    unsigned long instruction_pointer = 0;
    unsigned long frame_pointer = 0;
    unsigned long stack_pointer = 0;
    __asm {
        call capture_label
    capture_label:
        pop eax
        mov instruction_pointer, eax
        mov eax, ebp
        mov frame_pointer, eax
        mov eax, esp
        mov stack_pointer, eax
    }
    context.instruction_pointer = static_cast<std::uintptr_t>(instruction_pointer);
    context.frame_pointer = static_cast<std::uintptr_t>(frame_pointer);
    context.stack_pointer = static_cast<std::uintptr_t>(stack_pointer);
#elif defined(__i386__) && (defined(__GNUC__) || defined(__clang__))
    std::uintptr_t instruction_pointer = 0;
    std::uintptr_t frame_pointer = 0;
    std::uintptr_t stack_pointer = 0;
    asm volatile(
        "call 1f\n"
        "1: pop %0\n"
        "mov %%ebp, %1\n"
        "mov %%esp, %2\n"
        : "=r"(instruction_pointer), "=r"(frame_pointer), "=r"(stack_pointer)
        :
        : "memory");
    context.instruction_pointer = instruction_pointer;
    context.frame_pointer = frame_pointer;
    context.stack_pointer = stack_pointer;
#elif defined(_M_X64)
    CONTEXT captured{};
    RtlCaptureContext(&captured);
    context.instruction_pointer = static_cast<std::uintptr_t>(captured.Rip);
    context.frame_pointer = static_cast<std::uintptr_t>(captured.Rbp);
    context.stack_pointer = static_cast<std::uintptr_t>(captured.Rsp);
#elif defined(_M_ARM64)
    CONTEXT captured{};
    RtlCaptureContext(&captured);
    context.instruction_pointer = static_cast<std::uintptr_t>(captured.Pc);
    context.frame_pointer = static_cast<std::uintptr_t>(captured.Fp);
    context.stack_pointer = static_cast<std::uintptr_t>(captured.Sp);
#endif

    return context;
}

void PopulateLoadedModuleEntry(detail::LoadedModuleEntry& destination,
                               const std::uint32_t base_addr,
                               const std::uint32_t module_size,
                               const char* module_name) {
    destination = {};
    destination.base_addr = base_addr;
    destination.module_size = module_size;
    if (module_name == nullptr) {
        return;
    }

    std::snprintf(destination.module_name.data(), destination.module_name.size(), "%s",
                  module_name);
}

bool TryCollectToolhelpLoadedModules(std::array<detail::LoadedModuleEntry, kMaxLoggedModules>& modules,
                                     std::size_t* count, DWORD* last_error) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        if (last_error != nullptr) {
            *last_error = GetLastError();
        }
        return false;
    }

    MODULEENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    if (Module32First(snapshot, &entry)) {
        do {
            if (*count >= modules.size()) {
                break;
            }

            PopulateLoadedModuleEntry(
                modules[*count],
                static_cast<std::uint32_t>(
                    reinterpret_cast<std::uintptr_t>(entry.modBaseAddr)),
                static_cast<std::uint32_t>(entry.modBaseSize), entry.szModule);
            ++(*count);
            entry.dwSize = sizeof(entry);
        } while (Module32Next(snapshot, &entry));
    }

    if (last_error != nullptr) {
        *last_error = GetLastError();
    }
    CloseHandle(snapshot);
    return true;
}

void PopulateLoadedModuleNameFromUnicode(detail::LoadedModuleEntry& destination,
                                         const UNICODE_STRING& full_name) {
    destination.module_name.fill('\0');
    if (full_name.Buffer == nullptr || full_name.Length == 0u) {
        return;
    }

    const int source_length = static_cast<int>(full_name.Length / sizeof(wchar_t));
    const int converted_length = WideCharToMultiByte(
        CP_ACP, 0, full_name.Buffer, source_length, destination.module_name.data(),
        static_cast<int>(destination.module_name.size() - 1u), nullptr, nullptr);
    if (converted_length <= 0) {
        destination.module_name[0] = '\0';
        return;
    }

    destination.module_name[static_cast<std::size_t>(converted_length)] = '\0';
    detail::NormalizeLoadedModulePathInPlace(destination.module_name.data());
}

void CollectLoadedModulesFromPeb(std::array<detail::LoadedModuleEntry, kMaxLoggedModules>& modules,
                                 std::size_t* count) {
    if (GetModuleHandleW(L"ntdll.dll") == nullptr) {
        return;
    }

    const PPEB peb = NtCurrentTeb()->ProcessEnvironmentBlock;
    if (peb == nullptr || peb->Ldr == nullptr) {
        return;
    }

    const LIST_ENTRY* const head = &peb->Ldr->InMemoryOrderModuleList;
    for (LIST_ENTRY* link = head->Flink; link != head && *count < modules.size();
         link = link->Flink) {
        const auto* const module =
            CONTAINING_RECORD(link, PebLoadedModuleEntry, in_memory_order_links);
        auto& destination = modules[*count];
        PopulateLoadedModuleEntry(
            destination,
            static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(module->dll_base)),
            static_cast<std::uint32_t>(module->size_of_image), nullptr);
        PopulateLoadedModuleNameFromUnicode(destination, module->full_dll_name);
        ++(*count);
    }
}

}
#endif

void WriteLogSectionHeader(LogWriterFn writer, int handle, const char* title) {
    char separator[80];
    std::memset(separator, '-', 78);
    separator[78] = '\0';

    writer(handle, "");
    writer(handle, "%s", separator);
    writer(handle, "    %s", title);
    std::memset(separator, '-', 78);
    separator[78] = '\0';
    writer(handle, "%s", separator);
    writer(handle, "");
}

void HexDumpMemory(const uint8_t* addr, uint32_t size, LogWriterFn writer,
                   int handle, int show_pointer_marker) {
    uint32_t num_lines = size >> 4;
    uint32_t start_offset = reinterpret_cast<uintptr_t>(addr) & 0xF;

    if (show_pointer_marker) {
        char marker_line[80];
        std::memset(marker_line, ' ', 78);
        marker_line[0] = '*';
        marker_line[2] = '=';
        std::memcpy(&marker_line[4], "addr", 4);
        marker_line[78] = '\0';

        int col = static_cast<int>(start_offset) * 2 + 10 +
                  static_cast<int>(start_offset) +
                  (static_cast<int>(start_offset) >> 2);
        marker_line[col] = '*';
        marker_line[col + 1] = '*';
        marker_line[start_offset + 62] = '*';

        writer(handle, "%s", marker_line);

        if (start_offset) {
            addr -= start_offset;
            ++num_lines;
        }
    }

    if (!num_lines) return;

    for (uint32_t line = 0; line < num_lines; ++line) {
        char buf[80];
        int pos = std::snprintf(
            buf, sizeof(buf), "%08X: ",
            static_cast<unsigned>(reinterpret_cast<uintptr_t>(addr)));

#if defined(_WIN32)
        if (IsBadReadPtr(addr, 16)) {
            std::strcpy(buf + pos, "<can't read from this address>");
            writer(handle, "%s", buf);
            return;
        }
#endif

        pos += std::snprintf(buf + pos, sizeof(buf) - static_cast<std::size_t>(pos),
                             "%02X %02X %02X %02X  ",
                             addr[0], addr[1], addr[2], addr[3]);
        pos += std::snprintf(buf + pos, sizeof(buf) - static_cast<std::size_t>(pos),
                             "%02X %02X %02X %02X  ",
                             addr[4], addr[5], addr[6], addr[7]);
        pos += std::snprintf(buf + pos, sizeof(buf) - static_cast<std::size_t>(pos),
                             "%02X %02X %02X %02X  ",
                             addr[8], addr[9], addr[10], addr[11]);
        pos += std::snprintf(buf + pos, sizeof(buf) - static_cast<std::size_t>(pos),
                             "%02X %02X %02X %02X  ",
                             addr[12], addr[13], addr[14], addr[15]);

        for (int i = 0; i < 16; ++i) {
            buf[pos + i] = std::isprint(addr[i]) ? static_cast<char>(addr[i]) : '.';
        }
        buf[pos + 16] = '\0';

        writer(handle, "%s", buf);
        addr += 16;
    }
}

const char* FileNameFromPath(const char* path) {
    const char* last = nullptr;

    const char* p = std::strrchr(path, '/');
    if (p) last = p;

    p = std::strrchr(path, '\\');
    if (p && p > last) last = p;

    p = std::strrchr(path, ':');
    if (p && p > last) last = p;

    return last ? last + 1 : path;
}

int WriteLogExePath(LogWriterFn writer, int handle) {
    const std::string exe_path = OS_GetModulePath();
    return writer(handle, "%-10s%s", "Exe:", exe_path.c_str());
}

void WriteLogTimestamp(const uint16_t* system_time, LogWriterFn writer, int handle) {

    static const char* month_names[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };

    uint16_t hour = system_time[4];
    char ampm = (hour < 12) ? 'A' : 'P';
    if (hour > 12) {
        hour -= 12;
    } else if (hour == 0) {
        hour = 12;
    }

    int month_idx = system_time[1];
    if (month_idx < 1) month_idx = 1;
    if (month_idx > 12) month_idx = 12;

    writer(handle, "%-10s%3s %2d, %4d %2d:%02d:%02d.%03d %cM",
           "Time:",
           month_names[month_idx - 1],
           system_time[3],
           system_time[0],
           hour,
           system_time[5],
           system_time[6],
           system_time[7],
           ampm);
}

void WriteLogUsername(LogWriterFn writer, int handle) {
#if defined(_WIN32)
    char buf[260];
    DWORD size = 257;
    if (!GetUserNameA(buf, &size)) {
        std::strcpy(buf, "<unknown>");
    }
    writer(handle, "%-10s%s", "User:", buf);
#else
    const char* user = std::getenv("USER");
    if (!user) user = "<unknown>";
    writer(handle, "%-10s%s", "User:", user);
#endif
}

void WriteLogComputerName(LogWriterFn writer, int handle) {
#if defined(_WIN32)
    char buf[16];
    DWORD size = 16;
    if (!GetComputerNameA(buf, &size)) {
        std::strcpy(buf, "<unknown>");
    }
    writer(handle, "%-10s%s", "Computer:", buf);
#else
    char buf[256] = {};
    if (gethostname(buf, sizeof(buf) - 1) != 0) {
        std::strcpy(buf, "<unknown>");
    }
    writer(handle, "%-10s%s", "Computer:", buf);
#endif
}

void DumpMemory(LogWriterFn writer, int handle, int flags,
                const uint8_t* eip, const uint8_t* esp) {
    WriteLogSectionHeader(writer, handle, "Memory Dump");

    if (flags & 0x100000) {
        writer(handle, "Code: %d bytes starting at (EIP = %08X)", 16,
               static_cast<unsigned>(reinterpret_cast<uintptr_t>(eip)));
        writer(handle, "");
        HexDumpMemory(eip, 16, writer, handle, 0);
        writer(handle, "");
        writer(handle, "");
    }

    if (flags & 0x200000) {
        writer(handle, "Stack: %d bytes starting at (ESP = %08X)", 1024,
               static_cast<unsigned>(reinterpret_cast<uintptr_t>(esp)));
        writer(handle, "");
        HexDumpMemory(esp, 1024, writer, handle, 1);
        writer(handle, "");
        writer(handle, "");
    }
}

#if defined(_WIN32)
void ResolveAddressToModule(const void* addr, char* module_path, int max_len,
                            uint32_t* out_section, uint32_t* out_offset) {
    lstrcpynA(module_path, "<unknown>", max_len);
    *out_offset = 0;
    *out_section = 0;

    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(addr, &mbi, sizeof(mbi))) return;

    HMODULE mod = static_cast<HMODULE>(mbi.AllocationBase);
    if (!mod) mod = GetModuleHandleA(nullptr);

    if (!GetModuleFileNameA(mod, module_path, max_len)) {
        lstrcpynA(module_path, "<unknown>", max_len);
        return;
    }

    if (!mod) return;

    (void)detail::ResolveAddressToImageSection(mod, 0u, addr, out_section, out_offset);
}
#endif

#if defined(_WIN32)
int SymGetInfo(uint32_t addr, char* module_name, char* symbol_name,
               uint32_t* symbol_offset, char* source_file, uint32_t* source_line) {
    int error_flags = 0;

    IMAGEHLP_MODULE64 mod_info;
    std::memset(&mod_info, 0, sizeof(mod_info));
    mod_info.SizeOfStruct = sizeof(mod_info);

    HANDLE proc = GetCurrentProcess();
    if (SymGetModuleInfo64(proc, addr, &mod_info)) {
        const char* name = FileNameFromPath(mod_info.ImageName);
        std::strcpy(module_name, name);
    } else {
        std::strcpy(module_name, "<unknown module>");
        error_flags |= 1;
    }

    *source_file = '\0';
    *source_line = 0;

    char sym_buf[sizeof(IMAGEHLP_SYMBOL64) + 256];
    std::memset(sym_buf, 0, sizeof(sym_buf));
    auto* sym = reinterpret_cast<IMAGEHLP_SYMBOL64*>(sym_buf);
    sym->SizeOfStruct = sizeof(IMAGEHLP_SYMBOL64);
    sym->Address = addr;
    sym->MaxNameLength = 256;

    DWORD64 disp64 = 0;
    if (SymGetSymFromAddr64(proc, addr, &disp64, sym)) {
        std::strncpy(symbol_name, sym->Name, 256);
        symbol_name[255] = '\0';
        *symbol_offset = static_cast<uint32_t>(disp64);

        IMAGEHLP_LINE64 line_info;
        std::memset(&line_info, 0, sizeof(line_info));
        line_info.SizeOfStruct = sizeof(line_info);

        DWORD disp32 = 0;
        if (SymGetLineFromAddr64(proc, addr, &disp32, &line_info)) {
            const char* fname = FileNameFromPath(line_info.FileName);
            std::strncpy(source_file, fname, 259);
            source_file[259] = '\0';
            *source_line = line_info.LineNumber;
        } else {
            error_flags |= 2;
        }
    } else {
        std::strcpy(symbol_name, "<unknown symbol>");
        *symbol_offset = 0;
        error_flags |= 4;
    }

    return error_flags;
}
#endif

#if defined(_WIN32)
void StackFrame_Symbolicate(const uint32_t* frame, int flags,
                            LogWriterFn writer, int handle) {
    uint32_t addr = frame[0];
    char module_name[32];
    char symbol_name[256];
    char source_file[260];
    uint32_t sym_offset;
    uint32_t src_line;

    int err = SymGetInfo(addr, module_name, symbol_name, &sym_offset,
                         source_file, &src_line);

    if ((err & 1) && flags < 0)
        writer(handle, "**** SymGetModuleInfo() failed, error: %d", GetLastError());
    if ((err & 2) && flags < 0)
        writer(handle, "**** SymGetLineFromAddr() failed, error: %d", GetLastError());
    if ((err & 4) && flags < 0)
        writer(handle, "**** SymGetSymFromAddr() failed, error: %d", GetLastError());

    if (flags & 0x40000) {
        if (source_file[0]) {
            writer(handle,
                   "%08X %-12s %s+%d (0x%08X,0x%08X,0x%08X,0x%08X) (%s,%d)",
                   addr, module_name, symbol_name, sym_offset,
                   frame[13], frame[14], frame[15], frame[16],
                   source_file, src_line);
        } else {
            writer(handle,
                   "%08X %-12s %s+%d (0x%08X,0x%08X,0x%08X,0x%08X)",
                   addr, module_name, symbol_name, sym_offset,
                   frame[13], frame[14], frame[15], frame[16]);
        }
    } else {
        if (source_file[0]) {
            writer(handle, "%08X %-12s %s+%d (%s,%d)",
                   addr, module_name, symbol_name, sym_offset,
                   source_file, src_line);
        } else {
            writer(handle, "%08X %-12s %s+%d",
                   addr, module_name, symbol_name, sym_offset);
        }
    }
}
#endif

#if defined(_WIN32)
int SymInit(void* process_handle) {
    const std::string directory = OS_GetModuleDirectory();
    return SymInitialize(static_cast<HANDLE>(process_handle), directory.c_str(), TRUE);
}
#endif

int FormatCustomLogEntryCallback(detail::LogWriterContext* ctx,
                                 const detail::CustomLogEntry* entry) {
    if (!ctx || !ctx->writer || !entry) {
        return 0;
    }

    const int line_number = static_cast<int>(entry->packed_line >> 1);

    return ctx->writer(ctx->handle,
                       "%5d %5d %s(%d)",
                       entry->field_a,
                       entry->field_b,
                       entry->filename,
                       line_number);
}

void WriteLogCustomInfo(LogWriterFn writer, int handle) {

    (void)writer;
    (void)handle;
}

#if defined(_WIN32)
void EnumerateThreads(uint32_t* out_count, uint32_t* thread_ids,
                      uint32_t max_threads, uint32_t* total_count) {
    *out_count = 0;
    *total_count = 0;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        THREADENTRY32 te;
        te.dwSize = sizeof(te);

        if (Thread32First(snap, &te)) {
            DWORD pid = GetCurrentProcessId();
            while (Thread32Next(snap, &te)) {
                if (te.th32ThreadID && te.th32OwnerProcessID == pid) {
                    if (*out_count < max_threads) {
                        thread_ids[(*out_count)++] = te.th32ThreadID;
                    }
                    ++(*total_count);
                }
            }
        }
        CloseHandle(snap);
    }

    if (*out_count == 0) {
        thread_ids[0] = GetCurrentThreadId();
        *out_count = 1;
        *total_count = 1;
    }
}
#endif

#if defined(_WIN32)
void StackWalk_Loop(uint32_t* frame, void* process, void* thread,
                    int flags, LogWriterFn writer, int handle,
                    void* context_record, uint32_t skip_frames) {
    STACKFRAME64 sf;
    std::memset(&sf, 0, sizeof(sf));
    sf.AddrPC.Offset    = frame[0];
    sf.AddrPC.Mode      = AddrModeFlat;
    sf.AddrFrame.Offset = frame[6];
    sf.AddrFrame.Mode   = AddrModeFlat;
    sf.AddrStack.Offset = frame[9];
    sf.AddrStack.Mode   = AddrModeFlat;

    for (uint32_t i = 0; i < 100; ++i) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_I386,
                         static_cast<HANDLE>(process),
                         static_cast<HANDLE>(thread),
                         &sf, context_record,
                         nullptr, SymFunctionTableAccess64,
                         SymGetModuleBase64, nullptr)) {
            if (flags < 0) {
                writer(handle, "**** StackWalk() returned FALSE, error: %d",
                       GetLastError());
            }
            break;
        }

        if (sf.AddrPC.Offset == 0) {
            if (flags < 0)
                writer(handle,
                       "**** StackWalk() returned zero address - skipping stack frame");
            continue;
        }

        if (i >= skip_frames) {
            frame[0] = static_cast<uint32_t>(sf.AddrPC.Offset);
            StackFrame_Symbolicate(frame, flags, writer, handle);
        }
    }
}
#endif

#if defined(_WIN32)
void CollectStackTrace_DbgHelp(int flags, LogWriterFn writer, int handle,
                               const void* eip, const void* ebp,
                               const void* esp, uint32_t skip_frames) {
    WriteLogSectionHeader(writer, handle, "Stack Trace (Using DBGHELP.DLL)");

    HANDLE proc = GetCurrentProcess();
    if (!SymInit(proc)) {
        writer(handle, "****  Couldn't initialize Debug Help library, error: %d",
               GetLastError());
        return;
    }

    if (!(flags & 0x20000)) return;

    uint32_t thread_ids[128];
    uint32_t thread_count = 0;
    uint32_t total_threads = 0;
    EnumerateThreads(&thread_count, thread_ids, 128, &total_threads);

    writer(handle, "Showing %d/%d threads...", thread_count, total_threads);

    DWORD current_tid = GetCurrentThreadId();

    for (uint32_t t = 0; t < thread_count; ++t) {
        DWORD tid = thread_ids[t];
        writer(handle, "");

        if (tid == current_tid) {
            writer(handle, "--- Thread ID: %d [Current Thread] ---", tid);
        } else {
            writer(handle, "--- Thread ID: %d ---", tid);
        }

        HANDLE thread_handle = nullptr;
        if (tid == current_tid) {
            thread_handle = GetCurrentThread();
        } else {
            thread_handle = OpenThread(THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME |
                                       THREAD_QUERY_INFORMATION, FALSE, tid);
        }

        if (!thread_handle) {
            writer(handle, "**** Unable to gain access to the thread, error: %d",
                   GetLastError());
            continue;
        }

        uint32_t frame[20] = {};
        CONTEXT ctx = {};
        uint32_t skip = skip_frames;

        if (tid == current_tid) {
            frame[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(eip));
            frame[6] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ebp));
            frame[9] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(esp));
        } else {
            SuspendThread(thread_handle);
            ctx.ContextFlags = CONTEXT_FULL;
            BOOL got_ctx = GetThreadContext(thread_handle, &ctx);
            ResumeThread(thread_handle);

            if (!got_ctx) {
                writer(handle, "**** Unable to retrieve thread context, error: %d",
                       GetLastError());
                CloseHandle(thread_handle);
                continue;
            }

#if defined(_M_X64) || defined(_M_ARM64) || defined(_WIN64)
            frame[0] = static_cast<uint32_t>(ctx.Rip);
            frame[6] = static_cast<uint32_t>(ctx.Rbp);
            frame[9] = static_cast<uint32_t>(ctx.Rsp);
#else
            frame[0] = ctx.Eip;
            frame[6] = ctx.Ebp;
            frame[9] = ctx.Esp;
#endif
            skip = 1;
        }

        frame[11] = 3;
        frame[8]  = 3;
        frame[2]  = 3;

        StackWalk_Loop(frame, proc, thread_handle, flags, writer, handle,
                       (tid != current_tid) ? &ctx : nullptr, skip);

        CloseHandle(thread_handle);
    }

    writer(handle, "");
}

void CollectStackTrace_Manual(LogWriterFn writer, int handle,
                              const void* eip, void* const* ebp,
                              uint32_t skip_frames) {
    WriteLogSectionHeader(writer, handle, "Stack Trace (Manual)");
    writer(handle, "%s", "Address  Frame    Logical addr  Module");
    writer(handle, "");

    uint32_t thread_ids[128];
    uint32_t thread_count = 0;
    uint32_t total_threads = 0;
    EnumerateThreads(&thread_count, thread_ids, 128, &total_threads);

    writer(handle, "Showing %d/%d threads...", thread_count, total_threads);

    DWORD current_tid = GetCurrentThreadId();

    for (uint32_t t = 0; t < thread_count; ++t) {
        DWORD tid = thread_ids[t];
        HANDLE thread_handle = nullptr;

        writer(handle, "");
        if (tid == current_tid) {
            writer(handle, "--- Thread ID: %d [Current Thread] ---", tid);
        } else {
            writer(handle, "--- Thread ID: %d ---", tid);
        }

        const void* cur_eip;
        void* const* cur_ebp;

        if (tid == current_tid) {
            cur_eip = eip;
            cur_ebp = ebp;
            thread_handle = GetCurrentThread();
        } else {
            thread_handle = OpenThread(THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, tid);
            if (!thread_handle) continue;

            SuspendThread(thread_handle);
            CONTEXT ctx = {};
            ctx.ContextFlags = CONTEXT_FULL;
            BOOL got = GetThreadContext(thread_handle, &ctx);
            ResumeThread(thread_handle);

            if (!got) {
                writer(handle, "**** Unable to retrieve thread context, error: %d",
                       GetLastError());
                CloseHandle(thread_handle);
                continue;
            }

#if defined(_M_X64) || defined(_M_ARM64) || defined(_WIN64)
            cur_eip = reinterpret_cast<const void*>(ctx.Rip);
            cur_ebp = reinterpret_cast<void* const*>(ctx.Rbp);
#else
            cur_eip = reinterpret_cast<const void*>(ctx.Eip);
            cur_ebp = reinterpret_cast<void* const*>(ctx.Ebp);
#endif
        }

        uint32_t skip = (tid == current_tid) ? skip_frames : 1;

        char mod_path[260];
        for (uint32_t i = 0; i < 100; ++i) {
            if (i >= skip) {
                uint32_t section = 0, offset = 0;
                ResolveAddressToModule(cur_eip, mod_path, 260, &section, &offset);
                writer(handle, "%08X %08X %04X:%08X %s",
                       reinterpret_cast<uintptr_t>(cur_eip),
                       reinterpret_cast<uintptr_t>(cur_ebp),
                       section, offset, mod_path);
            }

            if (IsBadWritePtr(const_cast<void**>(cur_ebp), 8)) break;

            cur_eip = cur_ebp[1];
            void* const* next_ebp = reinterpret_cast<void* const*>(cur_ebp[0]);

            if (reinterpret_cast<uintptr_t>(next_ebp) & 3) break;
            if (next_ebp <= cur_ebp) break;
            if (IsBadWritePtr(const_cast<void**>(next_ebp), 8)) break;

            cur_ebp = next_ebp;
        }

        CloseHandle(thread_handle);
    }
}

void DumpRegisters(int handle, LogWriterFn writer, const uint32_t* ctx) {
    WriteLogSectionHeader(writer, handle, "x86 Registers");

    writer(handle,
           "EAX=%08X  EBX=%08X  ECX=%08X  EDX=%08X  ESI=%08X",
           ctx[44], ctx[41], ctx[43], ctx[42], ctx[40]);
    writer(handle,
           "EDI=%08X  EBP=%08X  ESP=%08X  EIP=%08X  FLG=%08X",
           ctx[39], ctx[45], ctx[49], ctx[46], ctx[48]);
    writer(handle,
           "CS =%04X      DS =%04X      ES =%04X      SS =%04X      FS =%04X      GS =%04X",
           ctx[47], ctx[38], ctx[37], ctx[50], ctx[36], ctx[35]);
    writer(handle, "");
}
#endif

int InitDbgHelp() {
#if defined(_WIN32)
    ++s_dbghelp_refcount;
    return 1;
#else
    return 0;
#endif
}

void ReleaseDbgHelp() {
#if defined(_WIN32)
    --s_dbghelp_refcount;
#endif
}

#if defined(_WIN32)
void WriteErrorLogHeader(int flags, LogWriterFn writer, int handle,
                         const char* error_text, const void* system_time) {
    if (InterlockedIncrement(&s_reentrant_guard) != 1) {
        InterlockedDecrement(&s_reentrant_guard);
        return;
    }

    int effective_flags = flags;
    if (!flags || (flags & 1)) {
        effective_flags = flags | 0x3C;
        if (error_text && *error_text)
            effective_flags = flags | 0x3E;
    }

    char sep[80];
    std::memset(sep, '=', 78);
    sep[78] = '\0';
    writer(handle, "%s", sep);

    if ((effective_flags & 2) && error_text && *error_text) {
        writer(handle, "%s", error_text);
        writer(handle, "");
    }

    if (effective_flags & 4)
        WriteLogExePath(writer, handle);

    if (effective_flags & 8) {
        SYSTEMTIME st;
        const SYSTEMTIME* pst;
        if (system_time) {
            pst = static_cast<const SYSTEMTIME*>(system_time);
        } else {
            GetLocalTime(&st);
            pst = &st;
        }
        WriteLogTimestamp(reinterpret_cast<const uint16_t*>(pst), writer, handle);
    }

    if (effective_flags & 0x20)
        WriteLogUsername(writer, handle);

    if (effective_flags & 0x10)
        WriteLogComputerName(writer, handle);

    std::memset(sep, '-', 78);
    sep[78] = '\0';
    writer(handle, "%s", sep);

    InterlockedDecrement(&s_reentrant_guard);
}
#endif

#if defined(_WIN32)
unsigned long __stdcall WriteMiniDump_ThreadProc(void* parameter) {
    auto* params = static_cast<uint32_t*>(parameter);

    HANDLE proc = GetCurrentProcess();
    DWORD pid = GetCurrentProcessId();

    MINIDUMP_EXCEPTION_INFORMATION mei;
    mei.ThreadId = params[2];
    mei.ExceptionPointers = reinterpret_cast<PEXCEPTION_POINTERS>(params[1]);
    mei.ClientPointers = FALSE;

    params[3] = MiniDumpWriteDump(
        proc, pid,
        reinterpret_cast<HANDLE>(static_cast<uintptr_t>(params[0])),
        MiniDumpNormal,
        params[1] ? &mei : nullptr,
        nullptr, nullptr) ? 1 : 0;

    return 0;
}

int WriteMiniDump(int file_handle, void* exception_pointers,
                  int custom_stream_count, const char** custom_streams) {
    int result = 0;

    if (InterlockedIncrement(&s_reentrant_guard) != 1) {
        InterlockedDecrement(&s_reentrant_guard);
        return 0;
    }

    uint32_t params[6] = {};
    params[0] = static_cast<uint32_t>(file_handle);
    params[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(exception_pointers));
    params[2] = GetCurrentThreadId();
    params[4] = static_cast<uint32_t>(custom_stream_count);
    params[5] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(custom_streams));

    if (!GetModuleHandleA("psapi.dll"))
        LoadLibraryA("psapi.dll");

    DWORD tid = 0;
    HANDLE thread = CreateThread(nullptr, 0, WriteMiniDump_ThreadProc, params, 0, &tid);
    if (thread) {
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
        result = static_cast<int>(params[3]);
    }

    InterlockedDecrement(&s_reentrant_guard);
    return result;
}
#endif

bool IsMiniDumpAvailable() {
#if defined(_WIN32)

    return true;
#else
    return false;
#endif
}

#if defined(_WIN32)
void WriteModuleInfo(LogWriterFn writer, int handle, uint32_t base_addr,
                     const char* module_path, uint32_t module_size) {
    const auto info = detail::ReadModuleDebugInfo(
        reinterpret_cast<const void*>(static_cast<std::uintptr_t>(base_addr)),
        module_size);
    const std::string line =
        detail::BuildModuleInfoLogLine(base_addr, module_size, module_path, info);
    writer(handle, "%s", line.c_str());
}
#endif

#if defined(_WIN32)
void CollectLoadedModules(LogWriterFn writer, int handle) {
    writer(handle, "");
    WriteLogSectionHeader(writer, handle, "Loaded Modules");

    std::array<detail::LoadedModuleEntry, kMaxLoggedModules> modules{};
    std::size_t count = 0;
    DWORD enumeration_error = ERROR_SUCCESS;

    const bool snapshot_available =
        TryCollectToolhelpLoadedModules(modules, &count, &enumeration_error);
    if (!snapshot_available) {
        SetLastError(enumeration_error);
        CollectLoadedModulesFromPeb(modules, &count);
    }

    detail::SortLoadedModuleEntries(modules.data(), count);

    if (count == 0) {
        writer(handle,
               "****  CreateToolhelp32Snapshot couldn't enumerate modules, error: %d",
               enumeration_error);
        return;
    }

    for (std::size_t index = 0; index < count; ++index) {
        const auto& module = modules[index];
        WriteModuleInfo(writer, handle, module.base_addr, module.module_name.data(),
                        module.module_size);
    }
}
#endif

#if defined(_WIN32)
void CollectCrashReportData(int flags, LogWriterFn writer, int handle,
                            int skip_frames, const void* context_record) {
    if (InterlockedIncrement(&s_reentrant_guard) != 1) {
        InterlockedDecrement(&s_reentrant_guard);
        return;
    }

    const auto current_context =
        context_record != nullptr ? detail::CrashReportCurrentContext{}
                                  : CaptureCurrentCrashReportContext();
    const detail::CrashReportDispatchState dispatch_state =
        detail::BuildCrashReportDispatchState(flags, static_cast<std::uint32_t>(skip_frames),
                                              context_record, current_context);
    const auto* const eip = reinterpret_cast<const uint8_t*>(
        dispatch_state.context.instruction_pointer);
    const auto* const esp = reinterpret_cast<const uint8_t*>(
        dispatch_state.context.stack_pointer);
    const auto* const ebp = reinterpret_cast<void* const*>(
        dispatch_state.context.frame_pointer);

    {
        char sep[80];
        std::memset(sep, '-', 78);
        sep[78] = '\0';
        writer(handle, "%s", sep);
    }

    if ((dispatch_state.effective_flags & 0x10000) != 0 && context_record != nullptr) {
        DumpRegisters(handle, writer, static_cast<const uint32_t*>(context_record));
    }

    if ((dispatch_state.effective_flags & 0x80000) != 0) {
        CollectStackTrace_Manual(writer, handle, eip, ebp, dispatch_state.skip_frames);
    }

    if ((dispatch_state.effective_flags & 0x20000) != 0) {
        CollectStackTrace_DbgHelp(dispatch_state.effective_flags, writer, handle,
                                  eip, dispatch_state.context.frame_pointer == 0
                                           ? nullptr
                                           : reinterpret_cast<const void*>(
                                                 dispatch_state.context.frame_pointer),
                                  esp, dispatch_state.skip_frames);
    }

    if ((dispatch_state.effective_flags & 0x400000) != 0) {
        CollectLoadedModules(writer, handle);
    }

    if ((dispatch_state.effective_flags & 0x300000) != 0) {
        DumpMemory(writer, handle, dispatch_state.effective_flags, eip, esp);
    }

    {
        char sep[80];
        std::memset(sep, '-', 78);
        sep[78] = '\0';
        writer(handle, "%s", sep);
    }
    WriteLogCustomInfo(writer, handle);

    InterlockedDecrement(&s_reentrant_guard);
}
#endif

}
