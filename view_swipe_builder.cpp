#include "view_swipe_builder.h"
#include "SwipeModel.h"
#include "swipecanvas.h"
#include "argsparser.h"
#include "video_worker.h"
#include "commandexecutor.h"
#include "ActionEditDialog.h"
#include "video_client.h"
#include "control_socket.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QFileDialog>
#include <QJsonDocument>
#include <QDebug>
#include <QLabel>
#include <QTimer>
#include <QListWidget>
#include <QSplitter>
#include <QMenu>
#include <QDialog>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QMessageBox>
#include <QRegularExpression>
#include <QDir>
#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QListWidgetItem>
#include <algorithm>
#include <QImage> 

ViewSwipeBuilder::ViewSwipeBuilder(CommandExecutor *executor, VideoClient *videoClient, QWidget *parent)
    : QWidget(parent), m_executor(executor), m_videoClient(videoClient) {
    if (!m_videoClient) {
        qFatal("[SwipeBuilder]: VideoClient is NULL");
    }
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    m_model = new SwipeModel(this);
    ControlSocket *socket = m_videoClient->controlSocket();
    m_canvas = new SwipeCanvas(m_model, socket, this);
    m_canvas->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	m_videoClient->setSwipeCanvas(m_canvas);
	
	if (m_videoClient->worker()) {
        connect(m_videoClient->worker(), &VideoWorker::remoteTouchFinished, 
                m_canvas, &SwipeCanvas::onRemoteTouchFinished);
    }
    QSplitter *rightSplitter = new QSplitter(Qt::Vertical);
    QWidget *controlsWidget = new QWidget();
    QVBoxLayout *controlsLayout = new QVBoxLayout(controlsWidget);
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    QHBoxLayout *listHeaderLayout = new QHBoxLayout();
    QLabel *recordedActionsLabel = new QLabel(tr("recorded actions")); 
    listHeaderLayout->addWidget(recordedActionsLabel);
    m_useRawCheckbox = new QCheckBox(tr("streaming"), this); 
    m_useRawCheckbox->setChecked(m_useRaw); 
    connect(m_useRawCheckbox, &QCheckBox::checkStateChanged, 
            this, &ViewSwipeBuilder::onRawToggleChanged); 
    listHeaderLayout->addWidget(m_useRawCheckbox);
    listHeaderLayout->addStretch(1); 
    controlsLayout->addLayout(listHeaderLayout);
    m_list = new QListWidget();
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    setAcceptDrops(true);
    controlsLayout->addWidget(m_list);
    QHBoxLayout *runLayout = new QHBoxLayout();
    m_runButton = new QPushButton("▶ Action");
    m_runButton->setStyleSheet("background-color: #4CAF50; color: white;");
    m_runSequenceButton = new QPushButton("▶ Sequence");
    m_runSequenceButton->setStyleSheet("background-color: #00BCD4; color: white;");
    runLayout->addWidget(m_runButton);
    runLayout->addWidget(m_runSequenceButton);
    controlsLayout->addLayout(runLayout);
    QHBoxLayout *editAddLayout = new QHBoxLayout();
    QPushButton *b_edit = new QPushButton("edit");
    QPushButton *b_add_cmd = new QPushButton("add");
    editAddLayout->addWidget(b_edit);
    editAddLayout->addWidget(b_add_cmd);
    controlsLayout->addLayout(editAddLayout);
    QHBoxLayout *deleteLayout = new QHBoxLayout();
    QPushButton *b_del = new QPushButton("delete");
    QPushButton *b_clear = new QPushButton("clear all");
    deleteLayout->addWidget(b_del);
    deleteLayout->addWidget(b_clear);
    controlsLayout->addLayout(deleteLayout);
    QHBoxLayout *fileLayout = new QHBoxLayout();
    QPushButton *b_export = new QPushButton("save");
    QPushButton *b_import = new QPushButton("load");
    fileLayout->addWidget(b_export);
    fileLayout->addWidget(b_import);
    controlsLayout->addLayout(fileLayout);
    rightSplitter->addWidget(controlsWidget);
    QSplitter *mainSplitter = new QSplitter(Qt::Horizontal);
    mainSplitter->addWidget(m_canvas);
    mainSplitter->addWidget(rightSplitter);
    mainSplitter->setStretchFactor(0, 3);
    mainSplitter->setStretchFactor(1, 1);
    mainLayout->addWidget(mainSplitter);
    m_resolutionProcess = new QProcess(this);
    connect(m_resolutionProcess,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &ViewSwipeBuilder::onResolutionReady);
    connect(m_model, &SwipeModel::modelChanged, this,&ViewSwipeBuilder::updateList);
    connect(b_export, &QPushButton::clicked, this,&ViewSwipeBuilder::saveJson);
    connect(b_import, &QPushButton::clicked, this,&ViewSwipeBuilder::loadJson);
    connect(b_clear, &QPushButton::clicked, this,&ViewSwipeBuilder::clearActions);
    connect(b_del, &QPushButton::clicked, this,&ViewSwipeBuilder::deleteSelected);
    connect(b_edit, &QPushButton::clicked, this,&ViewSwipeBuilder::editSelected);
    connect(b_add_cmd, &QPushButton::clicked, this,&ViewSwipeBuilder::addActionFromDialog);
    connect(m_runButton, &QPushButton::clicked, this,&ViewSwipeBuilder::runSelectedAction);
    connect(m_runSequenceButton, &QPushButton::clicked, this,&ViewSwipeBuilder::runFullSequence);
    connect(m_list, &QListWidget::customContextMenuRequested, this,
            [this](const QPoint &pos) {
                QMenu menu;
                menu.addAction("edit", this,&ViewSwipeBuilder::editSelected);
                menu.addAction("run", this,&ViewSwipeBuilder::runSelectedAction);
                menu.addSeparator();
                menu.addAction("move up", this,&ViewSwipeBuilder::moveSelectedUp);
                menu.addAction("move down", this,&ViewSwipeBuilder::moveSelectedDown);
                menu.addSeparator();
                menu.addAction("delete", this, &ViewSwipeBuilder::deleteSelected);
                menu.exec(m_list->mapToGlobal(pos));});
    connect(m_executor,QOverload<int, QProcess::ExitStatus>::of(&CommandExecutor::finished),this, &ViewSwipeBuilder::onAdbCommandFinished);
    QTimer::singleShot(500, this,&ViewSwipeBuilder::fetchDeviceResolution);}

