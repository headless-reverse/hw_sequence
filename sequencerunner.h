#ifndef SEQUENCERUNNER_H
#define SEQUENCERUNNER_H

#include <QProcess>
#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QJsonArray>
#include <QVector>
#include "commandexecutor.h"

struct SequenceCmd {
    QString command;
    int delayAfterMs = 0;
    QString runMode = "adb"; 
    bool stopOnError = true;
    QString successCommand;
    QString failureCommand;
    bool isConditionalExecution = false;
};

class SequenceRunner : public QObject {
    Q_OBJECT
public:
    explicit SequenceRunner(CommandExecutor *executor, QObject *parent = nullptr);
    ~SequenceRunner() override;

    bool appendSequence(const QString &filePath);
    void clearSequence();
    void setDiagnosticMode(bool enabled) { m_isDiagnosticMode = enabled; }
    bool startSequence();
    void stopSequence();
    void finishSequence(bool success);
    void setIntervalToggle(bool toggle);
    void setIntervalValue(int ms);
    bool isRunning() const { return m_isRunning; }
    int commandCount() const { return m_commands.count(); }
	bool loadSequenceFromJsonArray(const QJsonArray &array);
	QStringList getCommandsAsText() const;

signals:
    void sequenceStarted();
    void sequenceFinished(bool success);
    void scheduleRestart(int ms);
    void commandExecuting(const QString &cmd, int index, int total);
    void logMessage(const QString &text, const QString &color);
    void sequenceFinishedWithTime(bool success, qint64 elapsedMs);

private slots:
    void onCommandFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onDelayTimeout();

private:
    CommandExecutor *m_executor;
    QVector<SequenceCmd> m_commands;
    QTimer m_delayTimer;
    QElapsedTimer m_totalSequenceTimer;
    int m_currentIndex = 0;
    bool m_isRunning = false;
    bool m_isDiagnosticMode = false;
    bool m_isWaitingForMyCommand = false;
    bool m_isInterval = false;
    int m_intervalValueMs = 0;

    void executeNextCommand();
    SequenceCmd parseCommandFromJson(const QJsonObject &obj);
    void executeConditionalCommand(const QString& cmd, const QString& runMode, bool isSuccess);
};

#endif // SEQUENCERUNNER_H
