
#include "openwow/platform/adapters/ime/os_ime.h"
#include "openwow/platform/process/os_platform.h"

#if defined(_WIN32)
#include <Windows.h>
#include <Imm.h>
#pragma comment(lib, "imm32.lib")
#endif

namespace openwow::platform {

OsImeStub& OsImeStub::Instance() {
    static OsImeStub inst;
    return inst;
}

void OsImeStub::Initialize() {

    enableCount_ = 0;

#if defined(_WIN32)
    auto* const window = static_cast<HWND>(OS_GetActiveWindow(0));
    if (window) {
        savedContext_ = ImmAssociateContext(window, nullptr);
    }
#else

    savedContext_ = nullptr;
#endif

    textInputActive_ = false;
    initialized_ = true;
}

void OsImeStub::Shutdown() {
#if defined(_WIN32)
    auto* const window = static_cast<HWND>(OS_GetActiveWindow(0));
    if (window) {

        ImmAssociateContextEx(window, nullptr, 0x10u);
    }
#endif

    savedContext_    = nullptr;
    enableCount_     = 0;
    textInputActive_ = false;
    initialized_     = false;
}

void OsImeStub::AdjustEnableRef(bool enable) {

    if (enable) {
        if (++enableCount_ == 1) {

#if defined(_WIN32)
            auto* const window = static_cast<HWND>(OS_GetActiveWindow(0));
            if (window) {
                ImmAssociateContext(window, static_cast<HIMC>(savedContext_));
            }
#else

            textInputActive_ = true;
#endif
        }
    } else if (enableCount_ > 0) {
        if (--enableCount_ == 0) {

#if defined(_WIN32)
            auto* const window = static_cast<HWND>(OS_GetActiveWindow(0));
            if (window) {
                ImmAssociateContext(window, nullptr);
            }
#else

            textInputActive_ = false;
#endif
        }
    }

}

void OsImeStub::ManageContextAssociation() {

#if defined(_WIN32)
    auto* const window = static_cast<HWND>(OS_GetActiveWindow(0));
    if (!window) {
        return;
    }

    HIMC const currentContext = ImmGetContext(window);
    if (enableCount_ > 0) {

        if (currentContext != static_cast<HIMC>(savedContext_)) {
            ImmAssociateContext(window, static_cast<HIMC>(savedContext_));
        }
    } else {

        if (currentContext != nullptr) {
            ImmAssociateContext(window, nullptr);
        }
    }
#else

    const bool shouldBeActive = (enableCount_ > 0);
    if (textInputActive_ != shouldBeActive) {
        textInputActive_ = shouldBeActive;

    }
#endif
}

void OsImeStub::UpdateComposition(const std::string& text, int32_t cursor, int32_t selLen) {

    composition_.compositionText = text;
    composition_.cursorPos = cursor;
    composition_.selectionLen = selLen;
}

void OsImeStub::ClearComposition() {
    composition_ = {};
}

bool OsImeStub::ProcessTextEvent(int eventType, const char* text,
                                  int32_t start, int32_t length) {

    if (!IsEnabled()) return false;

    constexpr int SDL_TEXTEDITING_TYPE = 0x302;
    constexpr int SDL_TEXTINPUT_TYPE   = 0x303;

    if (eventType == SDL_TEXTEDITING_TYPE) {
        UpdateComposition(text ? text : "", start, length);
        return true;
    }
    if (eventType == SDL_TEXTINPUT_TYPE) {
        ClearComposition();

        return true;
    }
    return false;
}

void OsImeStub::ToggleImeNativeMode(bool enableNative) {

#if defined(_WIN32)
    auto* const window = static_cast<HWND>(OS_GetActiveWindow(0));
    if (!window) {
        return;
    }
    HIMC const context = ImmGetContext(window);
    if (!context) {
        return;
    }
    DWORD fdwConversion = 0;
    DWORD fdwSentence = 0;
    ImmGetConversionStatus(context, &fdwConversion, &fdwSentence);
    if (enableNative) {
        fdwConversion |= 1u;
    } else {
        fdwConversion &= ~1u;
    }
    ImmSetConversionStatus(context, fdwConversion, fdwSentence);
#endif

    nativeModeActive_ = enableNative;
}

void OsImeStub::ResetForTest() {
    initialized_     = false;
    enableCount_     = 0;
    savedContext_    = nullptr;
    textInputActive_ = false;
    nativeModeActive_ = false;
    composition_     = {};
    candidates_      = {};
}

void IME_Initialize() {
    OsImeStub::Instance().Initialize();
}

void IME_Shutdown() {
    OsImeStub::Instance().Shutdown();
}

void IME_SetEnabled(int enable) {
    OsImeStub::Instance().AdjustEnableRef(enable != 0);
}

void IME_ManageContextAssociation() {
    OsImeStub::Instance().ManageContextAssociation();
}

void IME_ToggleNativeMode(bool enableNative) {
    OsImeStub::Instance().ToggleImeNativeMode(enableNative);
}

}
