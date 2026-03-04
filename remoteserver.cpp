#include "remoteserver.h"
#include "commandexecutor.h"
#include "sequencerunner.h"
#include "hardware_grabbed.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <QFile>
#include <QBuffer>
#include <QHostAddress>
#include <QDateTime>
#include <QTimer>
#include <QTcpServer>
#include <QFileInfo>
#include <QCoreApplication>
#include <QDir>
#include <QThread>

RemoteServer::RemoteServer(const QString &adbPath, const QString &targetSerial, quint16 port, QObject *parent)
    : QObject(parent){
    m_executor = new CommandExecutor(this);
    m_executor->setAdbPath(adbPath);
    if (!targetSerial.isEmpty()) {
        m_executor->setTargetDevice(targetSerial);}
    m_runner = new SequenceRunner(m_executor, this);
    connect(m_runner, &SequenceRunner::logMessage, this, &RemoteServer::onRunnerLog);
    connect(m_runner, &SequenceRunner::sequenceFinished, this, &RemoteServer::onRunnerFinished);
    m_wsServer = new QWebSocketServer(QStringLiteral("AdbSequenceServer"),
                                      QWebSocketServer::NonSecureMode, this);
    if (m_wsServer->listen(QHostAddress::Any, port)) {
        qDebug() << "RemoteServer listening on ws://0.0.0.0:" << port;
        connect(m_wsServer, &QWebSocketServer::newConnection, this, &RemoteServer::onNewConnection);
    } else {
        qCritical() << "RemoteServer failed to listen on port" << port << ":" << m_wsServer->errorString();}
    m_httpServer = new QTcpServer(this);
    m_httpPort = (port == 80) ? 8080 : (port + 1);
    if (!m_httpServer->listen(QHostAddress::Any, m_httpPort)) {
        m_httpPort = 8080;
        m_httpServer->listen(QHostAddress::Any, m_httpPort);}
    if (m_httpServer->isListening()) {
        qDebug() << "HTTP server on http://0.0.0.0:" << m_httpPort;
        connect(m_httpServer, &QTcpServer::newConnection, this, &RemoteServer::onHttpNewConnection);
    } else {
        qCritical() << "HTTP server failed to start:" << m_httpServer->errorString();}
    m_agentSocket = new QTcpSocket(this);
    connect(m_agentSocket, &QTcpSocket::connected, this, &RemoteServer::onAgentConnected);
    connect(m_agentSocket, &QTcpSocket::disconnected, this, &RemoteServer::onAgentDisconnected);
    connect(m_agentSocket, &QTcpSocket::readyRead, this, &RemoteServer::onAgentReadyRead);
    connect(m_agentSocket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
            this, &RemoteServer::onAgentError);}

RemoteServer::~RemoteServer() {
    if (m_wsServer) m_wsServer->close();
    if (m_httpServer) m_httpServer->close();
    stopAgentAndDisconnect();
}

void RemoteServer::onNewConnection() {
    QWebSocket *socket = m_wsServer->nextPendingConnection();
    if (!socket) return;
    qDebug() << "[RemoteServer] New WS client from" << socket->peerAddress().toString();
    connect(socket, &QWebSocket::textMessageReceived, this, &RemoteServer::onTextMessageReceived);
    connect(socket, &QWebSocket::binaryMessageReceived, this, &RemoteServer::onBinaryMessageReceived);
    connect(socket, &QWebSocket::disconnected, this, &RemoteServer::onSocketDisconnected);
    m_clients << socket;
    QJsonObject st = createFullState();
    QJsonDocument doc(st);
    socket->sendTextMessage(doc.toJson(QJsonDocument::Compact));
    socket->sendTextMessage(QJsonDocument(createStatusMessage(QStringLiteral("connected"), QStringLiteral("Connected to AdbSequence remote server."))).toJson(QJsonDocument::Compact));
    if (m_clients.size() == 1) {
        QTimer::singleShot(50, this, &RemoteServer::startAgentAndConnect);}}

void RemoteServer::onSocketDisconnected() {
    QWebSocket *client = qobject_cast<QWebSocket*>(sender());
    if (!client) return;
    m_clients.removeAll(client);
    client->deleteLater();
    if (m_clients.isEmpty()) {
        stopAgentAndDisconnect();}}

void RemoteServer::onTextMessageReceived(const QString &message) {
    QWebSocket *client = qobject_cast<QWebSocket*>(sender());
    if (!client) return;
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (doc.isNull() || !doc.isObject()) return;
    handleCommand(client, doc.object());
}

void RemoteServer::onBinaryMessageReceived(const QByteArray &message) {Q_UNUSED(message);}

void RemoteServer::onRawVideoPacketReceived(const QByteArray &packet) {Q_UNUSED(packet);}

