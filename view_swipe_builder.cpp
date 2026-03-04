#include "view_swipe_builder.h"
#include "SwipeModel.h"
#include "swipecanvas.h"
#include "argsparser.h"
#include "video_worker.h"
#include "commandexecutor.h"
#include "ActionEditDialog.h"
#include "video_client.h"
#include "control_socket.h"
#include "hardware_grabbed.h"

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
    if (!m_videoClient) { qFatal("[SwipeBuilder]: VideoClient is NULL"); }
    HardwareGrabbed *hw = m_videoClient->hardwareGrab();
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0,0,0,0);
    m_model = new SwipeModel(this);
    ControlSocket *socket = m_videoClient->controlSocket();
    m_canvas = new SwipeCanvas(m_model, socket, this);
    m_canvas->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_videoClient->setSwipeCanvas(m_canvas);
    connect(m_videoClient,&VideoClient::remoteTouchEvent,m_canvas,&SwipeCanvas::onRemoteTouchEvent);
    connect(m_videoClient,&VideoClient::remoteTouchFinished,m_canvas,&SwipeCanvas::onRemoteTouchFinished);
    connect(m_videoClient,&VideoClient::remoteTouchEvent,this,&ViewSwipeBuilder::handleRemoteHardwareEvent);
    QSplitter *rightSplitter = new QSplitter(Qt::Vertical);
    QWidget *controlsWidget = new QWidget();
    QVBoxLayout *controlsLayout = new QVBoxLayout(controlsWidget);
    controlsLayout->setContentsMargins(0,0,0,0);
    QHBoxLayout *listHeaderLayout = new QHBoxLayout();
    QLabel *recordedActionsLabel = new QLabel(tr("recorded"));
    listHeaderLayout->addWidget(recordedActionsLabel);
    m_useRawCheckbox = new QCheckBox(tr("stream"),this);
    m_useRawCheckbox->setChecked(m_useRaw);
    connect(m_useRawCheckbox,&QCheckBox::checkStateChanged,this,&ViewSwipeBuilder::onRawToggleChanged);
    listHeaderLayout->addWidget(m_useRawCheckbox);
    m_useIoctlCheckbox = new QCheckBox(tr("ioctl"),this);
    m_useIoctlCheckbox->setChecked(m_defaultToIoctl);
    connect(m_useIoctlCheckbox,&QCheckBox::toggled,this,[this](bool checked){ m_defaultToIoctl=checked; setCanvasStatus(checked?tr("Mode: Hardware"):tr("Mode: Shell"),false); });
    listHeaderLayout->addWidget(m_useIoctlCheckbox);
    listHeaderLayout->addStretch(1);
    controlsLayout->addLayout(listHeaderLayout);
    m_list = new QListWidget();
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    setAcceptDrops(true);
    controlsLayout->addWidget(m_list);
    QHBoxLayout *runLayout = new QHBoxLayout();
    m_runButton = new QPushButton("▶ Action");
    m_runButton->setStyleSheet("background-color:#4CAF50;color:white;");
    m_runSequenceButton = new QPushButton("▶ Sequence");
	m_runSequenceButton->setStyleSheet("background-color:#00BCD4;color:white;");
	m_recordButton = new QPushButton("● REC", this);
	m_recordButton->setCheckable(true);
	m_recordButton->setFixedHeight(30);
	m_recordButton->setStyleSheet(
		"QPushButton { background-color: #607D8B; color: lightgray; font-weight: bold; text-align: left; padding-left: 10px; }"
		"QPushButton:checked { background-color: #f44336; color: #f44336; }"
);
	QCheckBox *modeToggle = new QCheckBox("IOCTL", m_recordButton);
	modeToggle->setStyleSheet("color: black; font-size: 9px; font-weight: normal;");
	modeToggle->setChecked(true);
	m_recordMode = "ioctl";
	QHBoxLayout *btnLayout = new QHBoxLayout(m_recordButton);
	btnLayout->setContentsMargins(0, 0, 8, 0);
	btnLayout->addStretch();
	btnLayout->addWidget(modeToggle);
	connect(modeToggle, &QCheckBox::toggled, this, [this](bool checked) {m_recordMode = checked ? "ioctl" : "shell";setCanvasStatus(checked ? "REC: hardware" : "REC: shell", false);});
	connect(m_recordButton, &QPushButton::toggled, this, [this](bool checked){
		m_recordButton->setStyleSheet(
			QString("QPushButton { background-color: %1; color: %2; font-weight: bold; text-align: left; padding-left: 10px; }")
				.arg(checked ? "#f44336" : "#607D8B")
				.arg(checked ? "red" : "lightgray")
		);
		onRecordToggled(checked);
});
runLayout->addWidget(m_recordButton);
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
    mainSplitter->setStretchFactor(0,3);
    mainSplitter->setStretchFactor(1,1);
    mainLayout->addWidget(mainSplitter);
    m_resolutionProcess = new QProcess(this);
    connect(m_resolutionProcess,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),this,&ViewSwipeBuilder::onResolutionReady);
    connect(m_model,&SwipeModel::modelChanged,this,&ViewSwipeBuilder::updateList);
    connect(b_export,&QPushButton::clicked,this,&ViewSwipeBuilder::saveJson);
    connect(b_import,&QPushButton::clicked,this,&ViewSwipeBuilder::loadJson);
    connect(b_clear,&QPushButton::clicked,this,&ViewSwipeBuilder::clearActions);
    connect(b_del,&QPushButton::clicked,this,&ViewSwipeBuilder::deleteSelected);
    connect(b_edit,&QPushButton::clicked,this,&ViewSwipeBuilder::editSelected);
    connect(b_add_cmd,&QPushButton::clicked,this,&ViewSwipeBuilder::addActionFromDialog);
    connect(m_runButton,&QPushButton::clicked,this,&ViewSwipeBuilder::runSelectedAction);
    connect(m_runSequenceButton,&QPushButton::clicked,this,&ViewSwipeBuilder::runFullSequence);
    connect(m_list,&QListWidget::customContextMenuRequested,this,[this](const QPoint &pos){
        QMenu menu;
        menu.addAction("edit",this,&ViewSwipeBuilder::editSelected);
        menu.addAction("run",this,&ViewSwipeBuilder::runSelectedAction);
        menu.addSeparator();
        menu.addAction("move up",this,&ViewSwipeBuilder::moveSelectedUp);
        menu.addAction("move down",this,&ViewSwipeBuilder::moveSelectedDown);
        menu.addSeparator();
        menu.addAction("delete",this,&ViewSwipeBuilder::deleteSelected);
        menu.exec(m_list->mapToGlobal(pos));
    });
    connect(m_executor,QOverload<int,QProcess::ExitStatus>::of(&CommandExecutor::finished),this,&ViewSwipeBuilder::onAdbCommandFinished);
    QTimer::singleShot(500,this,&ViewSwipeBuilder::fetchDeviceResolution);
    if (hw) {
    connect(hw, &HardwareGrabbed::recordWaitDetected, this, [this](int ms){ if (m_isRecording) m_model->addWait(ms); });
    connect(hw, &HardwareGrabbed::recordTapDetected, this, [this](int x, int y){ if (m_isRecording) {m_model->addTap(x, y, 10, m_recordMode);}});
    connect(hw, &HardwareGrabbed::recordSwipeDetected, this, [this](int x1, int y1, int x2, int y2, int dur){ if (m_isRecording) {m_model->addSwipe(x1, y1, x2, y2, dur, 10, m_recordMode);}});
}}

