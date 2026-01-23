#include "view_sequencerunner.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QTextEdit>
#include <QDialog>
#include <QDateTime>
#include <QTimer>
#include <QFileDialog>

#include "sequencerunner.h"
#include "commandexecutor.h"

ViewSequencerunner::ViewSequencerunner(SequenceRunner *runner, CommandExecutor *executor, QWidget *parent)
    : QWidget(parent), m_runner(runner), m_executor(executor) {

    auto mainLayout = new QVBoxLayout(this);
//	mainLayout->setContentsMargins(5,5,5,5);
    QHBoxLayout *topLayout = new QHBoxLayout();
    m_loadBtn = new QPushButton(tr("Load Sequence..."));
    m_previewBtn = new QPushButton(tr("Preview"));
    m_runBtn = new QPushButton(tr("Run Sequence"));
    m_stopBtn = new QPushButton(tr("Stop Sequence"));
    m_runBtn->setStyleSheet("background-color: #4CAF50; color: black;");
    m_previewBtn->setStyleSheet("background-color: #2196F3; color: black;");
    m_stopBtn->setStyleSheet("background-color: #FF0000; color: black;");
    topLayout->addWidget(m_loadBtn);
    topLayout->addWidget(m_previewBtn);
    topLayout->addWidget(m_runBtn);
    topLayout->addWidget(m_stopBtn);
    mainLayout->addLayout(topLayout);
    QHBoxLayout *statusRowLayout = new QHBoxLayout();
    m_intervalToggle = new QCheckBox(tr("Interval:"));
    m_intervalSpin = new QSpinBox();
    m_intervalSpin->setRange(1, 86400000);
    m_intervalSpin->setValue(60000);
    m_intervalSpin->setSuffix(" ms");
    m_intervalLabel = new QLabel(tr("Wait: 0ms"));
    m_intervalLabel->setStyleSheet("color: black;");
    m_statusLabel = new QLabel(tr("Status: Ready"), this);
    m_statusLabel->setStyleSheet("font-weight: bold; color: #444;");
    m_measureTimeCheck = new QCheckBox("stoper", this);
    m_measureTimeCheck->setToolTip("czas trwania sekwencji.");
    statusRowLayout->addWidget(m_intervalToggle);
    statusRowLayout->addWidget(m_intervalSpin);
    statusRowLayout->addWidget(m_intervalLabel);
    statusRowLayout->addSpacing(20);           // Odstęp dla czytelności
    statusRowLayout->addWidget(new QLabel(tr("Last Result:"))); 
    statusRowLayout->addWidget(m_statusLabel);
    statusRowLayout->addStretch(1);
    statusRowLayout->addWidget(m_measureTimeCheck);
    mainLayout->addLayout(statusRowLayout);
    mainLayout->addStretch(1);
    connect(m_loadBtn, &QPushButton::clicked, this, &ViewSequencerunner::onLoadClicked);
    connect(m_previewBtn, &QPushButton::clicked, this, &ViewSequencerunner::onPreviewClicked);
    connect(m_runBtn, &QPushButton::clicked, this, &ViewSequencerunner::onRunClicked);
    connect(m_stopBtn, &QPushButton::clicked, this, &ViewSequencerunner::onStopClicked);
    connect(m_intervalToggle, &QCheckBox::toggled, this, &ViewSequencerunner::onIntervalToggled);
    connect(m_intervalSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ViewSequencerunner::onIntervalValueChanged);
    m_countdownTimer = new QTimer(this);
    m_countdownTimer->setInterval(100);
    connect(m_countdownTimer, &QTimer::timeout, this, &ViewSequencerunner::onCountdownTick);
    if (m_runner) {
        connect(m_measureTimeCheck, &QCheckBox::toggled, m_runner, &SequenceRunner::setDiagnosticMode);
        connect(m_runner, &SequenceRunner::sequenceStarted, this, &ViewSequencerunner::handleSequenceStarted);
        connect(m_runner, &SequenceRunner::sequenceFinishedWithTime, this, &ViewSequencerunner::handleSequenceFinishedWithTime);
        connect(m_runner, &SequenceRunner::scheduleRestart, this, &ViewSequencerunner::handleScheduleRestart);
        QTimer::singleShot(0, this, &ViewSequencerunner::updateFromRunner);
    }
    m_runBtn->setEnabled(true);
    m_stopBtn->setEnabled(false);
}

void ViewSequencerunner::loadSequenceFromFile(const QString &path) {
    if (m_runner->appendSequence(path)) {
        // ok
    }
}

void ViewSequencerunner::onLoadClicked() {
    QStringList fns = QFileDialog::getOpenFileNames(this, tr("Load Sequence JSON(s)"),
                                                    QString(), tr("JSON files (*.json);;All files (*)"));
    if (fns.isEmpty()) return;
    if (!m_runner) return;
    m_runner->clearSequence();
    int successCount = 0;
    for (const QString &fn : fns) {
        if (m_runner->appendSequence(fn)) successCount++;
    }
    if (successCount > 0) {
        QMessageBox::information(this, tr("Loaded"), tr("Sequence queue loaded from %1 files.").arg(successCount));
    } else {
        QMessageBox::warning(this, tr("Error"), tr("Error loading sequence files."));
    }
    updateFromRunner();
}

