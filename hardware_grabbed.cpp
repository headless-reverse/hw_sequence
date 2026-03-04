#include "hardware_grabbed.h"
#include <QtEndian>
#include <QDebug>
#include <cstring>
#include <QElapsedTimer>
#include <netinet/in.h>

HardwareGrabbed::HardwareGrabbed(QObject *parent) : QObject(parent) {
    m_socket = new QTcpSocket(this);
	connect(m_socket, &QTcpSocket::connected, this, &HardwareGrabbed::connectedToDevice);
	connect(m_socket, &QTcpSocket::disconnected, this, &HardwareGrabbed::disconnectedFromDevice);
	connect(m_socket, &QTcpSocket::readyRead, this, &HardwareGrabbed::onReadyRead);
}

bool HardwareGrabbed::connectToDevice(const QString &address, quint16 port) {
    if (m_socket->state() == QAbstractSocket::ConnectedState) return true;
    m_socket->connectToHost(address, port);
    return m_socket->waitForConnected(2000);}

void HardwareGrabbed::disconnectDevice() {m_socket->disconnectFromHost();}

uint8_t HardwareGrabbed::calculatePacketCrc(const ControlPacket &pkt) {
    size_t data_len = offsetof(ControlPacket, crc) - offsetof(ControlPacket, magic);
    const uint8_t *data_ptr = (const uint8_t*)&pkt.magic;
    uint8_t crc = 0;
    for (size_t i = 0; i < data_len; i++) { crc ^= data_ptr[i]; }
    return crc;}

void HardwareGrabbed::sendPacket(const ControlPacket &pkt) {
    if (!isConnected()) return;
    m_socket->write(reinterpret_cast<const char*>(&pkt), sizeof(ControlPacket));
    m_socket->flush();
}

void HardwareGrabbed::sendTouch(uint8_t type, uint16_t x, uint16_t y, uint16_t slot) {
    if (!isConnected())
        return;
    if (type == EVENT_TYPE_TOUCH_MOVE &&
        m_socket->bytesToWrite() > sizeof(ControlPacket) * 5)
        return;
    ControlPacket pkt = createTouchPacket(
        static_cast<ControlEventType>(type), x, y, slot);
    qint64 written = 0;
    const char* raw = reinterpret_cast<const char*>(&pkt);
    const qint64 total = sizeof(ControlPacket);
    while (written < total) {
        qint64 w = m_socket->write(raw + written, total - written);
        if (w <= 0)
            return;
        written += w;
        if (!m_socket->waitForBytesWritten(50))
            return;}}

void HardwareGrabbed::handleRawEvent(uint16_t axis, uint16_t value) {
    if (!m_isRecording) return;
    if (axis == 53) m_currentX = value;
    else if (axis == 54) m_currentY = value;
    else if (axis == 57) {
        if (value != 0xFFFF && value != 65535) {
            processRecording(EVENT_TYPE_TOUCH_DOWN, m_currentX, m_currentY, 0);
        } else {
            processRecording(EVENT_TYPE_TOUCH_UP, m_currentX, m_currentY, 0);
        }
    }
}

void HardwareGrabbed::sendKey(uint16_t keyCode, bool pressed) {
    if (!isConnected())
        return;
    ControlPacket pkt = createTouchPacket(
        EVENT_TYPE_KEY,
        pressed ? 1 : 0,
        0,
        keyCode
    );
    qint64 written = 0;
    const char* raw = reinterpret_cast<const char*>(&pkt);
    const qint64 total = sizeof(ControlPacket);
    while (written < total) {
        qint64 w = m_socket->write(raw + written, total - written);
        if (w <= 0)
            return;
        written += w;
        if (!m_socket->waitForBytesWritten(50))
            return;}}

void HardwareGrabbed::setHardwareGrab(bool enable, int type) {
    if (!isConnected()) return;
    ControlPacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.head  = PROTOCOL_HEAD;
    pkt.magic = qToBigEndian<uint32_t>(CONTROL_MAGIC);
    pkt.type = (type == 1) ? EVENT_TYPE_GRAB_TOUCH : EVENT_TYPE_GRAB_KEYS;
    pkt.data = htons(enable ? 1 : 0);
    pkt.crc = calculatePacketCrc(pkt);
    m_socket->write(reinterpret_cast<const char*>(&pkt), sizeof(ControlPacket));
    m_socket->flush();
}

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

bool HardwareGrabbed::isConnected() const { return m_socket && m_socket->state() == QAbstractSocket::ConnectedState; }

void HardwareGrabbed::requestMemoryMaps(int pid) {
    if (!isConnected()) return;
    qDebug() << "[HardwareGrabbed] Requesting memory maps for PID:" << pid;
    m_mapsBuffer.remove(pid);
    ControlPacket pkt = createTouchPacket(static_cast<ControlEventType>(25), 0, 0, static_cast<uint16_t>(pid));
    sendPacket(pkt);
}