ViewSwipeBuilder::~ViewSwipeBuilder() {
    if (m_videoClient) { m_videoClient->stopStream(); }
    if (m_resolutionProcess) { m_resolutionProcess->kill(); m_resolutionProcess->waitForFinished(500); }}

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
                      .arg(cmd).arg(delayStr);
        } else if (a.type == SwipeAction::Wait) {
            txt = QString("%1. Wait: %2 ms").arg(idx).arg(a.delayAfterMs);
        }
        if (!txt.isEmpty()) {
            m_list->addItem(txt);
        }
        ++idx;
    }
    m_list->scrollToBottom();}

void ViewSwipeBuilder::runSelectedAction() {
    int row = m_list->currentRow();
    if (row < 0 || row >= m_model->actions().count())
        return;
    if (!m_executor || m_executor->isRunning())
        return;
    const SwipeAction &a = m_model->actions().at(row);
    QString finalMode = (m_defaultToIoctl || a.runMode.toLower() == "ioctl")
                        ? "ioctl"
                        : "shell";
    QString fullCommand = getAdbCommandForAction(a, finalMode == "ioctl");
    if (fullCommand.trimmed().isEmpty())
        return;
    m_executor->executeSequenceCommand(fullCommand, finalMode);
    if (QListWidgetItem *item = m_list->item(row))
        item->setBackground(QBrush(QColor("#FFC107")));
}

