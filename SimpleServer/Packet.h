#ifndef METIN2_SIMPLE_PACKET_H
#define METIN2_SIMPLE_PACKET_H

#include <cstdint>

enum PacketHeader : uint8_t {
    HEADER_CG_HANDSHAKE = 0xff,
    HEADER_GC_HANDSHAKE = 0xff
};

#pragma pack(push, 1)
struct TPacketCGHandshake {
    uint8_t  bHeader;
    uint32_t dwHandshake;
    uint32_t dwTime;
    int32_t  lDelta;
};

struct TPacketGCHandshake {
    uint8_t  bHeader;
    uint32_t dwHandshake;
    uint32_t dwTime;
    int32_t  lDelta;
};
#pragma pack(pop)

#endif // METIN2_SIMPLE_PACKET_H