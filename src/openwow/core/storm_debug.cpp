
#include "storm_debug.h"
#include "storm_component.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <utility>

#if defined(_WIN32)
#  include <windows.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <dlfcn.h>
#endif

#if defined(__linux__)
#include <execinfo.h>
#endif

namespace openwow::core {

DbgHelp& DbgHelp::Instance() {
    static DbgHelp inst;
    return inst;
}

void DbgHelp::RegisterComponent() {
    std::lock_guard lock(component_mutex_);
    auto& components = BlizzardComponent::Instance();
    if (components.GetRefCount(BlizzardComponents::kDebug) <= 0) {
        components.Register(BlizzardComponents::kDebug);
    }

    components.SetShutdownCallback(
        BlizzardComponents::kDebug,
        []() { DbgHelp::Instance().ShutdownComponent(); });
    components.AddDependency(BlizzardComponents::kDebug, BlizzardComponents::kMemory);

    if (!atexit_registered_) {
        std::atexit(&DbgHelp::ShutdownComponentAtExit);
        atexit_registered_ = true;
    }
}

void DbgHelp::ShutdownComponent() {
    std::lock_guard lock(component_mutex_);
    if (!component_lock_initialized_ || component_module_handle_ == nullptr) {
        return;
    }

    module_close_fn_(component_module_handle_);
    component_module_handle_ = nullptr;
    component_lock_initialized_ = false;
}

void DbgHelp::AdoptModuleHandle(NativeModuleHandle module_handle) {
    std::lock_guard lock(component_mutex_);
    if (component_module_handle_ != nullptr && component_module_handle_ != module_handle) {
        module_close_fn_(component_module_handle_);
    }

    component_module_handle_ = module_handle;
    if (module_handle != nullptr) {
        component_lock_initialized_ = true;
    }
}

bool DbgHelp::HasComponentModule() const {
    std::lock_guard lock(component_mutex_);
    return component_module_handle_ != nullptr;
}

void DbgHelp::SetModuleCloseFn(ModuleCloseFn close_fn) {
    std::lock_guard lock(component_mutex_);
    if (close_fn) {
        module_close_fn_ = std::move(close_fn);
    } else {
        module_close_fn_ = CloseNativeModuleHandle;
    }
}

void DbgHelp::ShutdownComponentAtExit() {
    BlizzardComponent::Instance().Shutdown(BlizzardComponents::kDebug);
}

void DbgHelp::CloseNativeModuleHandle(NativeModuleHandle module_handle) {
#if defined(_WIN32)
    if (module_handle != nullptr) {
        FreeLibrary(static_cast<HMODULE>(module_handle));
    }
#elif defined(__linux__) || defined(__APPLE__)
    if (module_handle != nullptr) {
        dlclose(module_handle);
    }
#else
    static_cast<void>(module_handle);
#endif
}

bool DbgHelp::Initialize() {
    RegisterComponent();
    ++state_.ref_count;
    if (state_.initialized) return true;

#if defined(_WIN32)

    state_.initialized = false;
    return false;
#elif defined(__linux__)

    state_.initialized = true;
    return true;
#else
    state_.initialized = false;
    return false;
#endif
}

void WriteLogSeparator(bool wide_mode, LogWriterFn writer, int handle,
                       char fill_char) {
    size_t len = wide_mode ? 78 : 40;
    char buf[80];
    std::memset(buf, fill_char, len);
    buf[len] = '\0';

    if (writer) {
        writer(handle, "%s", buf);
    }
}

}
