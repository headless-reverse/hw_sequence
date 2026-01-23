#ifndef HARDWAREGRABBED_H
#define HARDWAREGRABBED_H

#include <QObject>
#include <QtEndian>
#include <QTcpSocket>
#include <QByteArray>
#include "control_protocol.h"

class HardwareGrabbed : public QObject {
    Q_OBJECT
public:
    explicit HardwareGrabbed(QObject *parent = nullptr);
    
    bool connectToDevice(const QString &address = "127.0.0.1", quint16 port = 22222);
    void disconnectDevice();
    void sendTouch(uint8_t type, uint16_t x, uint16_t y, uint16_t slot = 0);
    void sendKey(uint16_t keyCode, bool pressed = true);
    void setHardwareGrab(bool enabled);
	void enableAdbWireless();
	bool isConnected() const;

signals:
    void remoteTouchEvent(uint16_t axis, uint16_t value);

private slots:
    void onReadyRead();

private:
	QTcpSocket *m_socket;
	QByteArray m_readBuffer;
    uint8_t calculatePacketCrc(const ControlPacket &pkt);
};

#endif // HARDWAREGRABBED_H
