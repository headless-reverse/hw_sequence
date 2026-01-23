#ifndef HARDWARECONTROLLER_H
#define HARDWARECONTROLLER_H

#include <QObject>
#include <QProcess>
#include <QTcpSocket>
#include <QTimer>
#include <QQueue>
#include "control_protocol.h"

enum class ControllerState {
    Disconnected,
    SettingUpTunnel,
    DeployingDaemon,
    Connecting,
    Connected,
    Error
};

class HardwareController : public QObject {
    Q_OBJECT
public:
    explicit HardwareController(QObject *parent = nullptr);

	ControllerState state() const { return m_state; }
	void connectToDevice(const QString &serial);
    void disconnectDevice();
    void sendAction(const ControlPacket &pkt);

signals:
    void stateChanged(ControllerState newState);
    void logMessage(const QString &msg, bool isError = false);
    void connectionLost();

private slots:
    void onSocketConnected();
    void onSocketError(QAbstractSocket::SocketError error);
    void processNextDeploymentStep();

private:
    ControllerState m_state = ControllerState::Disconnected;
    QString m_deviceSerial;
    QTcpSocket *m_socket;
    QTimer *m_reconnectTimer;
    void setState(ControllerState s);
    void setupAdbTunnel();
    void startRemoteDaemon();
};

#endif
