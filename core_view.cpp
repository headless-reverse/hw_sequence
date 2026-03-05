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
    mainLayout->setContentsMargins(6, 6, 6, 6);
    mainLayout->setSpacing(6);

    QHBoxLayout *connLayout = new QHBoxLayout();
    m_statusLed = new QLabel();
    m_statusLed->setFixedSize(10, 10);
    m_statusLed->setStyleSheet("background: gray; border-radius: 5px;");
    
    m_ipEdit = new QLineEdit("127.0.0.1");
    m_btnConnect = new QPushButton("ATTACH");
    m_btnConnect->setMaximumWidth(90);

    connLayout->addWidget(m_statusLed);
    connLayout->addWidget(m_ipEdit, 1);
    connLayout->addWidget(m_btnConnect);
    mainLayout->addLayout(connLayout);

    QFrame *teleFrame = new QFrame();
    teleFrame->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    teleFrame->setStyleSheet("background: #111; border-radius: 4px; padding: 4px;");
    
    QVBoxLayout *teleLayout = new QVBoxLayout(teleFrame);
    teleLayout->setContentsMargins(6, 6, 6, 6);
    teleLayout->setSpacing(4);
    
    teleLayout->addWidget(new QLabel("Core Temperature"));
    m_tempBar = new QProgressBar();
    m_tempBar->setRange(0, 100);
    m_tempBar->setFormat("%v°C");
    m_tempBar->setStyleSheet(
        "QProgressBar { border: 1px solid grey; border-radius: 2px; text-align: center; color: white; }"
        "QProgressBar::chunk { background-color: qlineargradient(x1:0, y1:0, x2:1, y2:0, "
        "stop:0 green, stop:0.6 yellow, stop:1 red); }"
    );
    teleLayout->addWidget(m_tempBar);

    QHBoxLayout *freqLayout = new QHBoxLayout();
    freqLayout->addWidget(new QLabel("Freq:"));
    m_freqValue = new QLabel("0 MHz");
    m_freqValue->setStyleSheet("color: #00FF41; font-family: monospace; font-weight: bold;");
    freqLayout->addStretch();
    freqLayout->addWidget(m_freqValue);
    teleLayout->addLayout(freqLayout);

    mainLayout->addWidget(teleFrame);

    QGroupBox *voltBox = new QGroupBox("Voltage Control");
    QVBoxLayout *voltLayout = new QVBoxLayout(voltBox);
    
    QHBoxLayout *presetLayout = new QHBoxLayout();
    QPushButton *presetSafe = new QPushButton("0.90V");
    QPushButton *presetMid = new QPushButton("1.20V");
    QPushButton *presetBoost = new QPushButton("1.43V");
    presetLayout->addWidget(presetSafe);
    presetLayout->addWidget(presetMid);
    presetLayout->addWidget(presetBoost);
    voltLayout->addLayout(presetLayout);

    QHBoxLayout *sliderLayout = new QHBoxLayout();
    m_preciseVoltSlider = new QSlider(Qt::Horizontal);
    m_preciseVoltSlider->setRange(700000, 1500000);
    m_preciseVoltSlider->setValue(900000);
    m_preciseVoltLabel = new QLabel("0.90V");
    m_applyVoltBtn = new QPushButton("APPLY");
    
    sliderLayout->addWidget(m_preciseVoltSlider, 1);
    sliderLayout->addWidget(m_preciseVoltLabel);
    sliderLayout->addWidget(m_applyVoltBtn);
    voltLayout->addLayout(sliderLayout);
    mainLayout->addWidget(voltBox);

    QGroupBox *expertBox = new QGroupBox("Expert Signals");
    QHBoxLayout *expertLayout = new QHBoxLayout(expertBox);
    QPushButton *btnTriggerKernel = new QPushButton("Kernel Hook");
    QPushButton *btnResetMem = new QPushButton("Clear Memfd");
    expertLayout->addWidget(btnTriggerKernel);
    expertLayout->addWidget(btnResetMem);
    mainLayout->addWidget(expertBox);

    QGroupBox *luaBox = new QGroupBox("Lua Runner");
    QVBoxLayout *luaLayout = new QVBoxLayout(luaBox);
    
    m_luaPathEdit = new QLineEdit();
    m_luaBrowseBtn = new QPushButton("...");
    QHBoxLayout *l1 = new QHBoxLayout();
    l1->addWidget(new QLabel("Script:"));
    l1->addWidget(m_luaPathEdit, 1);
    l1->addWidget(m_luaBrowseBtn);
    luaLayout->addLayout(l1);

    m_luaArgsEdit = new QLineEdit();
    luaLayout->addWidget(new QLabel("Args:"));
    luaLayout->addWidget(m_luaArgsEdit);

    QHBoxLayout *l2 = new QHBoxLayout();
    m_luaRunBtn = new QPushButton("Run Lua");
    m_luaStopBtn = new QPushButton("Stop");
    l2->addWidget(m_luaRunBtn);
    l2->addWidget(m_luaStopBtn);
    luaLayout->addLayout(l2);

    m_luaLog = new QPlainTextEdit();
    m_luaLog->setReadOnly(true);
    m_luaLog->setMinimumHeight(100);
    luaLayout->addWidget(m_luaLog);
    mainLayout->addWidget(luaBox);

    mainLayout->addStretch();
    setWidget(mainWidget);

    connect(m_core, &Core::telemetryUpdated, this, &CoreView::handleUpdate);
    connect(m_core, &Core::hardwareConnectionChanged, this, &CoreView::updateStatus);
    
    connect(m_btnConnect, &QPushButton::clicked, this, [this]() {
        if (m_core->isHardwareConnected()) m_core->disconnectHardware();
        else m_core->connectToHardware(m_ipEdit->text().trimmed(), 22222);
    });

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

    if (realTemp > 75.0) {
        m_freqValue->setStyleSheet("color: #FF0000; font-family: monospace; font-weight: bold;");
    } else {
        m_freqValue->setStyleSheet("color: #00FF41; font-family: monospace; font-weight: bold;");
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

void CoreView::stopLuaScript() {
    m_core->stopLuaScript();
}

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

void CoreView::onBoostClicked() {
    m_core->setVoltage(0x01, 1430000);
}

void CoreView::onSafeModeClicked() {
    m_core->setVoltage(0x01, 900000);
}
