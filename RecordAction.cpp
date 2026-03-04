#include "RecordAction.h"
#include <QVBoxLayout>
#include <QShortcut>
#include <QLabel>

RecordAction::RecordAction(QWidget *parent)
    : QDialog(parent) {

    setWindowTitle("Record Action");
    setMinimumWidth(320);
    auto *layout = new QVBoxLayout(this);
    m_edit = new QLineEdit(this);
    layout->addWidget(new QLabel("Action name:", this));
    layout->addWidget(m_edit);
    m_highlight = new QLabel("Current action: (none)", this);
    m_highlight->setStyleSheet("color: #888;");
    layout->addWidget(m_highlight);
    m_btn = new QPushButton("Action", this);
    m_btn->setAutoDefault(false);
    m_btn->setDefault(false);
    layout->addWidget(m_btn);
    auto *sc = new QShortcut(QKeySequence("Ctrl+A"), this);
    connect(m_edit, &QLineEdit::textChanged, this, &RecordAction::updateHighlight);
    connect(m_btn, &QPushButton::clicked, this, &RecordAction::onActionClicked);
    connect(sc, &QShortcut::activated, this, &RecordAction::onActionClicked);
    m_edit->setFocus();
}

RecordAction::~RecordAction() {}

void RecordAction::updateHighlight() {
    QString t = m_edit->text();
    if (t.isEmpty()) {
        m_highlight->setText("Current action: (none)");
        m_highlight->setStyleSheet("color: #888;");
    } else {
        m_highlight->setText("Current action: " + t);
        m_highlight->setStyleSheet("color: #00aaff; font-weight: bold;");
    }
}

void RecordAction::onActionClicked() {
    QString name = m_edit->text().trimmed();
    if (name.isEmpty())
        return;
    QString mode = m_runModeCombo->currentData().toString();
    emit actionTriggered(name, mode);
    accept();
}