void RemoteServer::onAgentReadyRead() {
    m_agentBuffer.append(m_agentSocket->readAll());
    while (true) {
        if (m_agentBuffer.size() < 5) break;
        quint8 type = static_cast<quint8>(m_agentBuffer.at(0));
        const uchar *ptr = reinterpret_cast<const uchar*>(m_agentBuffer.constData() + 1);
        quint32 payloadSize = (static_cast<quint32>(ptr[0]) << 24) | 
                              (static_cast<quint32>(ptr[1]) << 16) | 
                              (static_cast<quint32>(ptr[2]) << 8)  | 
                               static_cast<quint32>(ptr[3]);

        if (m_agentBuffer.size() < static_cast<int>(5 + payloadSize)) break;
        QByteArray framed = m_agentBuffer.left(5 + payloadSize);
        m_agentBuffer.remove(0, 5 + payloadSize);
        for (QWebSocket *client : m_clients) {
            if (client && client->isValid()) {
                client->sendBinaryMessage(framed);
            }
        }
    }
}

void RemoteServer::handleCommand(QWebSocket *sender, const QJsonObject &json) {
    QString command = json["command"].toString();
    QJsonObject payload = json["payload"].toObject();
	QString serial = payload["serial"].toString();
	HardwareGrabbed* hw = m_deviceInterfaces.value(serial);
    if (command == "hw_tap") {
        if (hw && hw->isConnected()) {
            hw->sendTouch(2, payload["x"].toInt(), payload["y"].toInt());
            QThread::msleep(50);
            hw->sendTouch(3, payload["x"].toInt(), payload["y"].toInt());
        }
    } 
    else if (command == "hw_swipe") {
        if (hw && hw->isConnected()) {
            hw->sendTouch(2, payload["x1"].toInt(), payload["y1"].toInt());
            QThread::msleep(100);
            hw->sendTouch(4, payload["x2"].toInt(), payload["y2"].toInt());
            QThread::msleep(payload["duration"].toInt());
            hw->sendTouch(3, payload["x2"].toInt(), payload["y2"].toInt());
        }
    }
    else if (command == "broadcast_key") {
        uint16_t key = payload["keyCode"].toInt();
        for (HardwareGrabbed* d : m_deviceInterfaces.values()) {
            if (d && d->isConnected()) {
                d->sendKey(key, true);
                QThread::msleep(10);
                d->sendKey(key, false);
            }
        }
    }
}

bool RemoteServer::deployAgentJar() {
    QString jarPath = "/opt/binaries/hw_sequence_beta/sequence.jar";
    QString adb = m_executor->adbPath();
    QProcess push;
    QStringList args;
    if (!m_executor->targetDevice().isEmpty()) args << "-s" << m_executor->targetDevice();
    args << "push" << jarPath << "/data/local/tmp/sequence.jar";
    push.start(adb, args);
    return push.waitForFinished() && push.exitCode() == 0;
}

void RemoteServer::startAgentAndConnect() {
    if (!deployAgentJar()) {
        qCritical() << "Failed to deploy JAR to device";
        return;
    }
    QString adb = m_executor->adbPath();
    QString serial = m_executor->targetDevice();
    QStringList adbPrefix;
    if (!serial.isEmpty()) adbPrefix << "-s" << serial;
    QProcess::execute(adb, adbPrefix << "forward" << QString("tcp:%1").arg(m_localPort) << QString("tcp:%1").arg(m_devicePort));
    m_agentProcess = new QProcess(this);
    QString shellCmd = "CLASSPATH=/data/local/tmp/sequence.jar app_process /data/local/tmp dev.headless.sequence.Server";
    connect(m_agentProcess, &QProcess::errorOccurred, this, &RemoteServer::onAgentProcessError);
    m_agentProcess->start(adb, adbPrefix << "shell" << shellCmd);
    qDebug() << "Agent process started, waiting for socket...";
    QTimer::singleShot(1500, this, [this]() {
        m_agentSocket->connectToHost(QHostAddress::LocalHost, m_localPort);
    });
}

void RemoteServer::stopAgentAndDisconnect() {
    if (m_agentSocket) {
        m_agentSocket->disconnect();
        m_agentSocket->abort();}
    if (m_agentProcess) {
        m_agentProcess->terminate();
        if (!m_agentProcess->waitForFinished(1000)) {
            m_agentProcess->kill();}
        m_agentProcess->deleteLater();
        m_agentProcess = nullptr;}}

void RemoteServer::onAgentConnected() {
    qDebug() << "[RemoteServer] Agent Connected via TCP, sending handshake...";
    QDataStream out(m_agentSocket);
    out.setByteOrder(QDataStream::BigEndian);
    quint32 w = 720;
    quint32 h = 1280;
    quint32 bitrate = 4000000;
    out << w;
    out << h;
    out << bitrate;
    m_agentSocket->flush();
}