void ViewSequencerunner::onPreviewClicked() {
    if (!m_runner) return;
    QStringList commands = m_runner->getCommandsAsText();
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Current Workflow Queue"));
    dialog.resize(600, 400);
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QTextEdit *textEdit = new QTextEdit(&dialog);
    textEdit->setReadOnly(true);
    textEdit->setFontFamily("Monospace");
    QString content = QString("--- TOTAL COMMANDS: %1 ---\n\n").arg(commands.count());
    if (commands.isEmpty()) {
        content += "No commands loaded in queue.";
    } else {
        for (int i = 0; i < commands.count(); ++i) {
            content += QString("[%1] %2\n").arg(i + 1, 2, 10, QChar('0')).arg(commands.at(i));
        }
    }
    textEdit->setText(content);
    layout->addWidget(textEdit);
    QHBoxLayout *btnLayout = new QHBoxLayout();
    QPushButton *clearBtn = new QPushButton("Clear Queue");
    clearBtn->setStyleSheet("background-color: #F44336; color: white;");
    connect(clearBtn, &QPushButton::clicked, [this, &dialog, textEdit]() {
        if (m_runner) m_runner->clearSequence();
        textEdit->setText("Queue cleared.");
        updateFromRunner();
    });
    btnLayout->addWidget(clearBtn);
    QPushButton *closeBtn = new QPushButton("Close");
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    btnLayout->addWidget(closeBtn);
    layout->addLayout(btnLayout);
    dialog.exec();
}

void ViewSequencerunner::onRunClicked() {
    if (m_runner->startSequence()) {
        m_runBtn->setEnabled(false);
        m_stopBtn->setEnabled(true);
        m_statusLabel->setText("running...");
    }
}

void ViewSequencerunner::onStopClicked() {
    m_runner->stopSequence();
}

void ViewSequencerunner::onIntervalToggled(bool checked) {
    if (!m_runner) return;
    m_runner->setIntervalToggle(checked);
}

void ViewSequencerunner::onIntervalValueChanged(int val) {
    if (!m_runner) return;
    m_runner->setIntervalValue(val);
    QString timeStr = (val >= 1000) ? QString::number(val/1000.0, 'f', 1) + "s" : QString::number(val) + "ms";
    m_intervalLabel->setText(QString("Interval: %1").arg(timeStr));
}

void ViewSequencerunner::handleSequenceStarted() {
    m_runBtn->setEnabled(false);
    m_stopBtn->setEnabled(true);
    m_intervalLabel->setText("WYKONYWANIE...");
    m_intervalLabel->setStyleSheet("color: #4CAF50; font-weight:");
    if (m_countdownTimer->isActive()) {
        m_countdownTimer->stop();
        m_remainingMs = 0;
    }
}

void ViewSequencerunner::handleSequenceFinished(bool success) {
    m_runBtn->setEnabled(true);
    m_stopBtn->setEnabled(false);
}

void ViewSequencerunner::handleSequenceFinishedWithTime(bool success, qint64 elapsedMs) {
	if (!m_statusLabel) {
		qCritical() << "[CRASH_PREVENTION] m_statusLabel is NULL! Check initialization.";
		return;
	}
    QString res = success ? "OK" : "ERR";
    m_statusLabel->setText(QString("%1 (%2 ms)").arg(res).arg(elapsedMs));
}

void ViewSequencerunner::updateFromRunner() {
    if (!m_runner) return;
    m_intervalToggle->setChecked(false);
    m_intervalSpin->setValue(60);
    m_intervalLabel->setText("(0ms)");
}

void ViewSequencerunner::handleScheduleRestart(int ms) {
    if (ms <= 0) {
        m_intervalLabel->setText("next run: 0ms");
        return;
    }
    m_remainingMs = ms;
    QString timeStr = (m_remainingMs >= 1000) 
        ? QString::number(m_remainingMs / 1000.0, 'f', 1) + "s" 
        : QString::number(m_remainingMs) + "ms";
    m_intervalLabel->setText(QString("Next run in: %1").arg(timeStr));
    if (!m_countdownTimer->isActive()) {
        m_countdownTimer->start(100); 
    }
}

void ViewSequencerunner::onCountdownTick() {
    if (m_remainingMs <= 0) {
        m_countdownTimer->stop();
        m_intervalLabel->setText("running...");
        return;
    }
    m_remainingMs -= 100;
    if (m_remainingMs % 500 == 0 || m_remainingMs < 1000) {
        QString timeStr = (m_remainingMs >= 1000) 
            ? QString::number(m_remainingMs / 1000.0, 'f', 1) + "s" 
            : QString::number(m_remainingMs) + "ms";
        m_intervalLabel->setText(QString("Next run in: %1").arg(timeStr));
    }
}
