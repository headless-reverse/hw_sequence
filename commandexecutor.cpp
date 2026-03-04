#include "commandexecutor.h"
#include "control_protocol.h"
#include "hardware_grabbed.h"
#include "control_protocol.h"
#include <QByteArray>
#include <QDebug>
#include <QProcess>
#include <QCoreApplication>
#include "adb_client.h"
#include <QTimer>
#include <QRegularExpression>
#include <QThread>

CommandExecutor::CommandExecutor(QObject *parent) : QObject(parent) {
    m_adbPath = "adb";
    m_targetSerial = QString();
    m_adbClient = new AdbClient(this);
    m_hwController = nullptr;
    m_hwGrab = nullptr;
    connect(m_adbClient, &AdbClient::adbError,this, &CommandExecutor::onAdbClientError);
    connect(m_adbClient, &AdbClient::rawDataReady,this, &CommandExecutor::onAdbClientRawDataReady);
    connect(m_adbClient, &AdbClient::commandResponseReady,this, &CommandExecutor::onAdbClientCommandResponseReady);m_shellProcess = new QProcess(this);
    connect(m_shellProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &CommandExecutor::onShellProcessFinished);
    connect(m_shellProcess, &QProcess::readyReadStandardOutput, this, &CommandExecutor::readStdOut);
    connect(m_shellProcess, &QProcess::readyReadStandardError, this, &CommandExecutor::readStdErr);
}

CommandExecutor::~CommandExecutor() {stop();}

void CommandExecutor::setAdbPath(const QString &path) {if (!path.isEmpty()) m_adbPath = path;}

void CommandExecutor::setTargetDevice(const QString &serial) {
    m_targetSerial = serial;
    if (m_adbClient) {
        m_adbClient->setTargetDevice(serial);}
    if (m_shellProcess && m_shellProcess->state() == QProcess::Running) {
        m_shellProcess->kill();
        m_shellProcess->waitForFinished(500);}}

void CommandExecutor::stop() {
    if (m_process) {
        if (m_process->state() == QProcess::Running) {
            m_process->kill();
            m_process->waitForFinished(500);}
        m_process->disconnect();
        delete m_process;
        m_process = nullptr;}
    if (m_shellProcess && m_shellProcess->state() == QProcess::Running) {
        m_shellProcess->kill();    
        m_shellProcess->waitForFinished(500);}}

void CommandExecutor::cancelCurrentCommand() {stop();}

bool CommandExecutor::isRunning() const {
	return (m_process && m_process->state() == QProcess::Running)
		|| (m_shellProcess && m_shellProcess->state() == QProcess::Running);}

void CommandExecutor::runAdbCommand(const QStringList &args) {
    stop();
    m_process = new QProcess(this);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &CommandExecutor::readStdOut);
    connect(m_process, &QProcess::readyReadStandardError, this, &CommandExecutor::readStdErr);
    connect(m_process, &QProcess::started, this, &CommandExecutor::onStarted);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
    this, &CommandExecutor::onFinished);
    QStringList finalArgs;
    if (!m_targetSerial.isEmpty()) {
        finalArgs << "-s" << m_targetSerial;}
    finalArgs.append(args);
    m_process->start(m_adbPath, finalArgs);}

void CommandExecutor::executeAdbCommand(const QString &command) {QStringList args = command.split(' ', Qt::SkipEmptyParts);runAdbCommand(args);}

void CommandExecutor::executeShellCommand(const QString &command) {
    ensureShellRunning();
    if (m_shellProcess->state() != QProcess::Running) {
        qCritical() << "Cannot execute shell command, persistent shell is not running.";
        return;}
    QByteArray cmdData = (command + "\n").toUtf8();
    m_shellProcess->write(cmdData);}

void CommandExecutor::executeRootShellCommand(const QString &command) {runAdbCommand(QStringList() << "shell" << "su" << "-c" << command);}

void CommandExecutor::ensureShellRunning() {
    if (m_shellProcess && m_shellProcess->state() == QProcess::Running) {
        return;}
    qDebug() << "Starting persistent ADB shell process...";
    QStringList args;
    if (!m_targetSerial.isEmpty()) {args << "-s" << m_targetSerial;}
    args << "shell";
    m_shellProcess->start(m_adbPath, args);
    if (!m_shellProcess->waitForStarted(5000)) {    
        qCritical() << "Failed to start persistent ADB shell!";
    } else {
        qDebug() << "Persistent ADB shell started.";}}

