
#pragma once

#include <cstdint>
#include <functional>
#include <mutex>

namespace openwow::core {

struct DbgHelpState {
    bool initialized = false;
    int32_t ref_count = 0;

    void* fn_stack_walk = nullptr;
    void* fn_sym_function_table_access = nullptr;
    void* fn_sym_get_line_from_addr = nullptr;
    void* fn_sym_get_module_base = nullptr;
    void* fn_sym_get_module_info = nullptr;
    void* fn_sym_get_options = nullptr;
    void* fn_sym_get_sym_from_addr = nullptr;
    void* fn_sym_initialize = nullptr;
    void* fn_sym_cleanup = nullptr;
    void* fn_sym_set_options = nullptr;
    void* fn_sym_enumerate_modules = nullptr;
    void* fn_sym_enumerate_symbols = nullptr;
    void* fn_minidump_write_dump = nullptr;
};

class DbgHelp {
public:
    using NativeModuleHandle = void*;
    using ModuleCloseFn = std::function<void(NativeModuleHandle)>;

    static DbgHelp& Instance();

    bool Initialize();

    void RegisterComponent();

    void ShutdownComponent();

    void AdoptModuleHandle(NativeModuleHandle module_handle);

    const DbgHelpState& state() const { return state_; }
    bool HasComponentModule() const;
    void SetModuleCloseFn(ModuleCloseFn close_fn);

private:
    DbgHelp() = default;

    static void ShutdownComponentAtExit();
    static void CloseNativeModuleHandle(NativeModuleHandle module_handle);

    DbgHelpState state_;
    mutable std::mutex component_mutex_;
    NativeModuleHandle component_module_handle_ = nullptr;
    ModuleCloseFn module_close_fn_ = CloseNativeModuleHandle;
    bool component_lock_initialized_ = false;
    bool atexit_registered_ = false;
};

using LogWriterFn = int (*)(int handle, const char* fmt, ...);

void WriteLogSeparator(bool wide_mode, LogWriterFn writer, int handle,
                       char fill_char);

}