void HardwareGrabbed::onReadyRead() {
    m_readBuffer.append(m_socket->readAll());
    while (m_readBuffer.size() >= static_cast<int>(sizeof(ControlPacket))) {
        if (static_cast<uint8_t>(m_readBuffer.at(0)) != PROTOCOL_HEAD) {
            m_readBuffer.remove(0, 1);
            continue;
        }
        ControlPacket pkt;
        std::memcpy(&pkt, m_readBuffer.constData(), sizeof(ControlPacket));
        if (!validatePacket(pkt)) {
            m_readBuffer.remove(0, 1);
            continue;
        }
        m_readBuffer.remove(0, sizeof(ControlPacket));
        uint16_t x_num = qFromBigEndian<uint16_t>(pkt.x);
        uint16_t y_num = qFromBigEndian<uint16_t>(pkt.y);
        uint16_t d_num = qFromBigEndian<uint16_t>(pkt.data);
        const uint8_t* raw_x = reinterpret_cast<const uint8_t*>(&pkt.x);
        const uint8_t* raw_y = reinterpret_cast<const uint8_t*>(&pkt.y);
        if (pkt.type == EVENT_TYPE_GET_PROCS) {
            int pid = d_num;
            if (pid == 0xFFFF) { // Koniec listy
                emit processListFinished();
                m_procNameBuffer.clear();
            } else if (x_num == 0 && y_num == 0) {
                if (m_procNameBuffer.contains(pid)) {
                    emit processReceived(pid, m_procNameBuffer[pid]);
                    m_procNameBuffer.remove(pid);
                }
            } else {
                uint8_t chunk[4] = { raw_x[1], raw_x[0], raw_y[1], raw_y[0] };
                for (int i = 0; i < 4; i++) {
                    if (chunk[i] != 0) {
                        m_procNameBuffer[pid] += QChar(chunk[i]);
                    }
                }
            }
            continue;
        }

        if (pkt.type == EVENT_TYPE_GET_MEM_MAPS) {
            int pid = d_num;
            if (x_num == 0xFFFF && y_num == 0xFFFF) {
                emit memoryMapsReceived(pid, QString::fromUtf8(m_mapsBuffer[pid]));
                m_mapsBuffer.remove(pid);
            } else {
                uint8_t chunk[4] = { raw_x[1], raw_x[0], raw_y[1], raw_y[0] };
                for (int i = 0; i < 4; i++) {
                    if (chunk[i] != 0) {
                        m_mapsBuffer[pid].append(static_cast<char>(chunk[i]));
                    }
                }
            }
            continue;
        }
        if (pkt.type == EVENT_TYPE_INJECT_PID) {
            if (x_num == 1) emit injectionSuccess(d_num);
            else emit injectionFailed(d_num, "Rejected by daemon");
            continue;
        }
        if (pkt.type == EVENT_TYPE_KEY) {
            if (m_isRecording && x_num == 1) {
                emit recordKeyDetected(d_num);
            }
        } else {
            processRecording(pkt.type, x_num, y_num, d_num);
        }
        emit remoteTouchEvent(pkt.type, x_num);
    }
}



void HardwareGrabbed::processRecording(uint8_t type, uint16_t x, uint16_t y, uint16_t slot) {
    if (type == EVENT_TYPE_TOUCH_DOWN) {
        if (m_waitTimer.isValid()) {
            int gap = m_waitTimer.elapsed();
            if (gap > 50) {
                emit recordWaitDetected(gap);
            }
        }
        m_startX = x;
        m_startY = y;
        m_currentX = x;
        m_currentY = y;
        m_gestureTimer.start();
    }
    else if (type == EVENT_TYPE_TOUCH_MOVE) {
        m_currentX = x;
        m_currentY = y;
    }
    else if (type == EVENT_TYPE_TOUCH_UP) {
        if (!m_gestureTimer.isValid()) return;
        int duration = m_gestureTimer.elapsed();
        int dist = qAbs(m_currentX - m_startX) + qAbs(m_currentY - m_startY);
        if (dist < 30) {
            emit recordTapDetected(m_startX, m_startY);
        } else {
            emit recordSwipeDetected(m_startX, m_startY, m_currentX, m_currentY, duration);
        }
        m_waitTimer.start();
    }
}

void HardwareGrabbed::requestProcessList() {
    if (!isConnected())
        return;
    m_procNameBuffer.clear();
    ControlPacket pkt = createTouchPacket(
        static_cast<ControlEventType>(EVENT_TYPE_GET_PROCS), 
        0, 0, 0);
    sendPacket(pkt);
}

void HardwareGrabbed::requestProcessInjection(uint16_t pid) {
    if (!isConnected())
        return;
    ControlPacket pkt = createTouchPacket(
        static_cast<ControlEventType>(EVENT_TYPE_INJECT_PID), 
        0, 0, pid);
    sendPacket(pkt);
}

void HardwareGrabbed::sendRawData(const QByteArray &data) {
    if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState) {
        m_socket->write(data);
        m_socket->flush();
    }
}

bool HardwareGrabbed::validatePacket(const ControlPacket &pkt) {
    if (pkt.head != PROTOCOL_HEAD) return false;
    if (qFromBigEndian<uint32_t>(pkt.magic) != CONTROL_MAGIC) return false;
    if (pkt.crc != calculatePacketCrc(pkt)) return false;
    return true; } 
