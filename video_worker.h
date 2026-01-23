#ifndef VIDEO_WORKER_H
#define VIDEO_WORKER_H

#include <QObject>
#include <QTcpSocket>
#include <QByteArray>
#include "h264decoder.h"

class VideoWorker : public QObject {
    Q_OBJECT
public:
    explicit VideoWorker(H264Decoder *decoder, QObject *parent = nullptr);

public slots:
    void startStream(const QString &deviceSerial, int localPort, int devicePort, const QString &adbPath, int w, int h);
    void stopStream();

signals:
    void frameReady(AVFramePtr frame);
    void statusUpdate(const QString &msg, bool isError = false);
	void finished();
	
	void remoteTouchEvent(uint16_t axis, uint16_t value);
	void remoteTouchFinished();

private slots:
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketReadyRead();
    void onSocketError(QTcpSocket::SocketError error);
    void onFrameReady(AVFramePtr frame);

private:
    QTcpSocket m_socket;
    H264Decoder *m_decoder;
    QString m_deviceSerial;
    int m_localPort;
    int m_devicePort;
    
    int m_targetW;
    int m_targetH;

    QByteArray m_readBuffer;
    uint8_t m_pendingPacketType;
    uint32_t m_pendingPacketSize;
};

#endif
