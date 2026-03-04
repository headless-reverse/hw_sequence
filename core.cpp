#include "core.h"
#include <QDebug>
#include <sys/mman.h>
#include <unistd.h>

Core::Core(QObject *parent) : QObject(parent) {
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &Core::updateTick);
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
