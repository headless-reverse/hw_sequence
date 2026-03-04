#ifndef CONTROL_SOCKET_H
#define CONTROL_SOCKET_H

#include <QObject>
#include <QTcpSocket>
#include <QByteArray>
#include "control_protocol.h"

class ControlSocket : public QObject {
    Q_OBJECT
public:
    explicit ControlSocket(QObject *parent = nullptr);
    
    void connectToLocalhost(quint16 port);
    void disconnectFromAgent();
    void connectToAgent(const QString &addr, quint16 port) { (void)addr; connectToLocalhost(port); }
    void sendTouchDown(uint16_t x, uint16_t y);
    void sendTouchMove(uint16_t x, uint16_t y);
    void sendTouchUp(uint16_t x = 0, uint16_t y = 0);
    void sendKey(uint16_t androidKeyCode);
    void sendTouch(int x, int y, int action) {
        if (action == 0) sendTouchDown(x, y);
        else if (action == 1) sendTouchUp(x, y);
        else if (action == 2) sendTouchMove(x, y);
    }
    void sendBackKey();
    void sendHomeKey();
    void setDeviceGrab(bool enable);
    void sendAdbWifi();

signals:
    void connected();
    void disconnected();
    void errorOccurred(const QString &message);
    void remoteTouchEvent(uint16_t axis, uint32_t value);
    
private slots:
    void onConnected();
    void onDisconnected();
    void onErrorOccurred(QTcpSocket::SocketError socketError);
    void onReadyRead();
    
private:
    QTcpSocket *m_socket;
    QByteArray m_readBuffer;
    void sendPacket(ControlEventType type, uint16_t x = 0, uint16_t y = 0, uint16_t data = 0);
};

#endif
