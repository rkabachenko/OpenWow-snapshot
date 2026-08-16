
#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <utility>
#include <vector>

namespace openwow::core {

using TlsSlotHandle = uint32_t;

using TlsDestructor = std::function<void(void*)>;

inline constexpr TlsSlotHandle kInvalidTlsSlot = 0;

class StormTls {
public:
    static StormTls& Instance();

    void InitMasterSlots();

    bool MasterSlotsInitialized() const;

    TlsSlotHandle AllocSlot(TlsDestructor destructor = nullptr);

    bool InitSlot(TlsSlotHandle& slot, TlsDestructor destructor = nullptr);

    void* GetValue(TlsSlotHandle slot) const;

    template <typename Factory>
    void* GetOrCreateValue(TlsSlotHandle& slot, Factory&& factory,
                           TlsDestructor destructor = nullptr) {
        if (!InitSlot(slot, std::move(destructor))) {
            return nullptr;
        }

        if (void* value = GetValue(slot); value != nullptr) {
            return value;
        }

        void* const value = std::forward<Factory>(factory)();
        SetValue(slot, value);
        return value;
    }

    bool IsSlotActive(TlsSlotHandle slot) const;

    bool SetValue(TlsSlotHandle slot, void* value);

    void CallDestructors();

    void FreeSlot(TlsSlotHandle slot);

    void* GetThreadMemory() const;

    void SetThreadMemory(void* mem);

    TlsSlotHandle GetThreadMemorySlot() const;

    TlsSlotHandle GetSecondaryMasterSlot() const;

private:
    StormTls() = default;

    TlsSlotHandle AllocateSlotLocked(TlsDestructor destructor);

    struct SlotInfo {
        bool          active = false;
        TlsDestructor destructor;
    };

    mutable std::recursive_mutex     mutex_;
    std::vector<SlotInfo>            slots_;
    TlsSlotHandle                    nextSlot_ = 1;
    TlsSlotHandle                    threadMemorySlot_ = kInvalidTlsSlot;
    TlsSlotHandle                    secondaryMasterSlot_ = kInvalidTlsSlot;
};

}
