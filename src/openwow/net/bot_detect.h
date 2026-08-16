#pragma once

#include "openwow/net/client_services_packet_sender.h"
#include "openwow/net/serialization/temp_world_packet_storage.h"

#include <cstdint>

namespace openwow::net {

static constexpr uint32_t kAddr_BotDetectEnabled = 0xB2F9E0;

static constexpr uint32_t kAddr_ConnectionState = 0xB2F9A0;

static constexpr uint32_t kAddr_LastTickTimestamp = 0xB2F9E4;

static constexpr uint32_t kAddr_CountdownTicks = 0xB2F9E8;

static constexpr uint32_t kOpcode_CMSG_BOT_DETECTED = 960;
static constexpr uint32_t kSessionKeySize            = 0x28;
static constexpr uint32_t kSHA1DigestSize             = 0x14;
static constexpr uint32_t kTickCooldownMs             = 0x3E8;
static constexpr std::uint32_t kWindowSizeSentinel    = 0xFFFFFFFFu;
static constexpr std::uint32_t kReadPosSentinel       = 0xFFFFFFFFu;

void SHA1_Init(uint8_t* ctx);

void SHA1_Update(uint8_t* ctx, const void* data, uint32_t len);

void SHA1_Final(uint8_t* ctx, uint8_t* digest);

void* ClientServices__GetConnectionObject();

char* GetSessionKey(void* connection);

int GetProbeValues(int* v1, int* v2, int* v3);

inline void CDataStore__PutUInt32(CDataStoreTempPacket* pkt, uint32_t value) {
    CDataStore_PutUInt32(pkt->store, value);
}

inline void CDataStore__PutInt8(CDataStoreTempPacket* pkt, std::int8_t value) {
    CDataStore_PutInt8(pkt->store, value);
}

inline void PutUInt8(CDataStoreTempPacket* pkt, std::uint8_t value) {
    CDataStore__PutInt8(pkt, static_cast<std::int8_t>(value));
}

inline void PutBytes(CDataStoreTempPacket* pkt, const void* data,
                     std::uint32_t len) {
    CDataStore_PutBytes(pkt->store, data, len);
}

inline bool ClientServices__SendPacket(CDataStoreTempPacket* pkt) {
    return pkt != nullptr && ClientServices__SendPacket(pkt->store);
}

CDataStoreTempPacket* CDataStore__Ctor_TempPacket_960(CDataStoreTempPacket* pkt);

void CDataStore__Dtor_TempPacket_960(CDataStoreTempPacket* pkt);

void CDataStore__DeleteDtor_TempPacket_960(CDataStoreTempPacket* pkt, char flags);

void Tick_CMSG_BOT_DETECTED();

}