void RemoteServer::onAgentDisconnected() { qDebug() << "Agent Disconnected"; }
void RemoteServer::onAgentError(QAbstractSocket::SocketError err) { Q_UNUSED(err); }
void RemoteServer::onAgentProcessFinished(int exitCode, QProcess::ExitStatus es) { Q_UNUSED(es); qDebug() << "Exit code:" << exitCode; }
void RemoteServer::onAgentProcessError(QProcess::ProcessError err) { Q_UNUSED(err); }

void RemoteServer::onRunnerLog(const QString &text, const QString &color) {
    Q_UNUSED(color);
    sendMessageToAll(QJsonDocument(createLogMessage(text)).toJson(QJsonDocument::Compact));}

void RemoteServer::onRunnerFinished(bool success) {
    sendMessageToAll(QJsonDocument(createStatusMessage(QStringLiteral("finished"), success ? "Success" : "Failed")).toJson());
}

void RemoteServer::sendMessageToAll(const QString &message) {
    for (QWebSocket *client : m_clients) {
        if (client && client->isValid()) client->sendTextMessage(message);}}

QJsonObject RemoteServer::createLogMessage(const QString &text, const QString &type) const {
    QJsonObject json;
    json["type"] = "log";
    json["message"] = text;
    json["logType"] = type;
    json["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    return json;}

QJsonObject RemoteServer::createStatusMessage(const QString &status, const QString &message) const {
    QJsonObject json;
    json["type"] = "status";
    json["status"] = status;
    json["message"] = message;
    return json;}

QJsonObject RemoteServer::createFullState() const {
    QJsonObject root;
    QJsonObject exec;
    exec["adbPath"] = m_executor ? m_executor->adbPath() : QString();
    exec["targetDevice"] = m_executor ? m_executor->targetDevice() : QString();
    root["executor"] = exec;
    QJsonObject runner;
    if (m_runner) {
        runner["isRunning"] = m_runner->isRunning();
        runner["commandCount"] = m_runner->commandCount();
    } else {
        runner["isRunning"] = false;
        runner["commandCount"] = 0;
    }
    root["runner"] = runner;
	root["server"] = QString("ws://%1:%2").arg(QHostAddress(QHostAddress::Any).toString(), QString::number(m_wsServer->serverPort()));
	root["http"] = QString("http://%1:%2").arg(QHostAddress(QHostAddress::Any).toString(), QString::number(m_httpPort));
    return root;}

void RemoteServer::onHttpNewConnection() {
    QTcpSocket *sock = m_httpServer->nextPendingConnection();
    if (!sock) return;
    connect(sock, &QTcpSocket::disconnected, sock, &QTcpSocket::deleteLater);
    if (!sock->waitForReadyRead(200)) {
        sock->disconnectFromHost();
        return;}
    QByteArray request = sock->readAll();
    QList<QByteArray> lines = request.split('\n');
    QString path = "/";
    if (!lines.isEmpty()) {
        QList<QByteArray> parts = lines.first().split(' ');
        if (parts.size() >= 2) path = QString::fromUtf8(parts.at(1));}
    if (path == "/") path = "/index.html";
    QString baseDir = QCoreApplication::applicationDirPath() + "/www";
    QString fullPath = QDir(baseDir).filePath(path.mid(1));
    QFile f(fullPath);
    QByteArray body;
    QString contentType = "text/html; charset=utf-8";
    if (f.exists() && f.open(QIODevice::ReadOnly)) {
        body = f.readAll();
        if (fullPath.endsWith(".js")) contentType = "application/javascript; charset=utf-8";
        else if (fullPath.endsWith(".css")) contentType = "text/css; charset=utf-8";
        else if (fullPath.endsWith(".png")) contentType = "image/png";
        else if (fullPath.endsWith(".svg")) contentType = "image/svg+xml";
    } else {
        QByteArray fallback = R"html(
<!doctype html>
<html>
<head><meta charset="utf-8"><title>AdbSequence Remote</title></head>
<body>
<h1>AdbSequence Remote Server</h1>
<p>WebSocket: connect to <code>ws://{host}:</code> see server logs for port.</p>
</body>
</html>
)html";
        body = fallback;
    }
    QByteArray response;
    response.append("HTTP/1.1 200 OK\r\n");
    response.append("Content-Type: " + contentType.toUtf8() + "\r\n");
    response.append(QString("Content-Length: %1\r\n").arg(body.size()).toUtf8());
    response.append("Connection: close\r\n");
    response.append("\r\n");
    response.append(body);
    sock->write(response);
    sock->flush();
    sock->disconnectFromHost();
}
