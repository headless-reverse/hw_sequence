#include "hardware_controller.h"
#include <QDebug>

HardwareController::HardwareController(QObject *parent) : QObject(parent) {
    m_socket = new QTcpSocket(this);
    
    connect(m_socket, &QTcpSocket::connected, this, &HardwareController::onSocketConnected);
    connect(m_socket, QOverload<QTcpSocket::SocketError>::of(&QTcpSocket::errorOccurred), this, &HardwareController::onSocketError);}

void HardwareController::setState(ControllerState s) {if (m_state == s) return;m_state = s;emit stateChanged(m_state);}

void HardwareController::connectToDevice(const QString &serial) {
	m_deviceSerial = serial;
	if (m_deviceSerial.isEmpty()) {
		emit logMessage("Serial number is empty!", true);
	return;}
setupAdbTunnel();}

void HardwareController::setupAdbTunnel() {
    setState(ControllerState::SettingUpTunnel);
    QProcess *adb = new QProcess(this);
    connect(adb, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), [this, adb](int exitCode) {
        if (exitCode == 0) {
            startRemoteDaemon();
        } else {
            emit logMessage("ADB Tunnel Failed", true);
            setState(ControllerState::Error);}
        adb->deleteLater();});
    adb->start("adb", {"-s", m_deviceSerial, "forward", "tcp:22222", "tcp:22222"});}

void HardwareController::startRemoteDaemon() {
    setState(ControllerState::DeployingDaemon);
//	QString cmd = "su -c 'nohup /data/local/tmp/hw_resident -d > /dev/null 2>&1 &'";
	QString cmd = "su -c 'nohup /system/bin/hw_resident -d > /dev/null 2>&1 &'";
    QProcess::startDetached("adb", {"-s", m_deviceSerial, "shell", cmd});
    QTimer::singleShot(800, this, [this]() {
        setState(ControllerState::Connecting);
        m_socket->connectToHost("127.0.0.1", 22222);});}

void HardwareController::onSocketConnected() {
    setState(ControllerState::Connected);
    emit logMessage("[HW controller: connected (Kernel Level)");}

void HardwareController::sendAction(const ControlPacket &pkt) {
    if (m_state != ControllerState::Connected) {
        emit logMessage("Cannot send action: Not connected", true);
        return;}
    m_socket->write(reinterpret_cast<const char*>(&pkt), sizeof(ControlPacket));}

void HardwareController::onSocketError(QAbstractSocket::SocketError error) {
    emit logMessage(QString("Socket Error: %1").arg(m_socket->errorString()), true);
    setState(ControllerState::Error);}

void HardwareController::processNextDeploymentStep() {
    // puste
}
