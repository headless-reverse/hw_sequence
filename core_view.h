#ifndef CORE_VIEW_H
#define CORE_VIEW_H

#include <QDockWidget>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QLineEdit>
#include <QSlider>
#include <QVBoxLayout>
#include <QPlainTextEdit>
#include "core.h"

class CoreView : public QDockWidget {
    Q_OBJECT
public:
    explicit CoreView(Core *core, QWidget *parent = nullptr);
    void updateStatus(bool attached);

private slots:
    void handleUpdate(uint32_t temp, uint64_t freq);
    void onAttachClicked();
    void onBoostClicked();
    void onSafeModeClicked();
    void browseLuaScript();
    void runLuaScript();
    void stopLuaScript();
    void appendLuaLog(const QString &line, bool isError);

private:
    Core *m_core = nullptr;
    QLineEdit *m_ipEdit = nullptr;
    QPushButton *m_btnConnect = nullptr;
    QLabel *m_statusLed = nullptr;
    QLabel *m_modelLabel = nullptr;
    QProgressBar *m_tempBar = nullptr;
    QLabel *m_freqValue = nullptr;
    QLabel *m_batteryValue = nullptr;
    QPushButton *m_boostBtn = nullptr;
    QPushButton *m_safeBtn = nullptr;
    QLabel *m_preciseVoltLabel = nullptr;
    QSlider *m_preciseVoltSlider = nullptr;
    QPushButton *m_applyVoltBtn = nullptr;

    QLineEdit *m_luaPathEdit = nullptr;
    QLineEdit *m_luaArgsEdit = nullptr;
    QLineEdit *m_luaInterpreterEdit = nullptr;
    QPushButton *m_luaBrowseBtn = nullptr;
    QPushButton *m_luaRunBtn = nullptr;
    QPushButton *m_luaStopBtn = nullptr;
    QPlainTextEdit *m_luaLog = nullptr;
};

#endif // CORE_VIEW_H