ViewSwipeBuilder::~ViewSwipeBuilder() {
    if (m_videoClient) {m_videoClient->stopStream();}
    if (m_resolutionProcess) {m_resolutionProcess->kill();m_resolutionProcess->waitForFinished(500);}}

void ViewSwipeBuilder::setCanvasStatus(const QString &message, bool isError) {m_canvas->setStatus(message, isError);}

void ViewSwipeBuilder::setAdbPath(const QString &path) {if (!path.isEmpty()) m_adbPath = path;}

void ViewSwipeBuilder::startMonitoring() {
    if (!m_videoClient) return;
    if (m_useRaw) {
        QString serial = m_executor ? m_executor->targetDevice() : QString();
        if (serial.isEmpty()) {
            setCanvasStatus(tr("No target device for video stream."), true);
            return;}
        setCanvasStatus(tr("Starting video stream..."), false);
        m_videoClient->startStream(serial, 7373, 7373);
    } else {
        setCanvasStatus(tr("video disabled - nothing to monitor."), false);}}

void ViewSwipeBuilder::stopMonitoring() {
    if (m_videoClient) {
        m_videoClient->stopStream();
        setCanvasStatus(tr("Video stream stopped."), false);}}

void ViewSwipeBuilder::setRunSequenceButtonEnabled(bool enabled) {if (m_runSequenceButton) m_runSequenceButton->setEnabled(enabled);}