void CommandExecutor::onShellProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    qWarning() << "Persistent Shell process finished unexpectedly. Exit code:" << exitCode << "Status:" << exitStatus;}

void CommandExecutor::onAdbClientError(const QString &message) {
    emit errorReceived(QString("[ADB SOCKET] %1").arg(message));
    emit finished(1, QProcess::NormalExit); }

void CommandExecutor::onAdbClientRawDataReady(const QByteArray &data) {emit rawDataReady(data);}

void CommandExecutor::onAdbClientCommandResponseReady(const QByteArray &response) {
    emit outputReceived(QString::fromUtf8(response));
    emit finished(0, QProcess::NormalExit);}

void CommandExecutor::readStdOut() {
    if (m_process && sender() == m_process) {    
        const QByteArray data = m_process->readAllStandardOutput();
        if (!data.isEmpty()) emit outputReceived(QString::fromUtf8(data));}    
    else if (m_shellProcess && sender() == m_shellProcess) {    
        const QByteArray data = m_shellProcess->readAllStandardOutput();
        if (!data.isEmpty()) {
            emit outputReceived("[SHELL] " + QString::fromUtf8(data));}}}

void CommandExecutor::readStdErr() {
    if (m_process && sender() == m_process) {
        const QByteArray data = m_process->readAllStandardError();
        if (!data.isEmpty()) emit errorReceived(QString::fromUtf8(data));}
    else if (m_shellProcess && sender() == m_shellProcess) {
        const QByteArray data = m_shellProcess->readAllStandardError();
        if (!data.isEmpty()) {
             emit errorReceived("[SHELL ERROR] " + QString::fromUtf8(data));}}}

void CommandExecutor::onStarted() {emit started();}

void CommandExecutor::onFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    emit finished(exitCode, exitStatus);
    if (m_process) {
        m_process->disconnect();    
        m_process->deleteLater();    
        m_process = nullptr;}}