void ViewSwipeBuilder::onAdbCommandFinished(int exitCode, QProcess::ExitStatus) {
    int finishedRow = -1;
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem *item = m_list->item(i);
        if (item && item->background().color() == QColor("#FFC107")) {
            finishedRow = i;
            item->setBackground(Qt::NoBrush);
            break;
        }
    }
    if (finishedRow == -1)
        return;
    QListWidgetItem *item = m_list->item(finishedRow);
    if (!item)
        return;
    QColor resultColor = (exitCode == 0)
                         ? QColor("#4CAF50")
                         : QColor("#F44336");
    item->setBackground(QBrush(resultColor));
    QTimer::singleShot(exitCode == 0 ? 200 : 500, this, [item]() {
        if (item)
            item->setBackground(Qt::NoBrush);
    });
    emit adbStatus(exitCode == 0 ? "OK"
                                 : QString("Error: %1").arg(exitCode),
                   exitCode != 0);
}

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
    m_model->clear();
    bool ok = true;
    static const QRegularExpression tapRx("^input\\s+tap\\s+(\\d+)\\s+(\\d+)(?:\\s+\\d+)?$", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression swipeRx("^input\\s+swipe\\s+(\\d+)\\s+(\\d+)\\s+(\\d+)\\s+(\\d+)(?:\\s+(\\d+))?$", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression keyRx("^key\\s+(\\d+)$", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression hwRx("^HW_SOCKET_SEND\\s+(.+)$", QRegularExpression::CaseInsensitiveOption);
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
        QRegularExpressionMatch keyMatch = keyRx.match(cmd);
        QRegularExpressionMatch hwMatch = hwRx.match(cmd);
        if (tapMatch.hasMatch()) {
            int x = tapMatch.captured(1).toInt();
            int y = tapMatch.captured(2).toInt();
            m_model->addTap(x, y, delayMs);
        } else if (swipeMatch.hasMatch()) {
            int x1 = swipeMatch.captured(1).toInt();
            int y1 = swipeMatch.captured(2).toInt();
            int x2 = swipeMatch.captured(3).toInt();
            int y2 = swipeMatch.captured(4).toInt();
            int dur = swipeMatch.captured(5).toInt();
            m_model->addSwipe(x1, y1, x2, y2, dur, delayMs);
        } else if (cmd.startsWith("input keyevent", Qt::CaseInsensitive)) {
            QString key = cmd.mid(14).trimmed();
            if (!key.isEmpty())
                m_model->addKey(key, delayMs);
            else
                m_model->addCommand(cmd, delayMs, runMode);
        } else if (keyMatch.hasMatch()) {
            m_model->addKey(keyMatch.captured(1), delayMs);
        } else if (hwMatch.hasMatch()) {
            m_model->addKey(hwMatch.captured(1), delayMs);
        } else {
            m_model->addCommand(cmd, delayMs, runMode);}
        if (!m_model->actions().isEmpty() && m_model->actions().last().runMode.toLower() != runMode) {
            SwipeAction a = m_model->actions().last();
            a.runMode = runMode;
            m_model->editActionAt(m_model->actions().count() - 1, a);}}
    return ok;}

QString ViewSwipeBuilder::getAdbCommandForAction(const SwipeAction &action, bool useHardware) const {
    switch (action.type)
    {
        case SwipeAction::Tap:
            return useHardware
                ? QString("tap %1 %2").arg(action.x1).arg(action.y1)
                : QString("input tap %1 %2").arg(action.x1).arg(action.y1);
        case SwipeAction::Swipe:
            return useHardware
                ? QString("swipe %1 %2 %3 %4 %5")
                      .arg(action.x1).arg(action.y1)
                      .arg(action.x2).arg(action.y2)
                      .arg(action.duration)
                : QString("input swipe %1 %2 %3 %4 %5")
                      .arg(action.x1).arg(action.y1)
                      .arg(action.x2).arg(action.y2)
                      .arg(action.duration);
        case SwipeAction::Key:
        {
            QString cleanKey = action.command;
            cleanKey.remove(QRegularExpression("^input\\s+keyevent\\s+", QRegularExpression::CaseInsensitiveOption));
            cleanKey.remove(QRegularExpression("^key\\s+", QRegularExpression::CaseInsensitiveOption));
            cleanKey = cleanKey.trimmed();
            if (cleanKey.isEmpty())
                return QString();
            return useHardware
                ? QString("key %1").arg(cleanKey)
                : QString("input keyevent %1").arg(cleanKey);
        }
        case SwipeAction::Command:
            return action.command.trimmed();
        case SwipeAction::Wait:
            return QString();
        default:
            return QString();
    }
}

void ViewSwipeBuilder::clearActions() {m_model->clear();}

void ViewSwipeBuilder::deleteSelected() {
    int row = m_list->currentRow();
    if (row >= 0) m_model->removeActionAt(row);}

void ViewSwipeBuilder::editSelected() {
    int row = m_list->currentRow();
    if (row < 0) return;
    SwipeAction action = m_model->actionAt(row);
    ActionEditDialog dlg(action, this);
    if (dlg.exec() == QDialog::Accepted) {
        m_model->editActionAt(row, action);}}

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
        emit adbStatus(tr("Sequence is empty."), true);
        return;}
    QString currentMode = m_defaultToIoctl ? "hw" : "shell"; 
    emit adbStatus(tr("Running sequence (Mode: %1)").arg(m_defaultToIoctl ? "Hardware" : "Shell"), false);
    emit runFullSequenceRequested(currentMode); 
}