void ViewSwipeBuilder::onRawToggleChanged(int state) {
    bool enableVideo = (state == Qt::Checked);
    m_useRaw = enableVideo; 
    if (enableVideo) {
        if (m_videoClient) {
            setCanvasStatus(tr("Starting video stream..."), false);
            QString serial = m_executor ? m_executor->targetDevice() : QString();
            if (!serial.isEmpty()) {
                m_videoClient->startStream(serial, 7373, 7373);
            } else {
                setCanvasStatus(tr("[Critical error]"), true);}}
    } else {
        if (m_videoClient) {
            setCanvasStatus(tr("Podgląd zatrzymany."), false);
            m_videoClient->stopStream();
            m_canvas->update(); }}}

void ViewSwipeBuilder::updateList() {
    m_list->clear();
    int idx = 1;
    for (const auto &a : m_model->actions()) {
        QString txt;
        QString delayStr = QString("[D:%1ms]").arg(a.delayAfterMs);
        if (a.type == SwipeAction::Tap) {
            txt = QString("%1. Tap (%2,%3) %4")
                      .arg(idx).arg(a.x1).arg(a.y1).arg(delayStr);
        } else if (a.type == SwipeAction::Swipe) {
            QString durStr = QString("[T:%1ms]").arg(a.duration);
            txt = QString("%1. Swipe (%2,%3)->(%4,%5) %6 %7")
                      .arg(idx).arg(a.x1).arg(a.y1)
                      .arg(a.x2).arg(a.y2).arg(durStr).arg(delayStr);
        } else if (a.type == SwipeAction::Command) {
            QString cmd = a.command.simplified();
            if (cmd.length() > 40) cmd = cmd.left(40) + "...";
            txt = QString("%1. CMD [%2]: %3 %4")
                      .arg(idx).arg(a.runMode.toUpper())
                      .arg(cmd).arg(delayStr);
        } else if (a.type == SwipeAction::Key) {
            QString cmd = a.command.simplified();
            txt = QString("%1. KEY [%2]: %3 %4")
                      .arg(idx).arg(a.runMode.toUpper())
                      .arg(cmd).arg(delayStr);}
        m_list->addItem(txt);
        ++idx;}
    m_list->scrollToBottom();}

void ViewSwipeBuilder::runSelectedAction() {
    int row = m_list->currentRow();
    if (row < 0 || row >= m_model->actions().count()) {
        emit adbStatus("No action selected.", false);
        return;
    }
    const SwipeAction &a = m_model->actions().at(row);
    QString fullCommand = getAdbCommandForAction(a, true);
    QString runMode = (a.type == SwipeAction::Command) ? a.runMode.toLower() : QStringLiteral("root");
    if (fullCommand.isEmpty()) {
        emit adbStatus("Empty action or command.", true);
        return;
    }
    if (m_executor->isRunning()) {
        emit adbStatus("ADB is busy running another command...", true);
        return;
    }
    QStringList parsed = ArgsParser::parse(fullCommand);
    QString logCmd;
    QString displayMode = runMode.toUpper();
    if (runMode.compare("adb", Qt::CaseInsensitive) == 0) {
        QStringList args = parsed;
        logCmd = args.join(' ').simplified();
        if (logCmd.isEmpty()) logCmd = fullCommand;
        if (logCmd.length() > 80) logCmd = logCmd.left(80) + "...";
        emit adbStatus(QString("EXEC (%1): %2").arg(displayMode).arg(logCmd), false);
        m_executor->runAdbCommand(args);
    } else if (runMode.compare("root", Qt::CaseInsensitive) == 0) {
        QStringList args;
        args << "shell" << "su" << "-c" << fullCommand;
        logCmd = args.join(' ').simplified();
        if (logCmd.length() > 80) logCmd = logCmd.left(80) + "...";
        emit adbStatus(QString("EXEC (%1): %2").arg(displayMode).arg(logCmd), false);
        m_executor->runAdbCommand(args);
    } else if (runMode.compare("ioctl", Qt::CaseInsensitive) == 0 ||
               runMode.compare("hw", Qt::CaseInsensitive) == 0 ||
               runMode.compare("hw_direct", Qt::CaseInsensitive) == 0) {
        logCmd = fullCommand;
        if (logCmd.length() > 80) logCmd = logCmd.left(80) + "...";
        emit adbStatus(QString("EXEC (%1): %2").arg(displayMode).arg(logCmd), false);
        m_executor->executeSequenceCommand(fullCommand, "ioctl");
    } else {
        QStringList args;
        args << "shell";
        args.append(parsed);
        logCmd = args.join(' ').simplified();
        if (logCmd.isEmpty()) logCmd = fullCommand;
        if (logCmd.length() > 80) logCmd = logCmd.left(80) + "...";
        emit adbStatus(QString("EXEC (%1): %2").arg(displayMode).arg(logCmd), false);
        m_executor->runAdbCommand(args);
    }
    QListWidgetItem *item = m_list->item(row);
    if (item) item->setBackground(QBrush(QColor("#FFC107")));
}

