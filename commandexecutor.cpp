#include "commandexecutor.h"
#include "control_protocol.h"
#include "hardware_grabbed.h"
#include "control_protocol.h"
#include <QByteArray>
#include <QDebug>
#include <QProcess>
#include <QCoreApplication>
#include "adb_client.h" 

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

bool CommandExecutor::isRunning() const {return m_process && m_process->state() == QProcess::Running; }

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

void CommandExecutor::executeAdbCommand(const QString &command) {
    QStringList args = command.split(' ', Qt::SkipEmptyParts);
    runAdbCommand(args);}

void CommandExecutor::executeShellCommand(const QString &command) {
    ensureShellRunning();
    if (m_shellProcess->state() != QProcess::Running) {
        qCritical() << "Cannot execute shell command, persistent shell is not running.";
        return;}
    QByteArray cmdData = (command + "\n").toUtf8();
    m_shellProcess->write(cmdData);}

void CommandExecutor::executeRootShellCommand(const QString &command) {
    runAdbCommand(QStringList() << "shell" << "su" << "-c" << command);}

void CommandExecutor::ensureShellRunning() {
    if (m_shellProcess && m_shellProcess->state() == QProcess::Running) {
        return;}
    qDebug() << "Starting persistent ADB shell process...";
    QStringList args;
    if (!m_targetSerial.isEmpty()) {
        args << "-s" << m_targetSerial;}
    args << "shell";
    m_shellProcess->start(m_adbPath, args);
    if (!m_shellProcess->waitForStarted(5000)) {    
        qCritical() << "Failed to start persistent ADB shell!";
    } else {
        qDebug() << "Persistent ADB shell started.";}}

void CommandExecutor::onShellProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    qWarning() << "Persistent Shell process finished unexpectedly. Exit code:" << exitCode << "Status:" << exitStatus;
}

void CommandExecutor::onAdbClientError(const QString &message) {
    emit errorReceived(QString("[ADB SOCKET ERROR] %1").arg(message));
    emit finished(1, QProcess::NormalExit); }

void CommandExecutor::onAdbClientRawDataReady(const QByteArray &data) {
    emit rawDataReady(data);}

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
    QString mode = runMode.toLower();
    QString cmd = command.trimmed();
    if (cmd.isEmpty()) return;
    if (mode == "hw" || mode == "ioctl") {
        if (cmd.startsWith("HW_SOCKET_SEND ")) {
            bool ok;
            uint16_t code = cmd.mid(15).toUInt(&ok);
            if (ok) sendHardwareKey(code);
            return;
        }
        QString hwCmd = cmd;
        if (hwCmd.startsWith("input ")) hwCmd = hwCmd.mid(6);
        if (hwCmd.startsWith("key ")) {
            sendHardwareKey(hwCmd.mid(4).toUInt());
        } 
        else if (hwCmd.startsWith("tap ")) {
            QStringList pts = hwCmd.mid(4).split(' ', Qt::SkipEmptyParts);
            if (pts.size() >= 2) {
                sendHardwareTouch(EVENT_TYPE_TOUCH_DOWN, pts[0].toUInt(), pts[1].toUInt());
                sendHardwareTouch(EVENT_TYPE_TOUCH_UP, pts[0].toUInt(), pts[1].toUInt());
            }
        }
        return;
    }
    QString finalCmd = cmd;
    if (finalCmd.startsWith("adb ")) {
        finalCmd = finalCmd.mid(4).trimmed();
    }
    QStringList args = ArgsParser::parse(finalCmd);
    if (args.isEmpty()) return;
    if (mode == "shell") {
        runAdbCommand(QStringList() << "shell" << args);
    } else if (mode == "root") {
        runAdbCommand(QStringList() << "shell" << "su" << "-c" << args.join(" "));
    } else {
        runAdbCommand(args);
    }
}

void CommandExecutor::sendHardwareKey(uint16_t code) {
    if (m_hwGrab && m_hwGrab->isConnected()) {
        m_hwGrab->sendKey(code, true);
        m_hwGrab->sendKey(code, false);
        emit outputReceived(QString("[HW SOCKET] Key: %1").arg(code));
        emit finished(0, QProcess::NormalExit);
    } else if (m_hwController && m_hwController->state() == ControllerState::Connected) {
        m_hwController->sendAction(createKeyPacket(code));
        emit outputReceived(QString("[HW IOCTL] Key: %1").arg(code));
        emit finished(0, QProcess::NormalExit);
    } else {
        emit errorReceived("[Socket] BŁĄD:połączenia ze sterownikiem (Socket/Ioctl)");
        emit finished(1, QProcess::NormalExit);
    }
}

void CommandExecutor::sendHardwareTouch(uint8_t type, uint16_t x, uint16_t y) {
    if (m_hwGrab && m_hwGrab->isConnected()) {
        m_hwGrab->sendTouch(type, x, y, 0);
    } else if (m_hwController && m_hwController->state() == ControllerState::Connected) {
        m_hwController->sendAction(createTouchPacket(static_cast<ControlEventType>(type), x, y, 0));
    }
}
