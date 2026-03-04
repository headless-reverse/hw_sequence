#include "hardware_controller.h"
#include <QDebug>

HardwareController::HardwareController(QObject *parent) : QObject(parent) {
    m_socket = new QTcpSocket(this);
    connect(m_socket, &QTcpSocket::connected, this, &HardwareController::onSocketConnected);
    connect(m_socket, QOverload<QTcpSocket::SocketError>::of(&QTcpSocket::errorOccurred), this, &HardwareController::onSocketError);
}

void HardwareController::setState(ControllerState s) {
    if (m_state == s) return;
    m_state = s;
    emit stateChanged(m_state);
}

void HardwareController::connectToDevice(const QString &serial) {
    m_deviceSerial = serial;
    if (m_deviceSerial.isEmpty()) return;
    setupAdbTunnel();
}

void HardwareController::setupAdbTunnel() {
    setState(ControllerState::SettingUpTunnel);
    QProcess *adb = new QProcess(this);
    connect(adb, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), [this, adb](int exitCode) {
        if (exitCode == 0) startRemoteDaemon();
        else setState(ControllerState::Error);
        adb->deleteLater();
    });
    adb->start("adb", {"-s", m_deviceSerial, "forward", "tcp:22222", "tcp:22222"});
}

void HardwareController::startRemoteDaemon() {
    setState(ControllerState::DeployingDaemon);
    QString cmd = "su -c 'pkill -9 hw_resident; /data/local/tmp/hw_resident &'";
    QProcess::startDetached("adb", {"-s", m_deviceSerial, "shell", cmd});
    QTimer::singleShot(1000, this, [this]() {
        setState(ControllerState::Connecting);
        m_socket->connectToHost("127.0.0.1", 22222);
    });
}

void HardwareController::onSocketConnected() {setState(ControllerState::Connected);}

void HardwareController::sendAction(const ControlPacket &pkt) {
    if (m_state == ControllerState::Connected) {
        m_socket->write(reinterpret_cast<const char*>(&pkt), sizeof(ControlPacket));
        m_socket->flush();
    }
}

void HardwareController::onSocketError(QAbstractSocket::SocketError error) {
    setState(ControllerState::Error);
}

void HardwareController::processNextDeploymentStep() {
    qDebug() << "[Hardware] Step processing...";
}

void HardwareController::sendRaw(const QByteArray &data) {
    if (m_state == ControllerState::Connected && m_socket) {
        m_socket->write(data);
        m_socket->flush();
    } else {
        emit logMessage("Cannot send raw data: Not connected", true);
    }
}

void HardwareController::disconnectDevice() {
    if (m_socket) {
        m_socket->abort();
    }
    setState(ControllerState::Disconnected);
}
