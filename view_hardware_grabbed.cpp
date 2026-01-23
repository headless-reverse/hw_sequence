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
    mainLayout->setContentsMargins(5, 5, 5, 5);
    m_statusLabel = new QLabel("service 22222: disconnected");
    m_statusLabel->setStyleSheet("color: #f44336; font-weight:");
    mainLayout->addWidget(m_statusLabel);
    QHBoxLayout *topControls = new QHBoxLayout();
    recordCheckBox = new QCheckBox("REC", this);
    m_connectBtn = new QPushButton("🔗 connect service");
    topControls->addWidget(recordCheckBox);
    topControls->addWidget(m_connectBtn);
    mainLayout->addLayout(topControls);
    QGridLayout *systemGrid = new QGridLayout();
    QPushButton *btnWifi  = new QPushButton("🌐 adb connect:1337");
    QPushButton *btnGrab  = new QPushButton("🔒 KERNEL GRAB");
    btnGrab->setCheckable(true);
    systemGrid->addWidget(btnWifi, 0, 0, 1, 2);
    systemGrid->addWidget(btnGrab, 1, 0, 1, 2);
    mainLayout->addLayout(systemGrid);
    QGroupBox *keyBox = new QGroupBox("Hardware Keyboard");
    QVBoxLayout *keyBoxLayout = new QVBoxLayout(keyBox);
    m_hwKeyboardWidget = new HardwareKeyboard(m_logic, this);
    keyBoxLayout->addWidget(m_hwKeyboardWidget);
	mainLayout->addWidget(keyBox);
    QHBoxLayout *adminLayout = new QHBoxLayout();
    QPushButton *btnStatus = new QPushButton("status");
    QPushButton *btnKill   = new QPushButton("--daemon stop");
    btnKill->setStyleSheet("color: #d32f2f;");
    adminLayout->addWidget(btnStatus);
    adminLayout->addWidget(btnKill);
    mainLayout->addLayout(adminLayout);
    mainLayout->addStretch();
	connect(m_hwKeyboardWidget, &HardwareKeyboard::keyTriggered, this, &ViewHardwareGrabbed::handleKeyAction);
    connect(m_connectBtn, &QPushButton::clicked, this, &ViewHardwareGrabbed::onConnectClicked);
    connect(btnKill, &QPushButton::clicked, this, &ViewHardwareGrabbed::onKillClicked);
    connect(btnStatus, &QPushButton::clicked, this, &ViewHardwareGrabbed::onCheckStatusClicked);
    connect(btnWifi, &QPushButton::clicked, this, [this]{ if(m_logic->isConnected()) m_logic->enableAdbWireless(); });
    connect(btnGrab, &QPushButton::toggled, this, [this](bool checked){m_logic->setHardwareGrab(checked);});
    connect(m_logic, &HardwareGrabbed::remoteTouchEvent, 
        this, [this](uint16_t axis, uint16_t val) {
    if (axis == 0x35) { // X
        qDebug() << "PHONE TOUCH X:" << val;
    } else if (axis == 0x36) { // Y
        qDebug() << "PHONE TOUCH Y:" << val;
    }
});}

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
