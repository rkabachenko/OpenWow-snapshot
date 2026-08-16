
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace openwow::platform {

struct ImeCandidateList {
    uint32_t                  pageStart  = 0;
    uint32_t                  pageSize   = 0;
    uint32_t                  totalCount = 0;
    uint32_t                  selection  = 0;
    std::vector<std::string>  candidates;
};

struct ImeCompositionState {
    std::string compositionText;
    int32_t     cursorPos   = 0;
    int32_t     selectionLen = 0;
    int32_t     clauseStart = 0;
    int32_t     clauseEnd   = 0;
};

class OsImeStub {
public:

    static OsImeStub& Instance();

    void Initialize();

    void Shutdown();

    [[nodiscard]] bool IsInitialized() const { return initialized_; }

    void AdjustEnableRef(bool enable);

    [[nodiscard]] bool IsEnabled() const { return enableCount_ > 0; }
    [[nodiscard]] int32_t GetEnableCount() const { return enableCount_; }

    void ManageContextAssociation();

    void UpdateComposition(const std::string& text, int32_t cursor, int32_t selLen);

    void ClearComposition();

    [[nodiscard]] const ImeCompositionState& GetComposition() const { return composition_; }

    [[nodiscard]] const ImeCandidateList& GetCandidateList() const { return candidates_; }

    bool ProcessTextEvent(int eventType, const char* text, int32_t start, int32_t length);

    int OsGuiPointer_ProcessMessage(void* ) { return 0; }

    int RegisterInputKey(int , int , int ) { return 0; }

    void ToggleImeNativeMode(bool enableNative);

    [[nodiscard]] bool IsNativeModeActive() const { return nativeModeActive_; }

    void ResetForTest();

private:
    OsImeStub() = default;

    bool                initialized_  = false;
    int32_t             enableCount_  = 0;

    void*               savedContext_ = nullptr;

    bool                textInputActive_ = false;
    bool                nativeModeActive_ = false;

    ImeCompositionState composition_;
    ImeCandidateList    candidates_;
};

void IME_Initialize();

void IME_Shutdown();

void IME_SetEnabled(int enable);

void IME_ManageContextAssociation();

void IME_ToggleNativeMode(bool enableNative);

}