void ViewSwipeBuilder::loadSequence(const QString &filePath) {
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit adbStatus(
            tr("Failed to open file: %1")
                .arg(QFileInfo(filePath).fileName()),
            true);
        return;}
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        emit adbStatus(
            tr("Invalid JSON: %1")
                .arg(parseError.errorString()),
            true);
        return;
    }
    if (loadSequenceFromJsonArray(doc.array())) {
        emit adbStatus(
            tr("Sequence loaded from: %1")
                .arg(QFileInfo(filePath).fileName()),
            false);
    } else {
        emit adbStatus(
            tr("Failed to parse sequence from: %1")
                .arg(QFileInfo(filePath).fileName()),
            true);
    }
}

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
    qDebug() << "Zrzut ekranu gotowy, rozmiar:" << image.size();}

void ViewSwipeBuilder::handleRemoteHardwareEvent(uint16_t axis, uint32_t value) {
    if (!m_isRecording) return;
    static uint16_t startX = 0, startY = 0;
    static uint16_t lastX = 0, lastY = 0;
    static bool touching = false;
    static QElapsedTimer touchTimer;
    switch (axis) {
        case 0x35: lastX = static_cast<uint16_t>(value); break;
        case 0x36: lastY = static_cast<uint16_t>(value); break;
        case 0x39:
            if (value != 0xFFFFFFFF) {
                touching = true;
                startX = lastX;
                startY = lastY;
                touchTimer.start();
            } else {
                if (!touching) break;
                touching = false;
                int duration = touchTimer.elapsed();
                int delayMs = m_actionTimer.elapsed();
                m_actionTimer.restart();
                int dx = abs((int)lastX - (int)startX);
                int dy = abs((int)lastY - (int)startY);
                if (dx < 10 && dy < 10) {
                    if (duration > 500)
                        m_model->addLongPress(startX, startY, duration, delayMs, m_recordMode);
                    else
                        m_model->addTap(startX, startY, delayMs, m_recordMode);
                } else {
                    m_model->addSwipe(startX, startY, lastX, lastY, duration, delayMs, m_recordMode);
                }
            }
            break;
        default: break;
    }
}

void ViewSwipeBuilder::onRecordToggled(bool checked) {
    m_isRecording = checked;
    if (!m_videoClient)
        return;
    HardwareGrabbed *hw = m_videoClient->hardwareGrab();
    if (!hw)
        return;
    hw->setRecording(checked);
    if (checked) {
        m_recordButton->setText("■ STOP");
        m_recordButton->setStyleSheet("QPushButton { background-color: #f44336; color: white; font-weight: bold; text-align: left; padding-left: 10px; }");
        m_actionTimer.restart();
    } else {
        m_recordButton->setText("● REC");
        m_recordButton->setStyleSheet("QPushButton { background-color: #607D8B; color: white; font-weight: bold; text-align: left; padding-left: 10px; }");
    }
}
