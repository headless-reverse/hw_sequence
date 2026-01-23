#pragma once

#include <QWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QTimer>

class SequenceRunner;
class CommandExecutor;

class ViewSequencerunner : public QWidget {
    Q_OBJECT
public:
    explicit ViewSequencerunner(SequenceRunner *runner, CommandExecutor *executor, QWidget *parent = nullptr);
    ~ViewSequencerunner() override = default;

    void loadSequenceFromFile(const QString &path);

signals:
    void requestShowPreview(const QStringList &commands);

private slots:
    void onLoadClicked();
    void onPreviewClicked();
    void onRunClicked();
    void onStopClicked();
    void onIntervalToggled(bool checked);
    void onIntervalValueChanged(int val);
    void handleSequenceStarted();
	void handleSequenceFinished(bool success);
	void handleSequenceFinishedWithTime(bool success, qint64 elapsedMs);
    void updateFromRunner();
    void handleScheduleRestart(int ms);
    void onCountdownTick();

private:
    SequenceRunner *m_runner = nullptr;
	CommandExecutor *m_executor = nullptr;
    QPushButton *m_loadBtn = nullptr;
    QPushButton *m_previewBtn = nullptr;
    QPushButton *m_runBtn = nullptr;
	QPushButton *m_stopBtn = nullptr;
	QCheckBox *m_measureTimeCheck;
	QLabel *m_statusLabel = nullptr;
    QCheckBox *m_intervalToggle = nullptr;
    QSpinBox *m_intervalSpin = nullptr;
    QLabel *m_intervalLabel = nullptr;
    QTimer *m_countdownTimer = nullptr;
    int m_remainingMs = 0;
};
