#pragma once
#include <stdint.h>
#include <QByteArray>

#define CONTROL_MAGIC 0x41444253
#define PROTOCOL_HEAD 0x55

enum ControlEventType : uint8_t {
    EVENT_TYPE_KEY         = 1,
    EVENT_TYPE_TOUCH_DOWN  = 2,
    EVENT_TYPE_TOUCH_UP    = 3,
    EVENT_TYPE_TOUCH_MOVE  = 4,
    EVENT_TYPE_SCROLL      = 5,
    EVENT_TYPE_BACK        = 6,
	EVENT_TYPE_HOME        = 7,
	EVENT_TYPE_ADB_WIFI    = 20,
	EVENT_TYPE_SET_GRAB    = 21,
	EVENT_TYPE_REPORT_TOUCH = 30
};

#pragma pack(push, 1)
struct ControlPacket {
    uint8_t head;
    uint32_t magic;
    uint8_t type;
    uint16_t x;
    uint16_t y;
    uint16_t data;
    uint8_t crc;
} __attribute__((packed));
#pragma pack(pop)

ControlPacket createTouchPacket(ControlEventType type, uint16_t x, uint16_t y, uint16_t data = 0);
ControlPacket createKeyPacket(uint16_t keyCode);
QByteArray packetToByteArray(const ControlPacket& packet);
uint8_t calculate_xor_crc(const ControlPacket& pkt); 
bool validatePacket(const ControlPacket& pkt);
