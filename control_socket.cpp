#include "control_socket.h"
#include "control_protocol.h"
#include <QDebug>
#include <QHostAddress>
#include <QtEndian>
#include <arpa/inet.h>

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
        emit errorOccurred(QString("Control Socket Error: %1").arg(m_socket->errorString()));}}

void ControlSocket::onReadyRead() {
    m_readBuffer.append(m_socket->readAll());
    while (m_readBuffer.size() >= static_cast<int>(sizeof(ControlPacket))) {
        int headIdx = m_readBuffer.indexOf(PROTOCOL_HEAD);
        if (headIdx == -1) {
            m_readBuffer.clear();
            break;}
        if (headIdx > 0) {
            m_readBuffer.remove(0, headIdx);
            if (m_readBuffer.size() < static_cast<int>(sizeof(ControlPacket))) break;
        }
        ControlPacket pkt;
        std::memcpy(&pkt, m_readBuffer.constData(), sizeof(ControlPacket));
        if (!validatePacket(pkt)) {
            m_readBuffer.remove(0, 1);
            continue;}
        m_readBuffer.remove(0, sizeof(ControlPacket));
        uint16_t x    = qFromBigEndian<uint16_t>(pkt.x);
        uint16_t y    = qFromBigEndian<uint16_t>(pkt.y);
        uint16_t data = qFromBigEndian<uint16_t>(pkt.data);
		if (pkt.type == EVENT_TYPE_KEY) {
			emit remoteTouchEvent(x, static_cast<uint32_t>(data));
		} else {
			emit remoteTouchEvent(pkt.type, static_cast<uint32_t>(x));
		}
	}
}

void ControlSocket::sendPacket(ControlEventType type, uint16_t x, uint16_t y, uint16_t data) {
	if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) {
		qDebug() << "[ControlSocket] Brak połączenia, pomijam pakiet:" << type;
        return;
    }
    ControlPacket pkt = createTouchPacket(type, x, y, data);
    m_socket->write(reinterpret_cast<const char*>(&pkt), sizeof(ControlPacket));
    if (m_socket->bytesToWrite() > 1024) {
        m_socket->flush();
    }
}

void ControlSocket::sendTouchDown(uint16_t x, uint16_t y) { sendPacket(EVENT_TYPE_TOUCH_DOWN, x, y, 0); }
void ControlSocket::sendTouchMove(uint16_t x, uint16_t y) { sendPacket(EVENT_TYPE_TOUCH_MOVE, x, y, 0); }
void ControlSocket::sendTouchUp(uint16_t x, uint16_t y)   { sendPacket(EVENT_TYPE_TOUCH_UP, x, y, 0); }
void ControlSocket::sendKey(uint16_t androidKeyCode)      { sendPacket(EVENT_TYPE_KEY, 0, 0, androidKeyCode); }

void ControlSocket::sendBackKey() { sendPacket(EVENT_TYPE_BACK, 0, 0, 0); }
void ControlSocket::sendHomeKey() { sendPacket(EVENT_TYPE_HOME, 0, 0, 0); }

void ControlSocket::setDeviceGrab(bool enable) {
	sendPacket(EVENT_TYPE_GRAB_TOUCH, 0, 0, enable ? 1 : 0);
	sendPacket(EVENT_TYPE_GRAB_KEYS, 0, 0, enable ? 1 : 0);
}

void ControlSocket::sendAdbWifi() {sendPacket(EVENT_TYPE_ADB_WIFI, 0, 0, 0);}
