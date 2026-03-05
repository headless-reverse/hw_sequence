#include "core.h"
#include "control_protocol.h"
#include <QDebug>
#include <QProcess>
#include <QFileInfo>
#include <QTcpSocket>
#include <QtEndian>
#include <sys/mman.h>
#include <unistd.h>

Core::Core(QObject *parent) : QObject(parent) {
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &Core::updateTick);

    m_socket = new QTcpSocket(this);
    connect(m_socket, &QTcpSocket::readyRead, this, &Core::onDataReceived);
    connect(m_socket, &QTcpSocket::connected, this, &Core::onSocketConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &Core::onSocketDisconnected);
    connect(m_socket, &QAbstractSocket::errorOccurred, this, &Core::onSocketErrorOccurred);

    m_luaProcess = new QProcess(this);
    connect(m_luaProcess, &QProcess::readyReadStandardOutput, this, &Core::onLuaReadyReadStdOut);
    connect(m_luaProcess, &QProcess::readyReadStandardError, this, &Core::onLuaReadyReadStdErr);
    connect(m_luaProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &Core::onLuaFinished);
}

Core::~Core() {
    if (m_bridge) {
        munmap(m_bridge, sizeof(shm_bridge_t));
    }
}

bool Core::initBridge(int memfd) {
    if (memfd < 0) return false;
    void* ptr = mmap(NULL, sizeof(shm_bridge_t), 
                     PROT_READ | PROT_WRITE, MAP_SHARED, memfd, 0);
    if (ptr == MAP_FAILED) {
        qDebug() << "Core: mmap failed!" << strerror(errno);
        return false;
    }
    m_bridge = static_cast<shm_bridge_t*>(ptr);
    m_timer->start(100); 
    return true;
}

void Core::connectToHardware(const QString &ip, int port) {
    if (!m_socket) return;
    if (m_socket->state() == QAbstractSocket::ConnectedState) {
        m_socket->disconnectFromHost();
    }
    emit statusMessage(QString("Core: connecting to daemon %1:%2 ...").arg(ip).arg(port));
    m_socket->connectToHost(ip, static_cast<quint16>(port));
}

void Core::disconnectHardware() {
    if (!m_socket) return;
    if (m_socket->state() == QAbstractSocket::ConnectedState ||
        m_socket->state() == QAbstractSocket::ConnectingState) {
        m_socket->disconnectFromHost();
    }
}

void Core::updateTick() {
    if (!m_bridge) return;
    uint32_t temp = m_bridge->cpu_temp;
    uint64_t freq = m_bridge->cpu_freq;
    emit telemetryUpdated(temp, freq);
}

void Core::setVoltage(uint8_t type, uint64_t uv) {
    if (!m_bridge) return;
    pthread_mutex_lock(&m_bridge->lock);
    m_bridge->target_voltage = uv;
    m_bridge->cmd_type = type;
    __atomic_store_n(&m_bridge->request_pending, 1, __ATOMIC_RELEASE);
    pthread_mutex_unlock(&m_bridge->lock);
    qDebug() << "Core: Voltage request sent:" << uv;
    emit statusMessage(QString("Requested %1 uV").arg(uv));
}

void Core::sendHardwareSignal(uint8_t type, uint32_t value1, uint32_t value2) {
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) {
        emit statusMessage("Error: Not connected to Hardware Daemon");
        return;
    }
    ControlPacket pkt{};
    pkt.head = PROTOCOL_HEAD;
    pkt.magic = qToBigEndian<uint32_t>(CONTROL_MAGIC);
    pkt.type = type;
    pkt.x = qToBigEndian<uint16_t>((value1 >> 16) & 0xFFFF);
    pkt.y = qToBigEndian<uint16_t>(value1 & 0xFFFF);
    pkt.data = qToBigEndian<uint16_t>(value2 & 0xFFFF);
    pkt.crc = calculate_crc(pkt);
    const auto written = m_socket->write(reinterpret_cast<const char*>(&pkt), sizeof(pkt));
    if (written != sizeof(pkt)) {
        emit statusMessage("Core: failed to write full packet to daemon");
        return;
    }
    m_socket->flush();
    emit statusMessage(QString("Core signal sent: type=0x%1").arg(type, 2, 16, QLatin1Char('0')));
}

