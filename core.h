#ifndef CORE_H
#define CORE_H

#include <QObject>
#include <QTimer>
#include <QString>
#include <QStringList>
#include <QAbstractSocket>
#include <QProcess>
#include <stdint.h>
#include <pthread.h>
#include <sys/mman.h>
#include <fcntl.h>

class QTcpSocket;

#pragma pack(push, 1)
typedef struct {
    pthread_mutex_t lock;      
    uint32_t cpu_temp;         
    uint64_t cpu_freq;         
    uint64_t target_voltage;   
    uint8_t  cmd_type;         
    uint8_t  request_pending;  
} shm_bridge_t;
#pragma pack(pop)

class Core : public QObject {
    Q_OBJECT
public:
    explicit Core(QObject *parent = nullptr);
    ~Core();
    
    bool initBridge(int memfd);
    void connectToHardware(const QString &ip, int port);
    void disconnectHardware();
    void setVoltage(uint8_t type, uint64_t microvolts);
    void sendHardwareSignal(uint8_t type, uint32_t value1, uint32_t value2);
    bool isHardwareConnected() const;

    void setLuaInterpreter(const QString &interpreter);
    QString luaInterpreter() const { return m_luaInterpreter; }
    bool runLuaScript(const QString &scriptPath, const QStringList &args = {});
    void stopLuaScript();

signals:
    void telemetryUpdated(uint32_t temp, uint64_t freq);
    void statusMessage(const QString &msg);
    void hardwareConnectionChanged(bool connected);
    void luaScriptStarted(const QString &scriptPath);
    void luaScriptFinished(const QString &scriptPath, int exitCode);
    void luaScriptOutput(const QString &line, bool isError);

public slots:
    void updateTick();

private slots:
    void onDataReceived();
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketErrorOccurred(QAbstractSocket::SocketError error);
    void onLuaReadyReadStdOut();
    void onLuaReadyReadStdErr();
    void onLuaFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    QTimer *m_timer;
    shm_bridge_t *m_bridge = nullptr;
    QTcpSocket *m_socket = nullptr;
    QProcess *m_luaProcess = nullptr;
    QString m_luaInterpreter = QStringLiteral("lua");
    QString m_currentLuaScript;
};

#endif
