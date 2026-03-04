#include "control_injector.h"
#include "hardware_grabbed.h"
#include <QDebug>
#include <QTimer>

ControlInjector::ControlInjector(HardwareGrabbed *hwLogic, QObject *parent)
    : QObject(parent), m_hwLogic(hwLogic)
{
    if (m_hwLogic) {
        connect(m_hwLogic, &HardwareGrabbed::processReceived, this, &ControlInjector::processDataReady);
        connect(m_hwLogic, &HardwareGrabbed::processListFinished, this, &ControlInjector::listFinished);
		connect(m_hwLogic, &HardwareGrabbed::memoryMapsReceived, this, &ControlInjector::memoryMapsReady);
		connect(m_hwLogic, &HardwareGrabbed::injectionSuccess, this, &ControlInjector::injectionSuccess);
		connect(m_hwLogic, &HardwareGrabbed::injectionFailed, this, &ControlInjector::injectionFailed);
    }
}

void ControlInjector::setLibraryPath(const QString &path) {
    if (!m_hwLogic || !m_hwLogic->isConnected()) {
        emit statusMessage("Not connected to device", true);
        return;
    }
    QByteArray pathData = path.toUtf8();
    uint16_t pathLen = qMin((int)pathData.length(), 255);
    qDebug() << "[ControlInjector] Setting library path:" << path << "Length:" << pathLen;
    ControlPacket pkt = createTouchPacket(
        static_cast<ControlEventType>(EVENT_TYPE_SET_LIB_PATH),
        pathLen,
        0,
        0
    );
    m_hwLogic->sendPacket(pkt);
    QTimer::singleShot(50, this, [this, pathData, pathLen]() {
        if (m_hwLogic && m_hwLogic->isConnected()) {
            m_hwLogic->sendRawData(pathData);
            m_hwLogic->m_socket->flush();
            emit statusMessage(QString("Library path sent: %1").arg(QString::fromUtf8(pathData)), false);
        }
    });
}

void ControlInjector::refreshProcessList() {
    if (!m_hwLogic || !m_hwLogic->isConnected()) {
        emit statusMessage("Brak połączenia z urządzeniem!", true);
        return;
    }
    qDebug() << "[Control] Requesting process list...";
    m_hwLogic->requestProcessList();
}

void ControlInjector::injectToPid(int pid) {
    if (!m_hwLogic || pid <= 0) {
        emit statusMessage("Invalid PID", true);
        return;
    }
    if (!m_hwLogic->isConnected()) {
        emit statusMessage("Not connected to device", true);
        return;
    }
    m_pendingPid = pid;
    qDebug() << "[Control] Injecting to PID:" << pid;
    ControlPacket pkt = createTouchPacket(
        static_cast<ControlEventType>(EVENT_TYPE_INJECT_PID),
        0,
        0,
        static_cast<uint16_t>(pid)
    );
    m_hwLogic->sendPacket(pkt);
    emit statusMessage(QString("⏳ Wstrzykiwanie do PID %1...").arg(pid), false);
}

void ControlInjector::killProcess(int pid) {
    if (!m_hwLogic || !m_hwLogic->isConnected()) {
        return;
    }
    if (pid <= 0) {
        return;
    }
    qDebug() << "[Control] Killing PID:" << pid;
    ControlPacket pkt = createTouchPacket(
        static_cast<ControlEventType>(EVENT_TYPE_KILL_PROC),
        0,
        0,
        static_cast<uint16_t>(pid)
    );
    m_hwLogic->sendPacket(pkt);
    emit statusMessage(QString("💀 Wysłano sygnał KILL do %1").arg(pid), true);
}

void ControlInjector::requestMemoryMaps(int pid) {
    if (!m_hwLogic || !m_hwLogic->isConnected()) {
        return;
    }
    if (pid <= 0) {
        return;
    }
    qDebug() << "[Control] Requesting maps for PID:" << pid;
    m_hwLogic->requestMemoryMaps(pid);
    emit statusMessage(QString("Pobieranie map pamięci dla %1...").arg(pid), false);
}
