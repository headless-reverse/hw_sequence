#include "process_injector.h"
#include "view_process_injector.h"
#include "hardware_grabbed.h"
#include "control_injector.h"
#include <QDebug>
#include <QTimer>

ProcessInjector::ProcessInjector(HardwareGrabbed *hwLogic, ViewProcessInjector *view, QObject *parent)
    : QObject(parent), m_view(view), m_hwLogic(hwLogic)
{
    if (!m_view || !hwLogic) {
        qWarning() << "[ProcessInjector] View or HardwareLogic is null!";
        return;
    }
    m_control = new ControlInjector(hwLogic, this);
    setupSignalConnections();
    qDebug() << "[ProcessInjector] Initialized successfully";
}

void ProcessInjector::setupSignalConnections() {

    connect(m_view, &ViewProcessInjector::refreshRequested, m_control, &ControlInjector::refreshProcessList);
    connect(m_view, &ViewProcessInjector::injectRequested, m_control, &ControlInjector::injectToPid);
    connect(m_view, &ViewProcessInjector::killRequested, m_control, &ControlInjector::killProcess);
    connect(m_view, &ViewProcessInjector::memMapsRequested, m_control, &ControlInjector::requestMemoryMaps);
    connect(m_view, &ViewProcessInjector::libraryPathChanged, m_control, &ControlInjector::setLibraryPath);
    connect(m_control, &ControlInjector::processDataReady, m_view, &ViewProcessInjector::addProcessItem);
    connect(m_control, &ControlInjector::listFinished, m_view, &ViewProcessInjector::onListFinished);
    connect(m_control, &ControlInjector::memoryMapsReady, m_view, &ViewProcessInjector::displayMemMaps);
    connect(m_control, &ControlInjector::statusMessage, m_view, &ViewProcessInjector::showStatus);
    connect(m_control, &ControlInjector::injectionSuccess, this, &ProcessInjector::onInjectionSuccess);
    connect(m_control, &ControlInjector::injectionFailed, this, &ProcessInjector::onInjectionFailed);
    connect(m_hwLogic, &HardwareGrabbed::connectedToDevice, this, &ProcessInjector::onDeviceConnected);
	connect(m_hwLogic, &HardwareGrabbed::disconnectedFromDevice, this, &ProcessInjector::onDeviceDisconnected);
	connect(m_view, &ViewProcessInjector::logRequested, this, [this](const QString &msg) {
		emit m_hwLogic->logMessage(msg, "#00FF00"); // Zielony kolor dla injectora
	});
	connect(m_control, &ControlInjector::statusMessage, this, [this](const QString &msg, bool isError) {
		emit m_hwLogic->logMessage(msg, isError ? "#F44336" : "#BCBCBC");
	});
}

void ProcessInjector::onDeviceConnected() {
    m_isConnected = true;
    m_view->setConnected(true);
    m_view->showStatus("✓ Connected to device", false);
    m_view->addLogEntry("Device connected - ready for injection");
    qDebug() << "[ProcessInjector] Device connected";
}

void ProcessInjector::onDeviceDisconnected() {
    m_isConnected = false;
    m_view->setConnected(false);
    m_view->showStatus("✗ Disconnected from device", true);
    m_view->addLogEntry("Device disconnected");
    qDebug() << "[ProcessInjector] Device disconnected";
}

void ProcessInjector::onProcessReceived(int pid, const QString &name) {
    qDebug() << "[ProcessInjector] Process received:" << pid << "-" << name;
}

void ProcessInjector::onProcessListFinished() {
    qDebug() << "[ProcessInjector] Process list finished";
}

void ProcessInjector::onMemoryMapsReceived(int pid, const QString &maps) {
    qDebug() << "[ProcessInjector] Memory maps received for PID:" << pid;
}

void ProcessInjector::onStatusMessageReceived(const QString &msg, bool isError) {
    qDebug() << "[ProcessInjector] Status:" << (isError ? "ERROR" : "INFO") << msg;
}

void ProcessInjector::onInjectionSuccess(int pid) {
    m_lastInjectedPid = pid;
    m_view->onInjectionSuccess(pid);
    qDebug() << "[ProcessInjector] Injection successful for PID:" << pid;
    QTimer::singleShot(1000, this, [this]() {
        if (m_isConnected) {
            m_view->addLogEntry("Auto-refreshing process list...");
        }
    });
}

void ProcessInjector::onInjectionFailed(int pid, const QString &error) {
    m_view->onInjectionFailed(pid, error);
    qWarning() << "[ProcessInjector] Injection failed for PID:" << pid << "-" << error;
    m_view->addLogEntry(QString("❌ Injection failed: %1").arg(error));
}

bool ProcessInjector::isConnected() const {
    return m_isConnected && m_hwLogic && m_hwLogic->isConnected();
}

bool ProcessInjector::isInjecting() const {
    // rozszerzane w przyszłości o śledzenie stanu iniekcji
    return false;
}
