#include "core_view.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QFrame>
#include <QGroupBox>
#include <QRegularExpression>
#include <QLabel>
#include <QProgressBar>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QPlainTextEdit>

CoreView::CoreView(Core *logic, QWidget *parent) 
    : QDockWidget("Hardware Core Monitor", parent), m_core(logic) 
{
    QWidget *mainWidget = new QWidget();
    QVBoxLayout *mainLayout = new QVBoxLayout(mainWidget);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);
    QHBoxLayout *connLayout = new QHBoxLayout();
    m_statusLed = new QLabel();
    m_statusLed->setFixedSize(8, 8);
    m_statusLed->setStyleSheet("background: gray; border-radius: 4px;");
    m_ipEdit = new QLineEdit("127.0.0.1");
    m_ipEdit->setFixedHeight(20);
    m_ipEdit->setStyleSheet("font-size: 10px; background: #222; color: #eee; border: 1px solid #444;");
    m_btnConnect = new QPushButton("ATTACH");
    m_btnConnect->setFixedSize(60, 20);
    m_btnConnect->setStyleSheet("QPushButton { font-size: 9px; font-weight: bold; background: #333; color: #00FF41; border: 1px solid #555; }"
                                "QPushButton:hover { background: #444; }");

    connLayout->addWidget(m_statusLed);
    connLayout->addWidget(m_ipEdit, 1);
    connLayout->addWidget(m_btnConnect);
    mainLayout->addLayout(connLayout);
    QFrame *teleFrame = new QFrame();
    teleFrame->setStyleSheet("QFrame { background: #0a0a0a; border: 1px solid #333; border-radius: 4px; } "
                             "QLabel { color: #aaa; font-size: 9px; text-transform: uppercase; }");
    
    QVBoxLayout *teleLayout = new QVBoxLayout(teleFrame);
    teleLayout->setContentsMargins(5, 5, 5, 5);
    teleLayout->setSpacing(3);
    QHBoxLayout *topTele = new QHBoxLayout();
    QLabel *tLabel = new QLabel("TEMP:");
    m_tempBar = new QProgressBar();
    m_tempBar->setRange(0, 100);
    m_tempBar->setFixedHeight(8);
    m_tempBar->setTextVisible(false);
    m_tempBar->setStyleSheet("QProgressBar { background: #222; border: none; border-radius: 2px; } "
                             "QProgressBar::chunk { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #2ecc71, stop:1 #e74c3c); }");
    m_freqValue = new QLabel("0 MHz");
    m_freqValue->setStyleSheet("color: #00FF41; font-family: monospace; font-weight: bold; font-size: 10px;");
    topTele->addWidget(tLabel);
    topTele->addWidget(m_tempBar, 1);
    topTele->addWidget(m_freqValue);
    teleLayout->addLayout(topTele);
    QHBoxLayout *pwrLayout = new QHBoxLayout();
    m_voltValue = new QLabel("0.00 V");
    m_curValue = new QLabel("0 mA");
    m_pwrValue = new QLabel("0.00 W");
    
    QString pwrStyle = "color: #00D1FF; font-family: monospace; font-size: 10px; font-weight: bold;";
    m_voltValue->setStyleSheet(pwrStyle);
    m_curValue->setStyleSheet(pwrStyle);
    m_pwrValue->setStyleSheet("color: #FFD700; font-family: monospace; font-size: 10px; font-weight: bold;");
    pwrLayout->addWidget(m_voltValue);
    pwrLayout->addStretch();
    pwrLayout->addWidget(m_curValue);
    pwrLayout->addStretch();
    pwrLayout->addWidget(m_pwrValue);
    teleLayout->addLayout(pwrLayout);
    mainLayout->addWidget(teleFrame);
    QGroupBox *voltBox = new QGroupBox("Voltage");
    voltBox->setStyleSheet("QGroupBox { font-weight: bold; color: #888; border: 1px solid #333; margin-top: 10px; padding-top: 5px; } "
                           "QGroupBox::title { subcontrol-origin: margin; left: 7px; padding: 0 3px; }");
    QVBoxLayout *voltLayout = new QVBoxLayout(voltBox);
    voltLayout->setContentsMargins(4, 8, 4, 4);
    QHBoxLayout *presetLayout = new QHBoxLayout();
    QString smallBtn = "QPushButton { background: #222; border: 1px solid #444; border-radius: 3px; padding: 2px; font-size: 10px; color: #ccc; }"
                       "QPushButton:hover { background: #333; border-color: #777; }";
    
    QPushButton *presetSafe = new QPushButton("0.90V");
    QPushButton *presetMid = new QPushButton("1.20V");
    QPushButton *presetBoost = new QPushButton("1.43V");
    presetSafe->setStyleSheet(smallBtn);
    presetMid->setStyleSheet(smallBtn);
    presetBoost->setStyleSheet(smallBtn + "QPushButton:hover { border-color: #e74c3c; color: #e74c3c; }");
    
    presetLayout->addWidget(presetSafe);
    presetLayout->addWidget(presetMid);
    presetLayout->addWidget(presetBoost);
    voltLayout->addLayout(presetLayout);

    QHBoxLayout *sliderLayout = new QHBoxLayout();
    m_preciseVoltSlider = new QSlider(Qt::Horizontal);
    m_preciseVoltSlider->setRange(700000, 1500000);
    m_preciseVoltSlider->setFixedHeight(15);
    m_preciseVoltLabel = new QLabel("0.90V");
    m_preciseVoltLabel->setStyleSheet("font-size: 10px; color: #00D1FF; font-family: monospace;");
    m_applyVoltBtn = new QPushButton("SET");
    m_applyVoltBtn->setFixedSize(40, 18);
    m_applyVoltBtn->setStyleSheet(smallBtn + "background: #002b00; color: #00FF41; border-color: #005500;");
    
    sliderLayout->addWidget(m_preciseVoltSlider, 1);
    sliderLayout->addWidget(m_preciseVoltLabel);
    sliderLayout->addWidget(m_applyVoltBtn);
    voltLayout->addLayout(sliderLayout);
    mainLayout->addWidget(voltBox);

    QGroupBox *expertBox = new QGroupBox("Signals");
    expertBox->setStyleSheet(voltBox->styleSheet());
    QHBoxLayout *expertLayout = new QHBoxLayout(expertBox);
    expertLayout->setContentsMargins(4, 8, 4, 4);
    
    QPushButton *btnTriggerKernel = new QPushButton("KERNEL HOOK");
    QPushButton *btnResetMem = new QPushButton("RESET MEMFD");
    btnTriggerKernel->setStyleSheet(smallBtn);
    btnResetMem->setStyleSheet(smallBtn);
    expertLayout->addWidget(btnTriggerKernel);
    expertLayout->addWidget(btnResetMem);
    mainLayout->addWidget(expertBox);

    QGroupBox *luaBox = new QGroupBox("Lua Engine");
    luaBox->setStyleSheet(voltBox->styleSheet());
    QVBoxLayout *luaLayout = new QVBoxLayout(luaBox);
    luaLayout->setContentsMargins(4, 8, 4, 4);
    
    QHBoxLayout *luaFileLayout = new QHBoxLayout();
    m_luaPathEdit = new QLineEdit();
    m_luaPathEdit->setPlaceholderText("Script path...");
    m_luaPathEdit->setFixedHeight(18);
    m_luaPathEdit->setStyleSheet("font-size: 9px; background: #111; border: 1px solid #333; color: #bbb;");
    m_luaBrowseBtn = new QPushButton("...");
    m_luaBrowseBtn->setFixedSize(20, 18);
    m_luaBrowseBtn->setStyleSheet(smallBtn);
    
    luaFileLayout->addWidget(m_luaPathEdit, 1);
    luaFileLayout->addWidget(m_luaBrowseBtn);
    luaLayout->addLayout(luaFileLayout);

    QHBoxLayout *luaCtrlLayout = new QHBoxLayout();
    m_luaArgsEdit = new QLineEdit();
    m_luaArgsEdit->setPlaceholderText("Args...");
    m_luaArgsEdit->setFixedHeight(18);
    m_luaArgsEdit->setStyleSheet(m_luaPathEdit->styleSheet());
    
    m_luaRunBtn = new QPushButton("RUN");
    m_luaStopBtn = new QPushButton("STOP");
    m_luaRunBtn->setFixedSize(40, 18);
    m_luaStopBtn->setFixedSize(40, 18);
    m_luaRunBtn->setStyleSheet(smallBtn + "background: #002244; color: #00D1FF;");
    m_luaStopBtn->setStyleSheet(smallBtn + "background: #331111; color: #FF4444;");
    
    luaCtrlLayout->addWidget(m_luaArgsEdit, 1);
    luaCtrlLayout->addWidget(m_luaRunBtn);
    luaCtrlLayout->addWidget(m_luaStopBtn);
    luaLayout->addLayout(luaCtrlLayout);

    m_luaLog = new QPlainTextEdit();
    m_luaLog->setReadOnly(true);
    m_luaLog->setMinimumHeight(60);
    m_luaLog->setStyleSheet("background: #050505; color: #00FF41; font-family: 'Consolas', monospace; font-size: 9px; border: none;");
    luaLayout->addWidget(m_luaLog);
    mainLayout->addWidget(luaBox);
    mainLayout->addStretch();
    setWidget(mainWidget);
    connect(m_core, &Core::telemetryUpdated, this, &CoreView::handleUpdate);
    connect(m_core, &Core::hardwareConnectionChanged, this, &CoreView::updateStatus);
    connect(m_btnConnect, &QPushButton::clicked, this, &CoreView::onAttachClicked);
    connect(m_preciseVoltSlider, &QSlider::valueChanged, this, [this](int val) {
        m_preciseVoltLabel->setText(QString("%1V").arg(val / 1000000.0, 0, 'f', 2));
    });

    connect(m_applyVoltBtn, &QPushButton::clicked, this, [this]() {
        m_core->setVoltage(0x01, static_cast<uint64_t>(m_preciseVoltSlider->value()));
    });

    connect(presetSafe, &QPushButton::clicked, this, [this]() { m_preciseVoltSlider->setValue(900000); });
    connect(presetMid, &QPushButton::clicked, this, [this]() { m_preciseVoltSlider->setValue(1200000); });
    connect(presetBoost, &QPushButton::clicked, this, [this]() { m_preciseVoltSlider->setValue(1430000); });
    connect(btnTriggerKernel, &QPushButton::clicked, this, [this]() { m_core->sendHardwareSignal(0x99, 0, 0); });
    connect(btnResetMem, &QPushButton::clicked, this, [this]() { m_core->sendHardwareSignal(0x9A, 0, 0); });
    connect(m_luaBrowseBtn, &QPushButton::clicked, this, &CoreView::browseLuaScript);
    connect(m_luaRunBtn, &QPushButton::clicked, this, &CoreView::runLuaScript);
    connect(m_luaStopBtn, &QPushButton::clicked, this, &CoreView::stopLuaScript);
}