void ViewSwipeBuilder::onAdbCommandFinished(int exitCode, QProcess::ExitStatus) {
    int finishedRow = -1;
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem *item = m_list->item(i);
        if (item->background().color() == QColor("#FFC107")) {
            finishedRow = i;
            item->setBackground(Qt::NoBrush);
            break;}}
    if (exitCode == 0) {
        if (finishedRow != -1) {
            QListWidgetItem *item = m_list->item(finishedRow);
            if (item) item->setBackground(QBrush(QColor("#4CAF50")));
            QTimer::singleShot(200, this, [item]() {
                if (item) item->setBackground(Qt::NoBrush);});}
        emit adbStatus("OK", false);
    } else {
        if (finishedRow != -1) {
            QListWidgetItem *item = m_list->item(finishedRow);
            if (item) item->setBackground(QBrush(QColor("#F44336")));
            QTimer::singleShot(500, this, [item]() {
                if (item) item->setBackground(Qt::NoBrush);});}
        emit adbStatus(QString("Error: %1").arg(exitCode), true);}}

void ViewSwipeBuilder::saveJson() {
    QString path = QFileDialog::getSaveFileName(this,
                                               tr("Save Sequence"),
                                               QString(),
                                               tr("JSON Files (*.json)"));
    if (path.isEmpty()) return;
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        QJsonDocument doc(m_model->toJsonSequence());
        f.write(doc.toJson(QJsonDocument::Indented));
        f.close();
        emit sequenceGenerated(path);
    } else {
        emit adbStatus("Failed to save file.", true);}}

