#include "video_worker.h"
#include "h264decoder.h"
#include <QHostAddress>
#include <QtEndian>
#include <QDebug>
#include <QThread>
#include <QTimer>
#include <QDataStream>
#include <QJsonDocument>
#include <QJsonObject>
#include "control_protocol.h"

static const int RECONNECT_MS = 500;
static const uint8_t TYPE_VIDEO   = 0x01;
static const uint8_t TYPE_META    = 0x02;
static const uint8_t TYPE_CONTROL = 0x03;

VideoWorker::VideoWorker(H264Decoder *decoder, QObject *parent) 
    : QObject(parent), m_decoder(decoder), m_pendingPacketSize(0), m_targetW(0), m_targetH(0) {
    
    m_socket.setParent(this); 
    connect(&m_socket, &QTcpSocket::connected, this, &VideoWorker::onSocketConnected);
    connect(&m_socket, &QTcpSocket::disconnected, this, &VideoWorker::onSocketDisconnected);
    connect(&m_socket, &QTcpSocket::readyRead, this, &VideoWorker::onSocketReadyRead);
    connect(&m_socket, QOverload<QTcpSocket::SocketError>::of(&QTcpSocket::errorOccurred), this, &VideoWorker::onSocketError);}

void VideoWorker::startStream(const QString &deviceSerial, int localPort, int devicePort, const QString &adbPath, int w, int h) {
    Q_UNUSED(adbPath);
    m_deviceSerial = deviceSerial;
    m_localPort = localPort;
    m_devicePort = devicePort;
    m_targetW = w;
    m_targetH = h;
    m_readBuffer.clear();
    m_pendingPacketSize = 0;
    if (!m_decoder) {
        qDebug() << "[video worker] creating decoder in thread:" << QThread::currentThread();
        m_decoder = new H264Decoder(this);
        connect(m_decoder, &H264Decoder::frameReady, this, &VideoWorker::onFrameReady);}
    if (!m_decoder || m_decoder->thread() != QThread::currentThread()) {
        emit statusUpdate("[video worker] critical error", true);
        emit finished();
        return;}
    emit statusUpdate(QString("connecting streaming (port %1)...").arg(localPort));
    if (m_socket.state() == QAbstractSocket::UnconnectedState) {
        m_socket.connectToHost(QHostAddress::LocalHost, m_localPort);}}

void VideoWorker::onSocketConnected() {
    QDataStream out(&m_socket);
    out.setByteOrder(QDataStream::BigEndian);
    quint32 finalW = (m_targetW > 0) ? m_targetW : 720;
    quint32 finalH = (m_targetH > 0) ? m_targetH : 1280;
    quint32 bitrate = 4000000;
    out << finalW;
    out << finalH;
    out << bitrate;
    m_socket.flush(); 
    qDebug() << "[Worker] handshake sent to android:" << finalW << "x" << finalH << "@" << bitrate;
    emit statusUpdate(QString("active stream: %1x%2").arg(finalW).arg(finalH));}

void VideoWorker::onSocketReadyRead() {
    m_readBuffer.append(m_socket.readAll());
    while (true) {
        if (m_pendingPacketSize == 0) {
            if (m_readBuffer.size() < 5) break;
            m_pendingPacketType = static_cast<uint8_t>(m_readBuffer[0]);
            quint32 rawLen = 0;
            memcpy(&rawLen, m_readBuffer.constData() + 1, 4);
            m_pendingPacketSize = qFromBigEndian<quint32>(rawLen);
            m_readBuffer.remove(0, 5);
        }
        if (m_readBuffer.size() < (int)m_pendingPacketSize) break;
        QByteArray payload = m_readBuffer.left(m_pendingPacketSize);
        m_readBuffer.remove(0, m_pendingPacketSize);
        if (m_pendingPacketType == TYPE_VIDEO) {
            if (m_decoder) {
                m_decoder->decode(payload);}
        } else if (m_pendingPacketType == TYPE_META) {
            // meta is JSON with { "w":.., "h":.., "rot":.. }
            QJsonDocument doc = QJsonDocument::fromJson(payload);
            if (doc.isObject()) {
                QJsonObject obj = doc.object();
                int w = obj.value("w").toInt(m_targetW);
                int h = obj.value("h").toInt(m_targetH);
                m_targetW = w; m_targetH = h;
                emit statusUpdate(QString("meta: %1x%2").arg(w).arg(h), false);}
        } else if (m_pendingPacketType == TYPE_CONTROL) {
            if ((int)payload.size() == sizeof(ControlPacket)) {
                ControlPacket pkt;
                memcpy(&pkt, payload.constData(), sizeof(pkt));
                if (validatePacket(pkt)) {
                    uint16_t axis = qFromBigEndian<uint16_t>(pkt.data);
                    uint16_t val = 0;
                    if (axis == 0x35) val = qFromBigEndian<uint16_t>(pkt.x);
                    else if (axis == 0x36) val = qFromBigEndian<uint16_t>(pkt.y);
                    else continue;
                    emit remoteTouchEvent(axis, val);}}}
        m_pendingPacketSize = 0;}}

void VideoWorker::onFrameReady(AVFramePtr frame) {emit frameReady(frame);}

void VideoWorker::stopStream() {m_socket.close();emit finished();}

void VideoWorker::onSocketDisconnected() {emit statusUpdate("video disconnect", true);
    if (m_targetW > 0) {
        QTimer::singleShot(RECONNECT_MS, this, [this]() {if (m_socket.state() == QAbstractSocket::UnconnectedState)
            m_socket.connectToHost(QHostAddress::LocalHost, m_localPort);});}}

void VideoWorker::onSocketError(QTcpSocket::SocketError) {qDebug() << "[Worker] socket error:" << m_socket.errorString();m_socket.close();}
