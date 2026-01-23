#include "sequencerunner.h"
#include "commandexecutor.h"
#include "argsparser.h"
#include "adb_client.h"
#include <QElapsedTimer>
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>
#include <QDebug>

SequenceRunner::SequenceRunner(CommandExecutor *executor, QObject *parent): QObject(parent), m_executor(executor) {
    m_delayTimer.setSingleShot(true);
    connect(&m_delayTimer, &QTimer::timeout, this, &SequenceRunner::onDelayTimeout);
    if (m_executor) {
        connect(m_executor, &CommandExecutor::finished, this, &SequenceRunner::onCommandFinished);
    }
}

SequenceRunner::~SequenceRunner() {}

SequenceCmd SequenceRunner::parseCommandFromJson(const QJsonObject &obj) {
    SequenceCmd cmd;
    cmd.command = obj.value("command").toString();
    cmd.delayAfterMs = obj.value("delayAfterMs").toInt(100);
    cmd.runMode = obj.value("runMode").toString("adb").toLower(); 
    cmd.stopOnError = obj.value("stopOnError").toBool(true);
    cmd.successCommand = obj.value("successCommand").toString();
    cmd.failureCommand = obj.value("failureCommand").toString();
    return cmd;
}

void SequenceRunner::clearSequence() {
    m_commands.clear();
    emit logMessage("sequence queue cleared.", "#BDBDBD");
}

bool SequenceRunner::appendSequence(const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit logMessage(QString("cannot open file: %1").arg(file.errorString()), "#F44336");
        return false;
    }
    QByteArray jsonData = file.readAll();
    QJsonDocument doc = QJsonDocument::fromJson(jsonData);
    if (!doc.isArray()) {
        emit logMessage("file does not contain a valid JSON array.", "#F44336");
        return false;
    }
    return loadSequenceFromJsonArray(doc.array());
}

bool SequenceRunner::loadSequenceFromJsonArray(const QJsonArray &array) {
    if (m_isRunning) {
        emit logMessage("cannot load sequence while one is running.", "#F44336");
        return false;
    }
    m_commands.clear();
    for (const QJsonValue &value : array) {
        if (value.isObject()) {
            m_commands.append(parseCommandFromJson(value.toObject()));
        } else {
            emit logMessage("invalid command format in JSON array.", "#F44336");
            return false;
        }
    }
    emit logMessage(QString("loaded %1 commands.").arg(m_commands.count()), "#4CAF50");
    return true;
}

QStringList SequenceRunner::getCommandsAsText() const {
    QStringList list;
    for (const auto &cmd : m_commands) {
        QString text = cmd.command;
        if (!cmd.successCommand.isEmpty()) {
            text += QString(" (sukces: '%1')").arg(cmd.successCommand);
        }
        if (!cmd.failureCommand.isEmpty()) {
            text += QString(" (error: '%1')").arg(cmd.failureCommand);
        }
        list.append(text);
    }
    return list;
}

bool SequenceRunner::startSequence() {
    if (m_commands.isEmpty()) {
        emit logMessage("[seq runner] Kolejka jest pusta.", "#FFC107");
        return false;
    }
    if (m_isRunning) {
        emit logMessage("[seq runner] Sekwencja już trwa.", "#FFC107");
        return true;
    }
    m_currentIndex = 0;
    m_isRunning = true;
    m_isWaitingForMyCommand = false; // Reset na start
    m_totalSequenceTimer.start(); 
    emit sequenceStarted();
    emit logMessage("-- start sekwencji (tryb HW/ioctl) --", "#009688");
    executeNextCommand();
    return true;
}

void SequenceRunner::stopSequence() {
    if (!m_isRunning) return;
    m_delayTimer.stop();
    if (m_executor) m_executor->cancelCurrentCommand();
    finishSequence(false);}

