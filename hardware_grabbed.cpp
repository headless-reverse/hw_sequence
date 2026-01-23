#include "hardware_grabbed.h"
#include <QtEndian>
#include <QDebug>
#include <cstring>

HardwareGrabbed::HardwareGrabbed(QObject *parent) : QObject(parent) {
    m_socket = new QTcpSocket(this);
	connect(m_socket, &QTcpSocket::readyRead, this, &HardwareGrabbed::onReadyRead);
}

bool HardwareGrabbed::connectToDevice(const QString &address, quint16 port) {
    if (m_socket->state() == QAbstractSocket::ConnectedState) return true;
    m_socket->connectToHost(address, port);
    return m_socket->waitForConnected(2000);}

void HardwareGrabbed::disconnectDevice() {m_socket->disconnectFromHost();}

uint8_t HardwareGrabbed::calculatePacketCrc(const ControlPacket &pkt) {
    const uint8_t *startNode = reinterpret_cast<const uint8_t*>(&pkt.magic);
    uint8_t crc = 0;
    for (size_t i = 0; i < 11; i++) {
        crc ^= startNode[i];}
    return crc;}

void HardwareGrabbed::sendTouch(uint8_t type, uint16_t x, uint16_t y, uint16_t slot) {
    if (!isConnected()) return;
    ControlPacket pkt = {0};
    pkt.head  = PROTOCOL_HEAD;
    pkt.magic = qToBigEndian<uint32_t>(CONTROL_MAGIC);
    pkt.type  = type;
    pkt.x     = qToBigEndian<uint16_t>(x);
    pkt.y     = qToBigEndian<uint16_t>(y);
    pkt.data  = qToBigEndian<uint16_t>(slot);
    pkt.crc   = calculatePacketCrc(pkt);
    m_socket->write(reinterpret_cast<const char*>(&pkt), sizeof(ControlPacket));
    if (type != EVENT_TYPE_TOUCH_MOVE) m_socket->flush();}

void HardwareGrabbed::sendKey(uint16_t keyCode, bool pressed) {
    if (m_socket->state() != QAbstractSocket::ConnectedState) return;
    ControlPacket pkt;
    memset(&pkt, 0, sizeof(ControlPacket));
    pkt.head  = PROTOCOL_HEAD;
    pkt.magic = qToBigEndian<uint32_t>(CONTROL_MAGIC);
    pkt.type  = EVENT_TYPE_KEY;
    pkt.data  = qToBigEndian<uint16_t>(keyCode);
    pkt.x = qToBigEndian<uint16_t>(pressed ? 1 : 0);
    pkt.crc = calculatePacketCrc(pkt);
    m_socket->write(reinterpret_cast<const char*>(&pkt), sizeof(ControlPacket));
    m_socket->flush();
}

void HardwareGrabbed::setHardwareGrab(bool enabled) {
    if (m_socket->state() != QAbstractSocket::ConnectedState) return;
    ControlPacket pkt;
    memset(&pkt, 0, sizeof(ControlPacket));
    pkt.head  = PROTOCOL_HEAD;
    pkt.magic = qToBigEndian<uint32_t>(CONTROL_MAGIC);
    pkt.type  = EVENT_TYPE_SET_GRAB;
    pkt.data  = qToBigEndian<uint16_t>(enabled ? 1 : 0);
    pkt.crc   = calculatePacketCrc(pkt);
    m_socket->write(reinterpret_cast<const char*>(&pkt), sizeof(ControlPacket));
    m_socket->flush();}

void HardwareGrabbed::enableAdbWireless() {
    if (m_socket->state() != QAbstractSocket::ConnectedState) return;
    ControlPacket pkt;
    memset(&pkt, 0, sizeof(ControlPacket));
    pkt.head  = PROTOCOL_HEAD;
    pkt.magic = qToBigEndian<uint32_t>(CONTROL_MAGIC);
    pkt.type  = EVENT_TYPE_ADB_WIFI;
    pkt.crc   = calculatePacketCrc(pkt);
    m_socket->write(reinterpret_cast<const char*>(&pkt), sizeof(ControlPacket));
    m_socket->flush();}

bool HardwareGrabbed::isConnected() const {return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;}

void HardwareGrabbed::onReadyRead() {
// Surowe bajty prosto do bufora
    m_readBuffer.append(m_socket->readAll());
// Pakiety 13-bajtowe
    while (m_readBuffer.size() >= sizeof(ControlPacket)) {
        // Synchronizacja nagłówka 0x55
        if (static_cast<uint8_t>(m_readBuffer[0]) != PROTOCOL_HEAD) {
            m_readBuffer.remove(0, 1);
            continue;}
        ControlPacket pkt;
        std::memcpy(&pkt, m_readBuffer.data(), sizeof(ControlPacket));
// validatePacket jest w control_protocol.cpp
        if (validatePacket(pkt)) {
            m_readBuffer.remove(0, sizeof(ControlPacket));
            if (pkt.type == EVENT_TYPE_REPORT_TOUCH) {
                uint16_t axis = qFromBigEndian<uint16_t>(pkt.data);
                uint16_t val = 0;
                if (axis == 0x35) val = qFromBigEndian<uint16_t>(pkt.x); // X
                else if (axis == 0x36) val = qFromBigEndian<uint16_t>(pkt.y); // Y
                emit remoteTouchEvent(axis, val);
            }
        } else {
            m_readBuffer.remove(0, 1);
        }
    }
}