bool Core::isHardwareConnected() const {
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}


void Core::setLuaInterpreter(const QString &interpreter) {
    const QString trimmed = interpreter.trimmed();
    if (trimmed.isEmpty()) return;
    m_luaInterpreter = trimmed;
    emit statusMessage(QString("Lua interpreter set to: %1").arg(m_luaInterpreter));
}

bool Core::runLuaScript(const QString &scriptPath, const QStringList &args) {
    if (!m_luaProcess) return false;

    QFileInfo info(scriptPath);
    if (!info.exists() || !info.isFile()) {
        emit statusMessage(QString("Lua script not found: %1").arg(scriptPath));
        return false;
    }

    if (m_luaProcess->state() != QProcess::NotRunning) {
        emit statusMessage("Lua runner is busy. Stop current script first.");
        return false;
    }

    QStringList launchArgs;
    launchArgs << info.absoluteFilePath();
    launchArgs << args;

    m_currentLuaScript = info.absoluteFilePath();
    m_luaProcess->start(m_luaInterpreter, launchArgs);
    if (!m_luaProcess->waitForStarted(1200)) {
        emit statusMessage(QString("Failed to start Lua script: %1").arg(m_luaProcess->errorString()));
        m_currentLuaScript.clear();
        return false;
    }

    emit luaScriptStarted(m_currentLuaScript);
    emit statusMessage(QString("Lua started: %1").arg(m_currentLuaScript));
    return true;
}

void Core::stopLuaScript() {
    if (!m_luaProcess || m_luaProcess->state() == QProcess::NotRunning) return;
    m_luaProcess->terminate();
    if (!m_luaProcess->waitForFinished(800)) {
        m_luaProcess->kill();
    }
}

void Core::onDataReceived() {
    if (!m_socket) return;
    const QByteArray data = m_socket->readAll();
    if (data.isEmpty()) return;
    emit statusMessage(QString("Core: RX %1 bytes from daemon").arg(data.size()));
}

void Core::onSocketConnected() {
    emit statusMessage("Core: daemon connected");
    emit hardwareConnectionChanged(true);
}

void Core::onSocketDisconnected() {
    emit statusMessage("Core: daemon disconnected");
    emit hardwareConnectionChanged(false);
}

void Core::onSocketErrorOccurred(QAbstractSocket::SocketError error) {
    Q_UNUSED(error);
    if (!m_socket) return;
    emit statusMessage(QString("Core socket error: %1").arg(m_socket->errorString()));
    emit hardwareConnectionChanged(false);
}


void Core::onLuaReadyReadStdOut() {
    if (!m_luaProcess) return;
    const QString data = QString::fromUtf8(m_luaProcess->readAllStandardOutput());
    const QStringList lines = data.split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        emit luaScriptOutput(line, false);
        emit statusMessage(QString("[lua] %1").arg(line));
    }
}

void Core::onLuaReadyReadStdErr() {
    if (!m_luaProcess) return;
    const QString data = QString::fromUtf8(m_luaProcess->readAllStandardError());
    const QStringList lines = data.split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        emit luaScriptOutput(line, true);
        emit statusMessage(QString("[lua][err] %1").arg(line));
    }
}

void Core::onLuaFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    Q_UNUSED(exitStatus);
    const QString script = m_currentLuaScript;
    m_currentLuaScript.clear();
    emit luaScriptFinished(script, exitCode);
    emit statusMessage(QString("Lua finished (%1): %2").arg(exitCode).arg(script));
}
