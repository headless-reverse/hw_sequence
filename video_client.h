#ifndef VIDEO_CLIENT_H
#define VIDEO_CLIENT_H

#include <QObject>
#include <QThread>
#include <QProcess>
#include "h264decoder.h"

class VideoWorker;
class ControlSocket;
class SwipeCanvas;

class VideoClient : public QObject {
    Q_OBJECT
public:
    explicit VideoClient(QObject *parent = nullptr);
    ~VideoClient();

    void startStream(const QString &deviceSerial, int localPort = 7373, int devicePort = 7373, int w = 720, int h = 1280);
    void stopStream();
    void setSwipeCanvas(SwipeCanvas *canvas);
    void setAdbPath(const QString &path);
    void setDeviceSerial(const QString &serial);
	ControlSocket* controlSocket() const { return m_controlSocket; }
	VideoWorker* worker() const { return m_worker; }

signals:
    void startWorker(const QString &serial, int lPort, int dPort, const QString &adb, int w, int h);
    void stopWorker();
    void statusUpdate(const QString &msg, bool isError);
    void frameUpdated(AVFramePtr frame);
    void finished();

private slots:
    void onFrameReady(AVFramePtr frame);
    void onWorkerFinished();
    void deployAndStartAgent();
    void onRemoteTouch(uint16_t axis, uint16_t value);

private:
    bool executeAdbCommand(const QStringList &args, bool wait = true);
    bool m_isStreaming;
    QString m_deviceSerial;
    int m_localPort;
    int m_devicePort;
    QString m_adbPath;

    int m_width = 720;
    int m_height = 1280;

    QThread m_workerThread;
    VideoWorker *m_worker;
    QProcess *m_agentProcess = nullptr;
    ControlSocket *m_controlSocket;
    SwipeCanvas *m_swipeCanvas = nullptr;
};

#endif
