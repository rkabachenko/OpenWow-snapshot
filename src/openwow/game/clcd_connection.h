
#pragma once

#include <cstddef>
#include <cstdint>

#include "openwow/platform/adapters/win32/win32_compat.h"

namespace openwow::game {

enum class CLCDConnectionEventType : std::uint32_t {
    kNotification = 0,

    kMethod = 2,

};

struct CLCDConnectionEventNode {
    CLCDConnectionEventNode* next = nullptr;
    CLCDConnectionEventNode* prev = nullptr;
    CLCDConnectionEventType event_type{};
    std::uint32_t param1 = 0;
    std::uint32_t param2 = 0;
    std::uint32_t param3 = 0;
    std::uint32_t reserved1 = 0;
    std::uint32_t reserved2 = 0;
};

#if INTPTR_MAX == INT32_MAX
static_assert(sizeof(CLCDConnectionEventNode) == 32,
              "CB_EVENT_NODE must be 32 bytes (IDA SMemAlloc size)");
static_assert(offsetof(CLCDConnectionEventNode, next) == 0x00);
static_assert(offsetof(CLCDConnectionEventNode, prev) == 0x04);
static_assert(offsetof(CLCDConnectionEventNode, event_type) == 0x08);
static_assert(offsetof(CLCDConnectionEventNode, param1) == 0x0C);
static_assert(offsetof(CLCDConnectionEventNode, param2) == 0x10);
static_assert(offsetof(CLCDConnectionEventNode, param3) == 0x14);
#endif

struct CLCDConnectionEventSentinel {
    CLCDConnectionEventNode* first = nullptr;
    CLCDConnectionEventNode* last = nullptr;

    void Initialize();
    [[nodiscard]] bool IsEmpty() const;

    [[nodiscard]] CLCDConnectionEventNode* AsSentinelNode();
    [[nodiscard]] const CLCDConnectionEventNode* AsSentinelNode() const;
};

#if INTPTR_MAX == INT32_MAX
static_assert(sizeof(CLCDConnectionEventSentinel) == 8);
#endif

struct CEzLcdPage;

class CLCDConnection {
public:
    CLCDConnection();
    virtual ~CLCDConnection();

    virtual void OnEventQueued();

    void ReleaseMonochromePage();

    void ReleaseColorPage();

    platform::StormCriticalSection& GetEventCriticalSection();
    CLCDConnectionEventSentinel& GetEventList();
    [[nodiscard]] const CLCDConnectionEventSentinel& GetEventList() const;

    [[nodiscard]] void* GetColorPage() const { return color_page_; }
    [[nodiscard]] void* GetMonochromePage() const { return mono_page_; }
    void SetColorPage(void* page) { color_page_ = page; }
    void SetMonochromePage(void* page) { mono_page_ = page; }

    [[nodiscard]] void* GetColorGfx() const { return color_gfx_; }
    [[nodiscard]] void* GetMonoGfx() const { return mono_gfx_; }
    void SetColorGfx(void* gfx) { color_gfx_ = gfx; }
    void SetMonoGfx(void* gfx) { mono_gfx_ = gfx; }

    [[nodiscard]] std::int32_t GetConnectionHandle() const { return connection_handle_; }
    void SetConnectionHandle(std::int32_t handle) { connection_handle_ = handle; }

    std::uint32_t* GetConnectionParams() { return connection_params_; }
    [[nodiscard]] const std::uint32_t* GetConnectionParams() const { return connection_params_; }

    void SetCallbackContext(void* ctx) { callback_context_ = ctx; }
    [[nodiscard]] void* GetCallbackContext() const { return callback_context_; }

private:

    std::uint32_t connection_params_[10] = {};

    std::int32_t connection_handle_ = -1;

    void* color_page_ = nullptr;

    void* color_gfx_  = nullptr;

    void* mono_page_  = nullptr;

    void* mono_gfx_   = nullptr;

    void* callback_context_ = nullptr;

    platform::StormCriticalSection event_cs_;

    CLCDConnectionEventSentinel event_list_;

};

void InsertEventNodeAtHead(CLCDConnectionEventSentinel& sentinel,
                           CLCDConnectionEventNode* node);

int CLCDConnection_CB_EVENT_NODE_Method(
    CLCDConnection* connection,
    std::uint32_t param1,
    std::uint32_t param2,
    std::uint32_t param3);

}
