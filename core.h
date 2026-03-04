#ifndef CORE_H
#define CORE_H

#include <QObject>
#include <QTimer>
#include <stdint.h>
#include <pthread.h>
#include <sys/mman.h>
#include <fcntl.h>

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
    void setVoltage(uint8_t type, uint64_t microvolts);

signals:
    void telemetryUpdated(uint32_t temp, uint64_t freq);
    void statusMessage(const QString &msg);

public slots:
    void updateTick();

private:
    QTimer *m_timer;
    shm_bridge_t *m_bridge = nullptr;
};

#endif
