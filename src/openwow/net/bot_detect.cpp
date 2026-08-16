
#include "openwow/net/bot_detect.h"
#include "openwow/runtime/time/game_clock.h"

#include <cstring>

namespace openwow::net {

static uint8_t  byte_B2F9E0  = 0;

static uint32_t dword_B2F9A0 = 0;

static uint32_t dword_B2F9E4 = 0;

static uint32_t dword_B2F9E8 = 0;

CDataStoreTempPacket* CDataStore__Ctor_TempPacket_960(CDataStoreTempPacket* pkt) {
    pkt->store.data = nullptr;
    pkt->store.window_base = 0;
    pkt->store.window_size = 0;
    pkt->store.write_pos = 0;
    pkt->store.read_pos = kReadPosSentinel;
    pkt->store.vtable = CDataStore_TempWorldPacketVTable();

    CDataStore__InitTempWorldPacketStorage(
        pkt, &pkt->store.data, &pkt->store.window_base,
        reinterpret_cast<int*>(&pkt->store.window_size));

    return pkt;
}

void CDataStore__Dtor_TempPacket_960(CDataStoreTempPacket* pkt) {
    pkt->store.vtable = CDataStore_TempWorldPacketVTable();

    if (pkt->store.window_size != kWindowSizeSentinel) {
        CDataStore__CleanupTempWorldPacketStorage(
            pkt, &pkt->store.data, &pkt->store.window_base,
            reinterpret_cast<int*>(&pkt->store.window_size));
    }

    CDataStore_Dtor(&pkt->store);
}

void CDataStore__DeleteDtor_TempPacket_960(CDataStoreTempPacket* pkt, char flags) {
    pkt->store.vtable = CDataStore_TempWorldPacketVTable();

    if (pkt->store.window_size != kWindowSizeSentinel) {
        CDataStore__CleanupTempWorldPacketStorage(
            pkt, &pkt->store.data, &pkt->store.window_base,
            reinterpret_cast<int*>(&pkt->store.window_size));
    }

    CDataStore_DeleteDtor(&pkt->store, flags);
}

void Tick_CMSG_BOT_DETECTED() {
    if (!byte_B2F9E0) {
        return;
    }

    void* connection = ClientServices__GetConnectionObject();

    if (!dword_B2F9A0) {
        return;
    }

    if (!connection) {
        return;
    }

    const uint32_t tick = openwow::core::GameClock::GetTickCount32();

    if (tick - dword_B2F9E4 < kTickCooldownMs) {
        return;
    }

    dword_B2F9E4 = tick;

    if (--dword_B2F9E8 > 0) {
        return;
    }

    byte_B2F9E0 = 0;

    int v9, v8, v10;
    if (!GetProbeValues(&v9, &v8, &v10)) {
        return;
    }

    uint8_t sha_ctx[96];
    SHA1_Init(sha_ctx);

    char* session_key = GetSessionKey(connection);

    SHA1_Update(sha_ctx, session_key, kSessionKeySize);
    SHA1_Update(sha_ctx, &v9, 1);
    SHA1_Update(sha_ctx, &v8, 1);
    SHA1_Update(sha_ctx, &v10, 1);
    SHA1_Update(sha_ctx, session_key, kSessionKeySize);

    uint8_t digest[kSHA1DigestSize];
    SHA1_Final(sha_ctx, digest);

    CDataStoreTempPacket packet;
    std::memset(&packet, 0, sizeof(packet));

    CDataStore__Ctor_TempPacket_960(&packet);

    CDataStore__PutUInt32(&packet, kOpcode_CMSG_BOT_DETECTED);
    PutUInt8(&packet, static_cast<uint8_t>(v9));
    PutUInt8(&packet, static_cast<uint8_t>(v8));
    PutUInt8(&packet, static_cast<uint8_t>(v10));
    PutBytes(&packet, digest, kSHA1DigestSize);

    packet.store.read_pos = 0;

    ClientServices__SendPacket(&packet);

    CDataStore__Dtor_TempPacket_960(&packet);
}

}
