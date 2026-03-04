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
#include <QRegularExpression>
#include <QRegularExpressionMatch>

ViewHardwareGrabbed::ViewHardwareGrabbed(CommandExecutor *executor, QWidget *parent) 
    : QWidget(parent), m_executor(executor), m_recordModel(nullptr) {
    
    this->setAttribute(Qt::WA_AcceptTouchEvents);
    this->setFocusPolicy(Qt::StrongFocus); 
    m_logic = new HardwareGrabbed(this);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    QHBoxLayout *statusRow = new QHBoxLayout();
    statusRow->setContentsMargins(2, 0, 2, 0);

    recordCheckBox = new QCheckBox("REC");
    recordCheckBox->setStyleSheet("font-size: 13px; font-weight: bold; color: #ff5722;");
    recordCheckBox->setToolTip("Record hardware events to sequence");

    m_statusLabel = new QLabel("22222: OFFLINE");
    m_statusLabel->setStyleSheet("color: #ff0000; font-size: 13px; font-family: 'Consolas'; font-weight:");

    statusRow->addWidget(recordCheckBox);
    statusRow->addSpacing(8); 
    statusRow->addWidget(m_statusLabel);
    statusRow->addStretch();
    mainLayout->addLayout(statusRow);

    QHBoxLayout *actionRow = new QHBoxLayout();
    actionRow->setSpacing(2);

    QPushButton *btnWifi = new QPushButton("🌍 ADB:1337");
    m_connectBtn = new QPushButton("📡 connect...");
    QPushButton *btnGrabTouch = new QPushButton("🔒 Grab Touch");
    QPushButton *btnGrabKeys = new QPushButton("🔒 Grab Keys");

    btnGrabTouch->setCheckable(true);
    btnGrabKeys->setCheckable(true);

    QString btnStyle = "QPushButton { font-size: 14px; height: 22px; padding: 2px; }"
                       "QPushButton:checked { background-color: #d32f2f; color: white; font-weight: bold; }";
    
    btnWifi->setStyleSheet(btnStyle);
    m_connectBtn->setStyleSheet(btnStyle);
    btnGrabTouch->setStyleSheet(btnStyle);
    btnGrabKeys->setStyleSheet(btnStyle);
    actionRow->addWidget(btnWifi, 1);
    actionRow->addWidget(m_connectBtn, 1);
    actionRow->addWidget(btnGrabTouch, 1);
    actionRow->addWidget(btnGrabKeys, 1);
    mainLayout->addLayout(actionRow);

    QFrame *line = new QFrame();
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet("background-color: #444;");
    mainLayout->addWidget(line);

    m_hwKeyboardWidget = new HardwareKeyboard(m_logic, this);
    mainLayout->addWidget(m_hwKeyboardWidget);
    
    QHBoxLayout *adminRow = new QHBoxLayout();
    adminRow->setSpacing(4);
    QPushButton *btnStatus = new QPushButton("refresh status");
    btnStatus->setFixedHeight(20);
    btnStatus->setStyleSheet("font-size: 10px;");
    
    QPushButton *btnKill = new QPushButton("pkill --daemon");
    btnKill->setFixedHeight(20);
    btnKill->setStyleSheet("color: #d32f2f; font-size: 10px; font-weight: bold;");
    
    adminRow->addWidget(btnStatus);
    adminRow->addWidget(btnKill);
    mainLayout->addLayout(adminRow);
    mainLayout->addStretch();

    connect(m_hwKeyboardWidget, &HardwareKeyboard::keyTriggered, this, &ViewHardwareGrabbed::handleKeyAction);
    connect(m_connectBtn, &QPushButton::clicked, this, &ViewHardwareGrabbed::onConnectClicked);
    connect(btnKill, &QPushButton::clicked, this, &ViewHardwareGrabbed::onKillClicked);
    connect(btnStatus, &QPushButton::clicked, this, &ViewHardwareGrabbed::onCheckStatusClicked);
    connect(btnWifi, &QPushButton::clicked, this, [this]{ if(m_logic->isConnected()) m_logic->enableAdbWireless(); });
    connect(btnGrabTouch, &QPushButton::toggled, this, [this](bool checked){ m_logic->setHardwareGrab(checked, 1); });
    connect(btnGrabKeys, &QPushButton::toggled, this, [this](bool checked){ m_logic->setHardwareGrab(checked, 2); });
    
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
        m_logic->sendKey(code, true);
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
        emit statusMessage("ioctl: no target device", true);
        return;
    }
    QRegularExpression ipRx(R"(^(\d{1,3}\.){3}\d{1,3})");
    QRegularExpressionMatch ipMatch = ipRx.match(target);
    if (ipMatch.hasMatch()) {
        QString ip = ipMatch.captured(0);
        if (m_logic->connectToDevice(ip, 22222)) {
            QProcess *pidProc = new QProcess(this);
            connect(pidProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), 
                    this, [this, pidProc](int exitCode) {
                if (exitCode == 0) {
                    QString pid = QString(pidProc->readAllStandardOutput()).trimmed();
                    m_statusLabel->setText(QString("22222 - PID: %1").arg(pid.isEmpty() ? "???" : pid));
                    m_statusLabel->setStyleSheet("color: #4CAF50; font-family: 'Consolas'; font-weight: bold;");
                }
                pidProc->deleteLater();
            });
            pidProc->start(m_executor->adbPath(), {"-s", target, "shell", "pidof hw_resident"});
            
            emit statusMessage("ioctl: link established (direct IP).", false);
        } else {
            m_statusLabel->setText("22222: FAILED");
            m_statusLabel->setStyleSheet("color: #f44336; font-weight: bold;");
            emit statusMessage("ioctl: connection failed (direct IP).", true);
        }
        return;
    }
    QString serial = target;
    QString adbProg = m_executor ? m_executor->adbPath() : QStringLiteral("adb");
    {
        QProcess adb;
        QStringList forwardArgs;
        forwardArgs << "-s" << serial << "forward" << "tcp:22222" << "tcp:22222";
        adb.start(adbProg, forwardArgs);
        if (!adb.waitForFinished(3000) || adb.exitCode() != 0) {
            QString err = adb.readAllStandardError();
            emit statusMessage(QString("adb forward failed: %1").arg(err.trimmed()), true);
            return;
        }
    }
    {
        QStringList startArgs;
        startArgs << "-s" << serial << "shell"
                  << "su -c 'hw_resident > /dev/null 2>&1 &'";
        QProcess::startDetached(adbProg, startArgs);
    }
    QTimer::singleShot(450, this, [this, serial, adbProg]() {
        if (m_logic->connectToDevice(QStringLiteral("127.0.0.1"), 22222)) {
            m_forwardedSerial = serial;
            QProcess *pidProc = new QProcess(this);
            connect(pidProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), 
                    this, [this, pidProc](int exitCode) {
                if (exitCode == 0) {
                    QString pid = QString(pidProc->readAllStandardOutput()).trimmed();
                    m_statusLabel->setText(QString("22222 - PID: %1").arg(pid.isEmpty() ? "???" : pid));
                    m_statusLabel->setStyleSheet("color: #4CAF50; font-family: 'Consolas'; font-weight:");
                }
                pidProc->deleteLater();
            });
            pidProc->start(adbProg, {"-s", serial, "shell", "pidof hw_resident"});
            emit statusMessage("ioctl: link established (adb forward).", false);
        } else {
            m_statusLabel->setText("22222: ERROR");
            m_statusLabel->setStyleSheet("color: #f44336; font-weight: bold;");
            emit statusMessage("ioctl: connection failed (adb-forward).", true);
        }
    });
}

void ViewHardwareGrabbed::onKillClicked() {
    QString target = m_executor->targetDevice();
    m_logic->disconnectDevice();
    if (!m_forwardedSerial.isEmpty()) {
        QString adbProg = m_executor ? m_executor->adbPath() : QStringLiteral("adb");
        QProcess::execute(adbProg, QStringList() << "-s" << m_forwardedSerial << "forward" << "--remove" << "tcp:22222");
        m_forwardedSerial.clear();
        emit statusMessage("adb forward removed", false);}
    if (!target.isEmpty()) {
        QString adbProg = m_executor ? m_executor->adbPath() : QStringLiteral("adb");
        QProcess::execute(adbProg, QStringList() << "-s" << target << "shell" << "su -c pkill -9 hw_resident");}
    m_statusLabel->setText("service: disconnected");
    m_statusLabel->setStyleSheet("color: #f44336; font-weight:");
    emit statusMessage("ioctl: disconnected", false);}

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