void CommandExecutor::executeSequenceCommand(const QString &command, const QString &runMode) {
    QString mode = runMode.trimmed().toLower();
    QString cmd = command.trimmed();

    qDebug() << "DEBUG: Próba wykonania:" << cmd << "Tryb:" << mode;

    if (cmd.toLower() == "wait") { 
        emit finished(0, QProcess::NormalExit); 
        return; 
    }
    if (mode == "ioctl" || mode == "socket" || mode == "hw" || mode == "hw_direct") {
        qDebug() << "DEBUG: Wybrano ścieżkę SOCKET/IOCTL";
        emit outputReceived(QString("⮚⮚⮚ ioctl/socket ⮘⮘⮘ %1").arg(cmd));
        bool sent = false;
        QString cleanCmd = cmd;
        if (cleanCmd.startsWith("input ", Qt::CaseInsensitive)) 
            cleanCmd = cleanCmd.mid(6).trimmed();
        QStringList parts = cleanCmd.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.isEmpty()) return;
        if (m_hwController && m_hwController->state() == ControllerState::Connected) {
            if (parts[0].compare("tap", Qt::CaseInsensitive) == 0 && parts.size() >= 3) {
                uint16_t x = parts[1].toUShort();
                uint16_t y = parts[2].toUShort();
                m_hwController->sendAction(createTouchPacket(EVENT_TYPE_TOUCH_DOWN, x, y, 0));
                QThread::msleep(40);
                m_hwController->sendAction(createTouchPacket(EVENT_TYPE_TOUCH_UP, x, y, 0));
                sent = true;
            } else if (parts[0].compare("key", Qt::CaseInsensitive) == 0 || parts[0].compare("keyevent", Qt::CaseInsensitive) == 0) {
                uint16_t code = parts.last().toUShort();
                m_hwController->sendAction(createKeyPacket(code));
                sent = true;
            }
        }
        if (!sent && m_hwGrab && m_hwGrab->isConnected()) {
            if (parts[0].compare("keyevent", Qt::CaseInsensitive) == 0 || parts[0].compare("key", Qt::CaseInsensitive) == 0) {
                bool ok; 
                uint16_t code = parts.last().toUShort(&ok);
                if (ok) { 
                    m_hwGrab->sendKey(code, true); 
                    m_hwGrab->sendKey(code, false); 
                    sent = true; 
                }
            } else if (parts[0].compare("tap", Qt::CaseInsensitive) == 0 && parts.size() >= 3) {
                uint16_t x = parts[1].toUShort();
                uint16_t y = parts[2].toUShort();
                m_hwGrab->sendTouch(EVENT_TYPE_TOUCH_UP, 0, 0, 0);
                QThread::msleep(10);
                m_hwGrab->sendTouch(EVENT_TYPE_TOUCH_DOWN, x, y, 0);
                QThread::msleep(40);
                m_hwGrab->sendTouch(EVENT_TYPE_TOUCH_UP, x, y, 0);
                sent = true;
            } else if (parts[0].compare("swipe", Qt::CaseInsensitive) == 0 && parts.size() >= 5) {
                int x1 = parts[1].toInt(); int y1 = parts[2].toInt();
                int x2 = parts[3].toInt(); int y2 = parts[4].toInt();
                int duration = (parts.size() >= 6) ? parts[5].toInt() : 300;
                m_hwGrab->sendTouch(EVENT_TYPE_TOUCH_UP, 0, 0, 0);
                QThread::msleep(20);
                m_hwGrab->sendTouch(EVENT_TYPE_TOUCH_DOWN, x1, y1, 0);
                int steps = qMax(12, duration / 15);
                for (int s = 1; s <= steps; ++s) {
                    int nx = x1 + ((x2 - x1) * s) / steps;
                    int ny = y1 + ((y2 - y1) * s) / steps;
                    m_hwGrab->sendTouch(EVENT_TYPE_TOUCH_MOVE, static_cast<uint16_t>(nx), static_cast<uint16_t>(ny), 0);
                    QThread::usleep((duration * 1000) / steps);
                }
                m_hwGrab->sendTouch(EVENT_TYPE_TOUCH_UP, x2, y2, 0);
                sent = true;
            }
        }
        if (sent) {
            emit finished(0, QProcess::NormalExit);
        } else {
            emit errorReceived("[ioctl] BŁĄD: Brak połączenia lub nieznany format.");
            emit finished(1, QProcess::NormalExit);
        }
        return;
    }
    if (mode == "shell-persistent") {
        emit outputReceived(QString("⮚⮚⮚ shell-persistent ⮘⮘⮘ %1").arg(cmd));
        executeShellCommand(cmd);
        emit finished(0, QProcess::NormalExit);
    } else if (mode == "root") {
        emit outputReceived(QString("⮚⮚⮚ root ⮘⮘⮘ %1").arg(cmd));
        runAdbCommand(QStringList() << "shell" << "su" << "-c" << cmd);
    } else if (mode == "shell") {
        emit outputReceived(QString("⮚⮚⮚ shell ⮘⮘⮘ %1").arg(cmd));
        runAdbCommand(QStringList() << "shell" << cmd);
    } else {
        emit outputReceived(QString("⮚⮚⮚ adb ⮘⮘⮘ %1").arg(cmd));
        runAdbCommand(ArgsParser::parse(cmd));
    }
}

void CommandExecutor::sendHardwareTouch(uint8_t type, uint16_t x, uint16_t y) {
    if (m_hwController && m_hwController->state() == ControllerState::Connected) {
        m_hwController->sendAction(createTouchPacket(static_cast<ControlEventType>(type), x, y, 0));
        return; }
    if (m_hwGrab && m_hwGrab->isConnected()) {
        m_hwGrab->sendTouch(type, x, y, 0);
        return;}
    qDebug() << "⚠️ [Executor]: Brak połączenia sprzętowego! Dotyk utracony.";
}

void CommandExecutor::sendHardwareKey(uint16_t code) {
    if (m_hwGrab && m_hwGrab->isConnected()) {
        m_hwGrab->sendKey(code, true);
        m_hwGrab->sendKey(code, false);
        return;
    } else if (m_hwController && m_hwController->state() == ControllerState::Connected) {
        m_hwController->sendAction(createKeyPacket(code));
        return;
    } else {
        emit errorReceived("[socket] BŁĄD połączenia (hw_resident/IOCTL)");
        return;}}
