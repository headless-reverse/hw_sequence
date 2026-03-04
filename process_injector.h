#ifndef PROCESS_INJECTOR_H
#define PROCESS_INJECTOR_H

#include <QObject>

class ViewProcessInjector;
class HardwareGrabbed;
class ControlInjector;

class ProcessInjector : public QObject {
    Q_OBJECT

public:
    explicit ProcessInjector(HardwareGrabbed *hwLogic, ViewProcessInjector *view, QObject *parent = nullptr);
    ~ProcessInjector() = default;

    HardwareGrabbed* hardwareLogic() const { return m_hwLogic; }
    ControlInjector* controlLogic() const { return m_control; }
    ViewProcessInjector* view() const { return m_view; }
    bool isConnected() const;
    bool isInjecting() const;

public slots:
    void onDeviceConnected();
    void onDeviceDisconnected();

private slots:
    void onProcessReceived(int pid, const QString &name);
    void onProcessListFinished();
    void onMemoryMapsReceived(int pid, const QString &maps);
    void onStatusMessageReceived(const QString &msg, bool isError);
    void onInjectionSuccess(int pid);
    void onInjectionFailed(int pid, const QString &error);

private:
    void setupSignalConnections();
    ViewProcessInjector *m_view;
    HardwareGrabbed     *m_hwLogic;
    ControlInjector     *m_control;
    bool                m_isConnected = false;
    int                 m_lastInjectedPid = -1;
};

#endif // PROCESS_INJECTOR_H
