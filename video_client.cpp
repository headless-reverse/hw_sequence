#include "video_client.h"
#include "h264decoder.h"
#include "video_worker.h"
#include "control_socket.h"
#include "swipecanvas.h"
#include "hardware_grabbed.h"

#include <iostream>
#include <QDebug>
#include <QtEndian>
#include <QProcess>
#include <QTimer>
#include <QCoreApplication>
#include <QFile>
#include <QMetaObject>
#include <QThread>

VideoClient::VideoClient(QObject *parent) : QObject(parent),
      m_isStreaming(false),
      m_localPort(7373),
      m_devicePort(7373),
      m_worker(new VideoWorker(nullptr, nullptr)),
      m_hwGrab(new HardwareGrabbed(this)),
      m_controlSocket(new ControlSocket(this))
{
    m_adbPath = "adb";
    m_remoteTimer = new QTimer(this);
    m_remoteTimer->setSingleShot(true);
    m_remoteTimer->setInterval(150);
    connect(m_remoteTimer, &QTimer::timeout, this, [this](){ emit remoteTouchFinished(); });
    m_worker->moveToThread(&m_workerThread);
    connect(m_worker, &VideoWorker::frameReady, this, &VideoClient::onFrameReady);
    connect(&m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(this, &VideoClient::startWorker, m_worker, &VideoWorker::startStream);
    connect(this, &VideoClient::stopWorker, m_worker, &VideoWorker::stopStream);
    connect(m_worker, &VideoWorker::statusUpdate, this, &VideoClient::statusUpdate);
    connect(m_worker, &VideoWorker::finished, this, &VideoClient::onWorkerFinished);
    connect(m_worker, &VideoWorker::metaReceived, this, &VideoClient::onMetaReceived);
    connect(m_worker, &VideoWorker::remoteTouchEvent, this, &VideoClient::onRemoteTouch);
	connect(m_controlSocket, &ControlSocket::remoteTouchEvent, this, &VideoClient::onRemoteTouch);
	connect(m_controlSocket, &ControlSocket::disconnected, this, [this]() {
		if (m_isStreaming && m_controlSocket && !this->thread()->isInterruptionRequested()) {
			qDebug() << "[VideoClient] Próba odnowienia...";
			QTimer::singleShot(1000, m_controlSocket, [this]() {
				if (m_isStreaming && m_controlSocket) {
					m_controlSocket->connectToLocalhost(m_localPort);
				}
			});
		}
	});
	
    m_workerThread.start();
}

VideoClient::~VideoClient() {stopStream();m_workerThread.quit();m_workerThread.wait();}

void VideoClient::onMetaReceived(int w, int h, int rot) {
    m_width = w; m_height = h;
    int devW = w;
    int devH = h;
    if (rot == 90 || rot == 270) std::swap(devW, devH);
    if (m_swipeCanvas) {m_swipeCanvas->setDeviceResolution(devW, devH);}
    emit statusUpdate(QString("META received: %1x%2 rot=%3").arg(w).arg(h).arg(rot), false);}

void VideoClient::startStream(const QString &deviceSerial, int localPort, int devicePort, int w, int h) {
    if (m_isStreaming) return;
    m_deviceSerial = deviceSerial;
    m_localPort = localPort;
    m_devicePort = devicePort;
    m_width = w;
    m_height = h;
    m_isStreaming = true;
    deployAndStartAgent();}

void VideoClient::stopStream() {
    if (!m_isStreaming) return;
    emit stopWorker();
    if (m_controlSocket) {
        m_controlSocket->disconnectFromAgent();}
    if (m_agentProcess) {
        m_agentProcess->kill();
        m_agentProcess->waitForFinished(500);
        m_agentProcess->deleteLater();
        m_agentProcess = nullptr;}
    if (!m_deviceSerial.isEmpty()) {
        executeAdbCommand(QStringList() << "-s" << m_deviceSerial << "forward" << "--remove" << QString("tcp:%1").arg(m_localPort), false);}
    m_isStreaming = false;
    emit statusUpdate("stream stopped", false);}

void VideoClient::deployAndStartAgent() {
//	const QString baseDir = QCoreApplication::applicationDirPath() + "/android";
	const QString baseDir = QCoreApplication::applicationDirPath();
    const QString jarPath = baseDir + "/sequence.jar";
    const QString deviceJarPath = "/data/local/tmp/sequence.jar";
    if (!QFile::exists(jarPath)) {
		qDebug() << "error: plik serwera nie istnieje:" << jarPath;
		emit statusUpdate("error: brak sequence.jar w katalogu głównym!", true);
//		emit statusUpdate("error: brak sequence.jar w ./android!", true);
        return;}
    if (!executeAdbCommand(QStringList() << "-s" << m_deviceSerial << "push" << jarPath << deviceJarPath))
        return;
    executeAdbCommand(QStringList() << "-s" << m_deviceSerial<< "shell"<< "chmod"<< "755"<< deviceJarPath);
    executeAdbCommand(QStringList() << "-s" << m_deviceSerial << "forward" << QString("tcp:%1").arg(m_localPort) << QString("tcp:%1").arg(m_devicePort));
    if (m_agentProcess) {
        m_agentProcess->disconnect();
        m_agentProcess->kill();
        m_agentProcess->waitForFinished(1000);
        delete m_agentProcess;
        m_agentProcess = nullptr;}
    m_agentProcess = new QProcess(this);
    QString shellCmd = QString("CLASSPATH=%1 app_process / dev.headless.sequence.Server").arg(deviceJarPath);
    QStringList args;
    args << "-s" << m_deviceSerial << "shell" << shellCmd;
    connect(m_agentProcess, &QProcess::readyReadStandardError, [this]() {
        std::cerr << "server error: "
                  << m_agentProcess->readAllStandardError().toStdString()
                  << std::endl;});
    QString program = m_adbPath.isEmpty() ? "adb" : m_adbPath;
    m_agentProcess->start(program, args);
    emit statusUpdate("android server running, connecting...", false);
    QTimer::singleShot(1200, this, [this]() {
        emit startWorker(m_deviceSerial, m_localPort, m_devicePort, m_adbPath, m_width, m_height);
        if (m_controlSocket) {
            m_controlSocket->connectToLocalhost(m_localPort);}});}

void VideoClient::onFrameReady(AVFramePtr frame) {
    if (m_swipeCanvas) {
        m_swipeCanvas->onFrameReady(frame);
    } else {
        static int nullCounter = 0;
        if (nullCounter++ % 100 == 0) {
            qDebug() << "[VideoClient] m_swipeCanvas is NULL, frames are ignored.";}}
    emit frameUpdated(frame);}

void VideoClient::onWorkerFinished() {m_isStreaming = false;emit finished();}

bool VideoClient::executeAdbCommand(const QStringList &args, bool wait) {
    QProcess adb;
    QString program = m_adbPath.isEmpty() ? "adb" : m_adbPath;
    adb.start(program, args);
    if (!wait) return adb.waitForStarted();
    if (!adb.waitForFinished(5000)) return false;
    return (adb.exitStatus() != QProcess::CrashExit && adb.exitCode() == 0);}

void VideoClient::setAdbPath(const QString &path) { m_adbPath = path; }
void VideoClient::setDeviceSerial(const QString &serial) { m_deviceSerial = serial; }
void VideoClient::setSwipeCanvas(SwipeCanvas *canvas) { m_swipeCanvas = canvas; }

void VideoClient::onRemoteTouch(uint16_t axis, uint16_t value) {
    if (m_swipeCanvas) {
        QMetaObject::invokeMethod(m_swipeCanvas, "onRemoteTouchEvent", Qt::QueuedConnection,
                                  Q_ARG(int, static_cast<int>(axis)),
                                  Q_ARG(int, static_cast<int>(value)));
    }
    if (m_hwGrab) {
        m_hwGrab->handleRawEvent(axis, value); 
    }
    m_remoteTimer->start();
    emit remoteTouchEvent(axis, value);
}