bool ViewSwipeBuilder::loadSequenceFromJsonArray(const QJsonArray &array) {
	QString cmd;
    m_model->clear();
    bool ok = true;
    QRegularExpression tapRx("^input\\s+tap\\s+(\\d+)\\s+(\\d+)(?:\\s+\\d+)?$");
    QRegularExpression swipeRx("^input\\s+swipe\\s+(\\d+)\\s+(\\d+)\\s+(\\d+)\\s+(\\d+)(?:\\s+(\\d+))?$");
    for (const QJsonValue &val : array) {
        if (!val.isObject()) {
            ok = false;
            break;}
        QJsonObject obj = val.toObject();
        QString cmd = obj.value("command").toString().trimmed();
        int delayMs = obj.value("delayAfterMs").toInt(10);
        QString runMode = obj.value("runMode").toString("shell").toLower();
        QRegularExpressionMatch tapMatch = tapRx.match(cmd);
        QRegularExpressionMatch swipeMatch = swipeRx.match(cmd);
        if (tapMatch.hasMatch()) {
            int x = tapMatch.captured(1).toInt();
            int y = tapMatch.captured(2).toInt();
            m_model->addTap(x, y, delayMs);
            if (m_model->actions().last().runMode.toLower() != runMode) {
                 SwipeAction a = m_model->actions().last();
                 a.runMode = runMode;
                 m_model->editActionAt(m_model->actions().count() - 1, a);}
        } else if (swipeMatch.hasMatch()) {
            int x1 = swipeMatch.captured(1).toInt();
            int y1 = swipeMatch.captured(2).toInt();
            int x2 = swipeMatch.captured(3).toInt();
            int y2 = swipeMatch.captured(4).toInt();
            int dur = swipeMatch.captured(5).toInt();
            m_model->addSwipe(x1, y1, x2, y2, dur, delayMs);
            if (m_model->actions().last().runMode.toLower() != runMode) {
                 SwipeAction a = m_model->actions().last();
                 a.runMode = runMode;
                 m_model->editActionAt(m_model->actions().count() - 1, a);}
        } else if (cmd.startsWith("input keyevent")) {
            QString key = cmd.mid(15).trimmed();
            if (!key.isEmpty())
                m_model->addKey(key, delayMs);
            else
                m_model->addCommand(cmd, delayMs, runMode);
        } else {
            m_model->addCommand(cmd, delayMs, runMode);}}
    return ok;}

QString ViewSwipeBuilder::getAdbCommandForAction(const SwipeAction &action, bool raw) const {
    switch (action.type) {
    case SwipeAction::Tap:
        return QString("input tap %1 %2")
            .arg(action.x1)
            .arg(action.y1);
    case SwipeAction::Swipe:
        return QString("input swipe %1 %2 %3 %4 %5")
            .arg(action.x1)
            .arg(action.y1)
            .arg(action.x2)
            .arg(action.y2)
            .arg(action.duration);
    case SwipeAction::Command:
        return action.command;
    case SwipeAction::Key:
        return QString("input keyevent %1").arg(action.command);
    default:
        return QString();}}

void ViewSwipeBuilder::clearActions() {m_model->clear();}

void ViewSwipeBuilder::deleteSelected() {
    int row = m_list->currentRow();
    if (row >= 0) m_model->removeActionAt(row);}

void ViewSwipeBuilder::editSelected() {
    int row = m_list->currentRow();
    if (row < 0) return;
    SwipeAction cur = m_model->actionAt(row);
    ActionEditDialog dlg(cur, this); 
    if (dlg.exec() == QDialog::Accepted) {
        m_model->editActionAt(row, cur);}}

void ViewSwipeBuilder::moveSelectedUp() {
    int row = m_list->currentRow();
    if (row > 0) {
        m_model->moveAction(row, row - 1);
        m_list->setCurrentRow(row - 1);}}

void ViewSwipeBuilder::moveSelectedDown() {
    int row = m_list->currentRow();
    if (row >= 0 && row < m_model->actions().count() - 1) {
        m_model->moveAction(row, row + 1);
        m_list->setCurrentRow(row + 1);}}

void ViewSwipeBuilder::addActionFromDialog() {
    SwipeAction tmp(SwipeAction::Command, 0, 0, 0, 0, 0, 100, "", "shell");
    ActionEditDialog dlg(tmp, this);
    if (dlg.exec() == QDialog::Accepted) {
        m_model->addCommand(tmp.command, tmp.delayAfterMs, tmp.runMode);
        emit adbStatus(QString("add command: %1")
                              .arg(tmp.command.simplified()),
                      false);}}

void ViewSwipeBuilder::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls()) {
        for (const QUrl &url : event->mimeData()->urls()) {
            if (url.isLocalFile() &&
                QFileInfo(url.toLocalFile())
                    .suffix()
                    .compare("json", Qt::CaseInsensitive) == 0) {
                event->acceptProposedAction();
                return;}}}
    event->ignore();}

void ViewSwipeBuilder::dragMoveEvent(QDragMoveEvent *event) {
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
    else
        event->ignore();}

