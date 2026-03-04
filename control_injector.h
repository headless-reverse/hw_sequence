#ifndef CONTROL_INJECTOR_H
#define CONTROL_INJECTOR_H

#include <QObject>
#include <QString>
#include "control_protocol.h"

class HardwareGrabbed;

class ControlInjector : public QObject {
    Q_OBJECT

public:
    explicit ControlInjector(HardwareGrabbed *hwLogic, QObject *parent = nullptr);

    void refreshProcessList();
    void injectToPid(int pid);
    void killProcess(int pid);
    void requestMemoryMaps(int pid);
    void setLibraryPath(const QString &path);

signals:
    void processDataReady(int pid, const QString &name);
    void listFinished();
    void memoryMapsReady(int pid, const QString &formattedMaps);
	void statusMessage(const QString &msg, bool isError);
	void injectionSuccess(int pid);
	void injectionFailed(int pid, const QString &error);

private:
    HardwareGrabbed *m_hwLogic;
    int m_pendingPid = -1;
};

#endif // CONTROL_INJECTOR_H