void SequenceRunner::executeNextCommand() {
    if (!m_isRunning || m_currentIndex >= m_commands.count()) {
        finishSequence(true);
        return;
    }
    const SequenceCmd &cmd = m_commands.at(m_currentIndex);
    if (cmd.isConditionalExecution) {
        emit logMessage(QString("[seq runner] executing conditional: %1").arg(cmd.command), "#FF9800");
    } else {
        emit commandExecuting(cmd.command, m_currentIndex + 1, m_commands.count());
    }
    if (m_executor) {
        m_isWaitingForMyCommand = true;
        m_executor->executeSequenceCommand(cmd.command, cmd.runMode);
    } else {
        qCritical() << "[SEQ_RUNNER] Error: Executor is NULL!";
        finishSequence(false);
    }
}

void SequenceRunner::onDelayTimeout() { executeNextCommand(); }

void SequenceRunner::executeConditionalCommand(const QString& cmd, const QString& runMode, bool isSuccess) {
    if (cmd.isEmpty()) return;
    SequenceCmd conditionalCmd;
    conditionalCmd.command = cmd;
    conditionalCmd.runMode = runMode;
    conditionalCmd.delayAfterMs = 0;
    conditionalCmd.stopOnError = true;
    conditionalCmd.isConditionalExecution = true;
    m_commands.insert(m_currentIndex + 1, conditionalCmd);
    emit logMessage(QString("[seq runner] command injection (ExitCode: %1): '%2'").arg(isSuccess ? "0 (sukces)" : "!=0 (error)", cmd), "#2196F3");
}

void SequenceRunner::onCommandFinished(int exitCode, QProcess::ExitStatus status) {
    if (!m_isRunning || !m_isWaitingForMyCommand) {
        return; 
    }
    m_isWaitingForMyCommand = false;
    if (m_currentIndex >= m_commands.count()) return;
    const SequenceCmd &currentCmd = m_commands.at(m_currentIndex);
    if (!currentCmd.isConditionalExecution) {
        if (exitCode == 0) {
            executeConditionalCommand(currentCmd.successCommand, currentCmd.runMode, true);
        } else {
            executeConditionalCommand(currentCmd.failureCommand, currentCmd.runMode, false);
        }
    }
    if (exitCode != 0 && currentCmd.stopOnError) {
        emit logMessage(QString("[seq runner] STOP: Błąd krytyczny (kod %1).").arg(exitCode), "#F44336");
        finishSequence(false);
        return;
    }
    m_currentIndex++;
    if (currentCmd.isConditionalExecution) {
        m_commands.removeAt(m_currentIndex - 1);
        m_currentIndex--;
    }
    if (m_currentIndex < m_commands.count()) {
        if (currentCmd.delayAfterMs > 0) {
            m_delayTimer.setInterval(currentCmd.delayAfterMs);
            m_delayTimer.start();
        } else {
            executeNextCommand();
        }
    } else {
        finishSequence(true);
    }
}

void SequenceRunner::finishSequence(bool success) {
    m_isRunning = false;
    m_isWaitingForMyCommand = false;
    m_delayTimer.stop();
    qint64 elapsedMs = 0;
    if (m_totalSequenceTimer.isValid()) {
        elapsedMs = m_totalSequenceTimer.elapsed();
    }
    emit sequenceFinishedWithTime(success, elapsedMs);
    emit sequenceFinished(success);
    if (m_isInterval) {
        emit scheduleRestart(m_intervalValueMs); 
    }
}

void SequenceRunner::setIntervalToggle(bool toggle) {
    m_isInterval = toggle;
    emit logMessage(QString("[seq runner] interval: %1").arg(toggle ? "ON" : "OFF"), "#BDBDBD");
}

void SequenceRunner::setIntervalValue(int val) {
    m_intervalValueMs = val; 
    QString timeInfo = (m_intervalValueMs >= 1000) 
        ? QString::number(m_intervalValueMs / 1000.0, 'f', 1) + " s" 
        : QString::number(m_intervalValueMs) + " ms";
    emit logMessage(QString("[seq runner] interval set to: %1").arg(timeInfo), "#BDBDBD");
}
