#include "core_view.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>

CoreView::CoreView(Core *logic, QWidget *parent) 
    : QDockWidget("Hardware Core Monitor", parent), m_core(logic) 
{
    QWidget *mainWidget = new QWidget();
    QVBoxLayout *mainLayout = new QVBoxLayout(mainWidget);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(10);
    QHBoxLayout *connLayout = new QHBoxLayout();
    m_statusLed = new QLabel();
    m_statusLed->setFixedSize(12, 12);
    m_statusLed->setStyleSheet("background: gray; border-radius: 6px;");
    m_ipEdit = new QLineEdit("127.0.0.1");
    m_btnConnect = new QPushButton("ATTACH BRIDGE");
    connLayout->addWidget(m_statusLed);
    connLayout->addWidget(m_ipEdit);
    connLayout->addWidget(m_btnConnect);
    mainLayout->addLayout(connLayout);
    QFrame *teleFrame = new QFrame();
    teleFrame->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    teleFrame->setStyleSheet("background: #111; border-radius: 4px; padding: 5px;");
    QVBoxLayout *teleLayout = new QVBoxLayout(teleFrame);
    teleLayout->addWidget(new QLabel("Core Temperature:"));
    m_tempBar = new QProgressBar();
    m_tempBar->setRange(0, 100);
    m_tempBar->setFormat("%v°C");
    m_tempBar->setStyleSheet(
        "QProgressBar { border: 1px solid grey; border-radius: 2px; text-align: center; color: white; }"
        "QProgressBar::chunk { background-color: qlineargradient(x1:0, y1:0, x2:1, y2:0, "
        "stop:0 green, stop:0.6 yellow, stop:1 red); }"
    );
    teleLayout->addWidget(m_tempBar);

    // Częstotliwość
    QHBoxLayout *freqLayout = new QHBoxLayout();
    freqLayout->addWidget(new QLabel("Frequency:"));
    m_freqValue = new QLabel("0 MHz");
    m_freqValue->setStyleSheet("color: #00FF41; font-family: monospace; font-size: 16px; font-weight: bold;");
    freqLayout->addStretch();
    freqLayout->addWidget(m_freqValue);
    teleLayout->addLayout(freqLayout);

    mainLayout->addWidget(teleFrame);
    QHBoxLayout *actionLayout = new QHBoxLayout();
    QPushButton *boostBtn = new QPushButton("ULTRA BOOST (1.43V)");
    boostBtn->setStyleSheet("background: #d35400; color: white; padding: 10px; font-weight: bold;");
    QPushButton *safeBtn = new QPushButton("SAFE MODE (0.9V)");
    safeBtn->setStyleSheet("background: #2ecc71; color: white; padding: 10px;");
    actionLayout->addWidget(boostBtn);
    actionLayout->addWidget(safeBtn);
    mainLayout->addLayout(actionLayout);
    mainLayout->addStretch();
    setWidget(mainWidget);

    connect(m_core, &Core::telemetryUpdated, this, &CoreView::handleUpdate);
    connect(m_btnConnect, &QPushButton::clicked, this, &CoreView::onAttachClicked);
    connect(boostBtn, &QPushButton::clicked, [this](){ m_core->setVoltage(0x01, 1430000); });
    connect(safeBtn, &QPushButton::clicked, [this](){ m_core->setVoltage(0x01, 900000); });
}

void CoreView::onAttachClicked() {updateStatus(true);}

void CoreView::updateStatus(bool attached) {
    if (attached) {
        m_statusLed->setStyleSheet("background: #00FF41; border-radius: 6px; border: 1px solid white;");
        m_btnConnect->setText("DETACH");
    } else {
        m_statusLed->setStyleSheet("background: gray; border-radius: 6px;");
        m_btnConnect->setText("ATTACH BRIDGE");
    }
}

void CoreView::handleUpdate(uint32_t temp, uint64_t freq) {
    double realTemp = temp / 1000.0;
    m_tempBar->setValue(static_cast<int>(realTemp));
    m_freqValue->setText(QString("%1 MHz").arg(freq / 1000));
    if (realTemp > 75.0) {
        m_freqValue->setStyleSheet("color: #FF0000; font-family: monospace; font-size: 18px; font-weight: bold;");
    } else {
        m_freqValue->setStyleSheet("color: #00FF41; font-family: monospace; font-size: 16px; font-weight: bold;");
    }
}

void CoreView::onBoostClicked() {
    //
}

void CoreView::onSafeModeClicked() {
    //
}
