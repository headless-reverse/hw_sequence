#include "control_protocol.h"
#include <QtEndian>
#include <cstring>

uint8_t calculate_xor_crc(const ControlPacket& pkt) {
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(&pkt.magic);
    uint8_t crc = 0;
    for (size_t i = 0; i < 11; ++i) {
        crc ^= ptr[i];}
    return crc;}

ControlPacket createTouchPacket(ControlEventType type, uint16_t x, uint16_t y, uint16_t data) {
    ControlPacket pkt;
    std::memset(&pkt, 0, sizeof(ControlPacket));
    
    pkt.head  = PROTOCOL_HEAD;
    pkt.magic = qToBigEndian<uint32_t>(CONTROL_MAGIC);
    pkt.type  = static_cast<uint8_t>(type);
    pkt.x     = qToBigEndian<uint16_t>(x);
    pkt.y     = qToBigEndian<uint16_t>(y);
    pkt.data  = qToBigEndian<uint16_t>(data);
    pkt.crc   = calculate_xor_crc(pkt);
    
    return pkt;}

ControlPacket createKeyPacket(uint16_t keyCode) {return createTouchPacket(EVENT_TYPE_KEY, 0, 0, keyCode);}

QByteArray packetToByteArray(const ControlPacket& packet) {
    return QByteArray(reinterpret_cast<const char*>(&packet), sizeof(ControlPacket));}

bool validatePacket(const ControlPacket& pkt) {
    if (pkt.head != PROTOCOL_HEAD) return false;
    if (pkt.magic != qToBigEndian<uint32_t>(CONTROL_MAGIC)) return false;
    
    uint8_t expectedCrc = calculate_xor_crc(pkt);
    return (pkt.crc == expectedCrc);
}