void ViewSwipeBuilder::dropEvent(QDropEvent *event) {
    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl &url : urls) {
        if (url.isLocalFile() &&
            QFileInfo(url.toLocalFile())
                .suffix()
                .compare("json", Qt::CaseInsensitive) == 0) {
            loadSequence(url.toLocalFile());
            break;}}
    event->acceptProposedAction();}

void ViewSwipeBuilder::fetchDeviceResolution() {
    if (!m_resolutionProcess) return;
    if (m_resolutionProcess->state() != QProcess::NotRunning) return;
    QString targetSerial = m_executor->targetDevice();
    if (targetSerial.isEmpty()) {
        qDebug() << "FetchResolution: No target device selected.";
        return;}
    QStringList args;
    args << "-s" << targetSerial << "shell" << "wm" << "size";
    QString program = m_adbPath.isEmpty() ? "adb" : m_adbPath;
    m_resolutionProcess->start(program, args);}

void ViewSwipeBuilder::onResolutionReady(int exitCode, QProcess::ExitStatus) {
    if (exitCode != 0) return;
    QString output = m_resolutionProcess->readAllStandardOutput();
    QRegularExpression re("(\\d+)x(\\d+)");
    QRegularExpressionMatch match = re.match(output);
    if (match.hasMatch()) {
        int w = match.captured(1).toInt();
        int h = match.captured(2).toInt();
        m_deviceWidth = w;
        m_deviceHeight = h;
        m_canvas->setDeviceResolution(w, h);
        qDebug() << "Wykryto rozdzielczość:" << w << "x" << h;
        if (m_useRaw && m_videoClient) {
            QString serial = m_executor->targetDevice();
            if (!serial.isEmpty()) {
                m_videoClient->startStream(serial, 7373, 7373, w, h);}}}}

void ViewSwipeBuilder::loadJson() {
    QString path = QFileDialog::getOpenFileName(this,
                                               tr("Load Sequence"),
                                               QString(),
                                               tr("JSON Files (*.json)"));
    if (!path.isEmpty())
        loadSequence(path);
}

void ViewSwipeBuilder::runFullSequence() {
    if (m_model->actions().isEmpty()) {
        emit adbStatus(tr("Sequence is empty. Add actions first."), true);
        return;}
    emit adbStatus(tr("Sending request to run full sequence…"), false);
    emit runFullSequenceRequested();}

void ViewSwipeBuilder::loadSequence(const QString &filePath) {
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        emit adbStatus(
            tr("Failed to open file: %1")
                .arg(QFileInfo(filePath).fileName()),
            true);
        return;}
    QByteArray data = f.readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isArray()) {
        emit adbStatus(tr("Invalid JSON format (expected array)."), true);
        return;}
    if (loadSequenceFromJsonArray(doc.array())) {
        emit adbStatus(
            tr("Sequence loaded from: %1")
                .arg(QFileInfo(filePath).fileName()),
            false);
    } else {
        emit adbStatus(
            tr("Failed to parse sequence from: %1")
                .arg(QFileInfo(filePath).fileName()),
            true);}}

void ViewSwipeBuilder::onSequenceCommandExecuting(const QString &cmd, int index, int total) {
    Q_UNUSED(cmd);
    Q_UNUSED(total);
    int listIndex = index - 1;
    if (listIndex >= 0 && listIndex < m_list->count()) {
        QListWidgetItem *item = m_list->item(listIndex);
        m_list->setCurrentItem(item); 
        m_list->scrollToItem(item);}}

void ViewSwipeBuilder::handleCanvasScreenshotReady(const QImage &image) {
    if (image.isNull()) {
        qWarning() << "Otrzymano pusty zrzut ekranu";
        return;}
    // mozna. zapisać obraz do pliku (opcjonalnie)
    qDebug() << "Zrzut ekranu gotowy, rozmiar:" << image.size();}
