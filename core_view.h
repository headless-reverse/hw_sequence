#ifndef CORE_VIEW_H
#define CORE_VIEW_H

#include <QDockWidget>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QLineEdit>
#include <QVBoxLayout>
#include "core.h"

class CoreView : public QDockWidget {
    Q_OBJECT
public:
    explicit CoreView(Core *core, QWidget *parent = nullptr);

    void updateStatus(bool attached);

private slots:
    // Slot odbierający dane ze struktury shm_bridge_t
    void handleUpdate(uint32_t temp, uint64_t freq);
    void onAttachClicked();
    void onBoostClicked();
    void onSafeModeClicked();

private:

    Core *m_core;
    QLabel      *m_statusLed;
    QLineEdit   *m_ipEdit;
    QPushButton *m_btnConnect;
    QProgressBar *m_tempBar;
    QLabel       *m_freqValue;
    QPushButton *m_boostBtn;
    QPushButton *m_safeBtn;
    QLabel *m_modelLabel;
};

#endif // CORE_VIEW_H
