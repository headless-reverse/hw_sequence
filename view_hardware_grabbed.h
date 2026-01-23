#ifndef VIEW_HARDWARE_GRABBED_H
#define VIEW_HARDWARE_GRABBED_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTouchEvent>
#include <QKeyEvent>
#include <QDateTime>
#include <QCheckBox>

#include "hardware_grabbed.h"
#include "commandexecutor.h"
#include "hw_keyboard.h"

class ViewHardwareGrabbed : public QWidget {
    Q_OBJECT

public:
    explicit ViewHardwareGrabbed(CommandExecutor *executor, QWidget *parent = nullptr);
    HardwareGrabbed* hardwareLogic() const { return m_logic; }

    void setRecordModel(QObject *model);
    bool isRecordModeActive() const { return recordCheckBox->isChecked(); }

signals:
    void statusMessage(const QString &msg, bool isError);
    void hwKeyGenerated(int linuxCode);

protected:
    bool event(QEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;
	void keyReleaseEvent(QKeyEvent *event) override;

private slots:
    void onConnectClicked();
    void onKillClicked();
    void onCheckStatusClicked();
    void handleKeyAction(int linuxCode);

private:
	CommandExecutor *m_executor;
	HardwareGrabbed *m_logic;
	HardwareKeyboard *m_hwKeyboardWidget;
	QObject         *m_recordModel = nullptr;
	QLabel          *m_statusLabel;
	QPushButton     *m_connectBtn;
	QCheckBox *recordCheckBox;
};

#endif // VIEW_HARDWARE_GRABBED_H
