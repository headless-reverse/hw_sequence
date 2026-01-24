#include "view_hardware_grabbed.h"
#include "commandexecutor.h"
#include "hw_keyboard_map.h"
#include <QKeyEvent>
#include <QCoreApplication>
#include <QVBoxLayout>
#include <QProcess>
#include <QFile>
#include <QDebug>
#include <QThread>
#include <QGroupBox>
#include <QDateTime>
#include <QScrollBar>

ViewHardwareGrabbed::ViewHardwareGrabbed(CommandExecutor *executor, QWidget *parent) 
    : QWidget(parent), m_executor(executor), m_recordModel(nullptr) {
    
    this->setAttribute(Qt::WA_AcceptTouchEvents);
    this->setFocusPolicy(Qt::StrongFocus); 
    m_logic = new HardwareGrabbed(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(6);
    m_statusLabel = new QLabel("SERVICE: DISCONNECTED");
    m_statusLabel->setStyleSheet("color: #f44336; font-size: 10px; font-weight: bold;");
    mainLayout->addWidget(m_statusLabel);
    QHBoxLayout *topRow = new QHBoxLayout();
    recordCheckBox = new QCheckBox("REC");
    recordCheckBox->setToolTip("Record hardware events to sequence");
    m_connectBtn = new QPushButton("🔗 Connect");
    m_connectBtn->setFixedHeight(26);
    
    topRow->addWidget(recordCheckBox);
    topRow->addWidget(m_connectBtn, 1);
    mainLayout->addLayout(topRow);

    QGridLayout *actionGrid = new QGridLayout();
    actionGrid->setSpacing(4);

    QPushButton *btnWifi = new QPushButton("🌐 ADB:1337");
    btnWifi->setToolTip("Connect ADB over Wireless");
    
    QPushButton *btnGrab = new QPushButton("🔒 KERNEL GRAB");
    btnGrab->setCheckable(true);
    btnGrab->setToolTip("EVIOCGRAB: Take exclusive control of input");
    btnGrab->setStyleSheet("QPushButton:checked { background-color: #d32f2f; color: white; }");

    actionGrid->addWidget(btnWifi, 0, 0);
    actionGrid->addWidget(btnGrab, 0, 1);
    mainLayout->addLayout(actionGrid);

    QFrame *line = new QFrame();
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    line->setStyleSheet("background-color: #444;");
    mainLayout->addWidget(line);

    m_hwKeyboardWidget = new HardwareKeyboard(m_logic, this);
    mainLayout->addWidget(m_hwKeyboardWidget);

    QHBoxLayout *adminRow = new QHBoxLayout();
    adminRow->setSpacing(4);
    QPushButton *btnStatus = new QPushButton("Status");
    btnStatus->setFixedHeight(22);
    btnStatus->setStyleSheet("font-size: 11px;");
    QPushButton *btnKill = new QPushButton("Stop Daemon");
    btnKill->setFixedHeight(22);
    btnKill->setStyleSheet("color: #d32f2f; font-size: 11px; font-weight: bold;");
    adminRow->addWidget(btnStatus);
    adminRow->addWidget(btnKill);
    mainLayout->addLayout(adminRow);

    mainLayout->addStretch();

    connect(m_hwKeyboardWidget, &HardwareKeyboard::keyTriggered, this, &ViewHardwareGrabbed::handleKeyAction);
    connect(m_connectBtn, &QPushButton::clicked, this, &ViewHardwareGrabbed::onConnectClicked);
    connect(btnKill, &QPushButton::clicked, this, &ViewHardwareGrabbed::onKillClicked);
    connect(btnStatus, &QPushButton::clicked, this, &ViewHardwareGrabbed::onCheckStatusClicked);
    connect(btnWifi, &QPushButton::clicked, this, [this]{ if(m_logic->isConnected()) m_logic->enableAdbWireless(); });
    connect(btnGrab, &QPushButton::toggled, this, [this](bool checked){ m_logic->setHardwareGrab(checked); });
    
    connect(m_logic, &HardwareGrabbed::remoteTouchEvent, this, [this](uint16_t axis, uint16_t val) {
        if (axis == 0x35) qDebug() << "PHONE X:" << val;
        else if (axis == 0x36) qDebug() << "PHONE Y:" << val;
    });
}

void ViewHardwareGrabbed::handleKeyAction(int linuxCode) {
    if (isRecordModeActive() && m_recordModel) {
        QMetaObject::invokeMethod(m_recordModel, "addKeyAction",
                                  Q_ARG(int, linuxCode),
                                  Q_ARG(QString, "KEY_PRESS"));
        qDebug() << "REC: Zapisano klawisz" << linuxCode;
    }
    emit hwKeyGenerated(linuxCode);}

void ViewHardwareGrabbed::keyPressEvent(QKeyEvent *event) {
    if (event->isAutoRepeat()) return;
    uint16_t code = LinuxKeys::fromQtKey(event->key());
    if (code != 0 && m_logic->isConnected()) {
        m_logic->sendKey(code, true); // press
        // opcjonalnie: emit hwKeyGenerated tylko dla nagrywania/wyzwalaczy:
        emit hwKeyGenerated(code);
        event->accept();
    } else {
        QWidget::keyPressEvent(event);}}

void ViewHardwareGrabbed::keyReleaseEvent(QKeyEvent *event) {
    if (event->isAutoRepeat()) return;
    uint16_t code = LinuxKeys::fromQtKey(event->key());
    if (code != 0 && m_logic->isConnected()) {
        m_logic->sendKey(code, false);
        event->accept();
    } else {
        QWidget::keyReleaseEvent(event);}}

void ViewHardwareGrabbed::setRecordModel(QObject *model) {m_recordModel = model;}

void ViewHardwareGrabbed::onConnectClicked() {
    QString target = m_executor->targetDevice();
    if (target.isEmpty()) {
        emit statusMessage("ioctl: no target device IP", true);
        return;}
    QString ip = target.split(":").at(0);
    if (m_logic->connectToDevice(ip, 22222)) {
        m_statusLabel->setText("service 22222: connected");
        m_statusLabel->setStyleSheet("color: #4CAF50; font-weight: bold;");
        emit statusMessage("ioctl: link established.", false);
    } else {
        m_statusLabel->setText("service 22222: FAILED");
        m_statusLabel->setStyleSheet("color: #f44336; font-weight: bold;");
        emit statusMessage("ioctl: connection failed.", true);
    }
}

void ViewHardwareGrabbed::onKillClicked() {
    QString target = m_executor->targetDevice();
    if (target.isEmpty()) return;
    emit statusMessage("ioctl: daemon stopped" + target, false);
    m_logic->disconnectDevice();
    QProcess::execute("adb", { "-s", target, "shell", "su -c pkill -9 hw_resident" });}

void ViewHardwareGrabbed::onCheckStatusClicked() {
    QString target = m_executor->targetDevice();
    if (target.isEmpty()) return;
    emit statusMessage("ioctl: checking process status...", false);
    QProcess *proc = new QProcess(this);
    connect(proc, &QProcess::readyReadStandardOutput, [this, proc](){
        emit statusMessage(proc->readAllStandardOutput().trimmed(), false);});
    proc->start("adb", { "-s", target, "shell", "ps -A | grep hw_resident" });}

bool ViewHardwareGrabbed::event(QEvent *event) {
    if (event->type() == QEvent::TouchBegin || event->type() == QEvent::TouchUpdate || event->type() == QEvent::TouchEnd) {
        QTouchEvent *touchEvent = static_cast<QTouchEvent *>(event);
        const QList<QEventPoint> &points = touchEvent->points();
        for (const QEventPoint &point : points) {
            uint8_t type;
            switch (point.state()) {
                case QEventPoint::Pressed:  type = EVENT_TYPE_TOUCH_DOWN; break;
                case QEventPoint::Updated:  type = EVENT_TYPE_TOUCH_MOVE; break;
                case QEventPoint::Released: type = EVENT_TYPE_TOUCH_UP; break;
                default: continue;}
            uint16_t x = static_cast<uint16_t>(point.position().x() * 4096 / this->width());
            uint16_t y = static_cast<uint16_t>(point.position().y() * 4096 / this->height());
            uint16_t slot = static_cast<uint16_t>(point.id());
            m_logic->sendTouch(type, x, y, slot);}
        return true;}
    return QWidget::event(event);}
