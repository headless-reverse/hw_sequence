#include "control_socket.h"
#include "control_protocol.h"
#include <QDebug>
#include <QHostAddress>
#include <QtEndian>

ControlSocket::ControlSocket(QObject *parent) 
    : QObject(parent), m_socket(new QTcpSocket(this)) {
    
    connect(m_socket, &QTcpSocket::connected, this, &ControlSocket::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &ControlSocket::onDisconnected);
	connect(m_socket, QOverload<QTcpSocket::SocketError>::of(&QTcpSocket::errorOccurred),this, &ControlSocket::onErrorOccurred);
	connect(m_socket, &QTcpSocket::readyRead, this, &ControlSocket::onReadyRead);
}

//void ControlSocket::connectToLocalhost(quint16 port) {m_socket->connectToHost("192.168.0.11", port);}

void ControlSocket::connectToLocalhost(quint16 port) {
	if (m_socket->state() != QAbstractSocket::UnconnectedState) {
		 m_socket->abort();}
    m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    m_socket->connectToHost(QHostAddress::LocalHost, port);}

void ControlSocket::disconnectFromAgent() {if (m_socket && m_socket->isOpen()) {m_socket->disconnectFromHost();}}

void ControlSocket::onConnected() {
    emit connected();
    qDebug() << "[ControlSocket]: connection established with android daemon.";
}

void ControlSocket::onDisconnected() {
    emit disconnected();
    qDebug() << "[ControlSocket]: connection closed.";
}

void ControlSocket::onErrorOccurred(QTcpSocket::SocketError socketError) {
    if (socketError != QTcpSocket::RemoteHostClosedError) {
        emit errorOccurred(QString("Control Socket Error: %1").arg(m_socket->errorString()));
    }
}

void ControlSocket::onReadyRead() {
    m_readBuffer.append(m_socket->readAll());
    while (m_readBuffer.size() >= sizeof(ControlPacket)) {
        if (static_cast<uint8_t>(m_readBuffer[0]) != PROTOCOL_HEAD) {
            m_readBuffer.remove(0, 1);
            continue;
        }
        ControlPacket pkt;
        std::memcpy(&pkt, m_readBuffer.data(), sizeof(ControlPacket));
//  CRC i Magic
        if (validatePacket(pkt)) {
            m_readBuffer.remove(0, sizeof(ControlPacket));
// Obsługa raportu z fizycznego ekranu (Typ 30)
            if (pkt.type == EVENT_TYPE_REPORT_TOUCH) {
                uint16_t axis = qFromBigEndian<uint16_t>(pkt.data);
                uint16_t val = 0;
                if (axis == 0x35) val = qFromBigEndian<uint16_t>(pkt.x);
                else if (axis == 0x36) val = qFromBigEndian<uint16_t>(pkt.y);
                emit remoteTouchEvent(axis, val);
            }
        } else {
            m_readBuffer.remove(0, 1);
        }
    }
}

void ControlSocket::sendPacket(ControlEventType type, uint16_t x, uint16_t y, uint16_t data) {
    if (m_socket->state() != QAbstractSocket::ConnectedState) return;
    ControlPacket pkt;
    pkt.head = PROTOCOL_HEAD;
    pkt.magic = qToBigEndian<uint32_t>(CONTROL_MAGIC);
    pkt.type = static_cast<uint8_t>(type);
    pkt.x = qToBigEndian<uint16_t>(x);
    pkt.y = qToBigEndian<uint16_t>(y);
    pkt.data = qToBigEndian<uint16_t>(data);
    pkt.crc = calculate_xor_crc(pkt); 
    m_socket->write(reinterpret_cast<const char*>(&pkt), sizeof(ControlPacket));
    m_socket->flush();
}

void ControlSocket::sendTouchDown(uint16_t x, uint16_t y) { sendPacket(EVENT_TYPE_TOUCH_DOWN, x, y, 0); }
void ControlSocket::sendTouchMove(uint16_t x, uint16_t y) { sendPacket(EVENT_TYPE_TOUCH_MOVE, x, y, 0); }
void ControlSocket::sendTouchUp(uint16_t x, uint16_t y)   { sendPacket(EVENT_TYPE_TOUCH_UP, x, y, 0); }
void ControlSocket::sendKey(uint16_t androidKeyCode)      { sendPacket(EVENT_TYPE_KEY, 0, 0, androidKeyCode); }