void CoreView::updateStatus(bool attached) {
    if (attached) {
        m_statusLed->setStyleSheet("background: #00FF41; border-radius: 5px; border: 1px solid white;");
        m_btnConnect->setText("DETACH");
    } else {
        m_statusLed->setStyleSheet("background: gray; border-radius: 5px;");
        m_btnConnect->setText("ATTACH");
    }
}

void CoreView::handleUpdate(uint32_t temp, uint64_t freq) {
    double realTemp = temp / 1000.0;
    m_tempBar->setValue(static_cast<int>(realTemp));
    m_freqValue->setText(QString("%1 MHz").arg(freq / 1000));
    float voltage = m_core->getCurrentVoltage();
    float current = m_core->getCurrentAmperage();
    float power = voltage * current;
    m_voltValue->setText(QString("%1 V").arg(voltage, 0, 'f', 2));
    m_curValue->setText(QString("%1 mA").arg(current * 1000, 0, 'f', 0));
    m_pwrValue->setText(QString("%1 W").arg(power, 0, 'f', 2));
    if (realTemp > 75.0) {
        m_freqValue->setStyleSheet("color:#FF3B30;font-weight:900;font-size:10px;");
    } else {
        m_freqValue->setStyleSheet("color:#00FF41;font-weight:bold;font-size:10px;");
    }
}

void CoreView::browseLuaScript() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Select Lua Script"), QString(), tr("Lua scripts (*.lua);;All files (*)"));
    if (!path.isEmpty()) m_luaPathEdit->setText(path);
}

void CoreView::runLuaScript() {
    const QString scriptPath = m_luaPathEdit->text().trimmed();
    if (scriptPath.isEmpty()) return;
    QStringList args = m_luaArgsEdit->text().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    m_core->runLuaScript(scriptPath, args);
}

void CoreView::stopLuaScript() {m_core->stopLuaScript();}

void CoreView::appendLuaLog(const QString &line, bool isError) {
    if (!m_luaLog) return;
    m_luaLog->appendPlainText(QString("%1 %2").arg(isError ? "[ERR]" : "[OK] ").arg(line));
}

void CoreView::onAttachClicked() {
    if (m_core->isHardwareConnected()) {
        m_core->disconnectHardware();
    } else {
        m_core->connectToHardware(m_ipEdit->text().trimmed(), 22222);
    }
}

void CoreView::onBoostClicked() {m_core->setVoltage(0x01, 1430000);}

void CoreView::onSafeModeClicked() {m_core->setVoltage(0x01, 900000);}
