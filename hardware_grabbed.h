#ifndef HARDWARE_GRABBED_H
#define HARDWARE_GRABBED_H

#include <QObject>
#include <QtEndian>
#include <QTcpSocket>
#include <QByteArray>
#include <QElapsedTimer>
#include "control_protocol.h"

class HardwareGrabbed : public QObject {
	Q_OBJECT
	friend class ControlInjector;
public:
    explicit HardwareGrabbed(QObject *parent = nullptr);

    bool connectToDevice(const QString &address = "127.0.0.1", quint16 port = 22222);
    void disconnectDevice();
    void sendTouch(uint8_t type, uint16_t x, uint16_t y, uint16_t slot = 0);
    void sendKey(uint16_t keyCode, bool pressed = true);
    void setHardwareGrab(bool enabled, int type = 0);
    void enableAdbWireless();
	bool isConnected() const;
	
	void requestProcessList();
	void requestProcessInjection(uint16_t pid);
	
    void setRecording(bool enabled) { m_isRecording = enabled; m_waitTimer.invalidate(); }
    void handleRawEvent(uint16_t axis, uint16_t value);
    void sendPacket(const ControlPacket &pkt);
    void requestMemoryMaps(int pid);
    void sendRawData(const QByteArray &data);

signals:
    void remoteTouchEvent(uint16_t axis, uint16_t value);
    void recordWaitDetected(int ms);
    void recordTapDetected(int x, int y);
    void recordSwipeDetected(int x1, int y1, int x2, int y2, int duration);
	void recordKeyDetected(int keyCode);
	
	void processReceived(int pid, const QString &name);
	void processListFinished();
	void memoryMapsReceived(int pid, const QString &maps);
	void injectionSuccess(int pid);
	void injectionFailed(int pid, const QString &reason);
	void connectedToDevice();
	void disconnectedFromDevice();
	void logMessage(const QString &text, const QString &color = "");

private slots:
    void onReadyRead();

private:
    QTcpSocket *m_socket;
	QByteArray m_readBuffer;
	
	QMap<int, QString> m_procNameBuffer;
	QMap<int, QByteArray> m_mapsBuffer;
	
    uint8_t calculatePacketCrc(const ControlPacket &pkt);
    bool validatePacket(const ControlPacket &pkt);
    void processRecording(uint8_t type, uint16_t x, uint16_t y, uint16_t slot);
    bool m_isRecording = false;
    uint16_t m_currentX = 0;
    uint16_t m_currentY = 0;
    uint16_t m_startX = 0;
    uint16_t m_startY = 0;

    QElapsedTimer m_gestureTimer;
    QElapsedTimer m_waitTimer;

};

#endif // HARDWAREGRABBED_H 
