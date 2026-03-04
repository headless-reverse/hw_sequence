#include "view_process_injector.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QDebug>
#include <QTimer>
#include <QClipboard>
#include <QRegularExpression>
#include <QSplitter>
#include <QMenu>
#include <QHeaderView>

ViewProcessInjector::ViewProcessInjector(QWidget *parent)
    : QDockWidget("Process Injector - hw_resident", parent), 
      m_isRefreshing(false),
      m_selectedPid(-1),
      m_injectionInProgress(false)
{
    setupUi();
    setupConnections();
    loadLibraryPathsFromConfig();
}

void ViewProcessInjector::setupUi() {
    auto *container = new QWidget();
    auto *mainLayout = new QVBoxLayout(container);
    mainLayout->setContentsMargins(2, 2, 2, 2);
    mainLayout->setSpacing(2);

    auto *libPathLayout = new QHBoxLayout();
    m_libPathCombo = new QComboBox();
    m_libPathCombo->setEditable(true);
    m_libPathCombo->addItem("/system/lib/resident_lib.so");
    QPushButton *btnBrowse = new QPushButton("📁");
    btnBrowse->setFixedWidth(30);
    connect(btnBrowse, &QPushButton::clicked, this, &ViewProcessInjector::onBrowseLibPath);
    
    libPathLayout->addWidget(new QLabel("Lib:"));
    libPathLayout->addWidget(m_libPathCombo, 1);
    libPathLayout->addWidget(btnBrowse);
    mainLayout->addLayout(libPathLayout);

    m_refreshBtn = new QPushButton("🔄 Odśwież");
    
    m_treeWidget = new QTreeWidget();
    m_treeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    m_treeWidget->setColumnCount(4);
    m_treeWidget->setHeaderLabels({"PID / Address", "Process Name / Perms", "Status / Offset", "Injected / Path"});
    
    m_treeWidget->header()->setSectionResizeMode(QHeaderView::Interactive);
    m_treeWidget->header()->setStretchLastSection(true);
    m_treeWidget->header()->setCascadingSectionResizes(true);
    m_treeWidget->header()->setDefaultSectionSize(100);

    m_treeWidget->setAnimated(true);
    m_treeWidget->setIndentation(20);
    m_treeWidget->setStyleSheet("QTreeWidget { font-size: 10px; } QTreeWidget::item { height: 20px; }");

    mainLayout->addWidget(m_refreshBtn);
    mainLayout->addWidget(m_treeWidget, 1);

    auto *btnLayout = new QHBoxLayout();
    m_injectBtn = new QPushButton("💉 INJECT");
    m_mapsRefreshBtn = new QPushButton("📟 mem/maps");
    m_killBtn = new QPushButton("💀 KILL");
    
    btnLayout->addWidget(m_injectBtn, 2);
    btnLayout->addWidget(m_mapsRefreshBtn, 1);
    btnLayout->addWidget(m_killBtn, 1);
    mainLayout->addLayout(btnLayout);

    m_statusLabel = new QLabel(" Ready");
    m_statusLabel->setStyleSheet("font-size: 9px; border-top: 1px solid #ccc; color: #555;");
    mainLayout->addWidget(m_statusLabel);

    setWidget(container);
}

void ViewProcessInjector::setupConnections() {
    connect(m_refreshBtn, &QPushButton::clicked, this, &ViewProcessInjector::onRefreshClicked);
    connect(m_injectBtn, &QPushButton::clicked, this, &ViewProcessInjector::onInjectClicked);
    connect(m_killBtn, &QPushButton::clicked, this, &ViewProcessInjector::onKillClicked);
    connect(m_mapsRefreshBtn, &QPushButton::clicked, this, &ViewProcessInjector::onMapsRefreshClicked);
    connect(m_treeWidget, &QTreeWidget::customContextMenuRequested, this, &ViewProcessInjector::onCustomContextMenuRequested);
    connect(m_treeWidget, &QTreeWidget::itemSelectionChanged, this, &ViewProcessInjector::onProcessSelectionChanged);

    connect(m_treeWidget, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem *item) {
        if (item->parent() == nullptr && item->childCount() == 0) {
            int pid = item->text(0).toInt();
            if (pid > 0) {
                addLogEntry(QString("Loading maps for PID %1...").arg(pid));
                emit memMapsRequested(pid);
                auto *loading = new QTreeWidgetItem(item);
                loading->setText(1, "⏳ Loading memory maps...");
            }
        }
    });
}

void ViewProcessInjector::onSetLibraryPath() {
    QString libPath = m_libPathCombo->currentText().trimmed();
    if (libPath.isEmpty()) return;
    emit libraryPathChanged(libPath);
    showStatus(QString("Library set: %1").arg(libPath), false);
}

void ViewProcessInjector::onBrowseLibPath() {
    QString path = QFileDialog::getOpenFileName(
        this, "Select Library", "/system/lib/", "Shared Objects (*.so);;All Files (*)"
    );
    if (!path.isEmpty()) m_libPathCombo->setCurrentText(path);
}

void ViewProcessInjector::displayMemMaps(int pid, const QString &maps) {
    QList<QTreeWidgetItem*> items = m_treeWidget->findItems(QString::number(pid), Qt::MatchExactly, 0);
    if (items.isEmpty()) return;
    QTreeWidgetItem *processItem = items.first();
    processItem->takeChildren(); 
    QStringList lines = maps.split('\n', Qt::SkipEmptyParts);
    int validLinesCount = 0;
    for (const QString &rawLine : lines) {
        QString cleanLine = "";
        for(QChar c : rawLine) {
            int ascii = c.toLatin1();
            if (ascii >= 32 && ascii <= 126) {
                cleanLine += c;
            }
        }
        QString line = cleanLine.trimmed();
        if (line.isEmpty()) continue;
        QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.size() >= 2 && parts[0].contains('-')) {
            if (parts[0].length() < 7) continue;
            auto *mapItem = new QTreeWidgetItem(processItem);
            mapItem->setText(0, parts[0]);
            mapItem->setText(1, parts[1]);
            mapItem->setText(2, (parts.size() > 2) ? parts[2] : "0");
            QString path = "[anon]";
            if (parts.size() > 5) {
                path = parts.mid(5).join(" ");
            }
            mapItem->setText(3, path.trimmed());
            for(int i = 0; i < 4; ++i) {
                mapItem->setForeground(i, QColor("#A0A0A0"));
                mapItem->setFont(i, QFont("Monospace", 8));
            }
            validLinesCount++;
        }
    }
    processItem->setExpanded(true);
    showStatus(QString("✓ Loaded %1 regions for PID %2").arg(validLinesCount).arg(pid), false);
}

void ViewProcessInjector::addProcessItem(int pid, const QString &name) {
    QList<QTreeWidgetItem*> existing = m_treeWidget->findItems(QString::number(pid), Qt::MatchExactly, 0);
    if (!existing.isEmpty()) return;
    auto *item = new QTreeWidgetItem(m_treeWidget);
    item->setText(0, QString::number(pid));
    item->setText(1, name);
    item->setText(2, "✓ Active");
    item->setText(3, "❌ Not injected");
    QFont font = item->font(0);
    font.setBold(true);
    item->setFont(0, font);
    item->setFont(1, font);
    item->setBackground(0, QColor("#f0f7ff"));
    item->setBackground(1, QColor("#f0f7ff"));
}

void ViewProcessInjector::updateProcessInjectionStatus(int pid, const QString &status) {
    QList<QTreeWidgetItem*> items = m_treeWidget->findItems(QString::number(pid), Qt::MatchExactly, 0);
    if (!items.isEmpty()) {
        QTreeWidgetItem *item = items.first();
        item->setText(3, status);
        if (status.contains("✓")) item->setBackground(3, QColor("#c8e6c9"));
        else if (status.contains("⏳")) item->setBackground(3, QColor("#fff9c4"));
        else item->setBackground(3, QColor("#ffcdd2"));
    }
}

void ViewProcessInjector::onRefreshClicked() {
    m_isRefreshing = true;
    m_refreshBtn->setEnabled(false);
    m_injectBtn->setEnabled(false);
    m_killBtn->setEnabled(false);
    clearProcessList();
    showStatus("⏳ Loading process list...", false);
    emit refreshRequested();
}

void ViewProcessInjector::onInjectClicked() {
    if (m_selectedPid <= 0) return;
    QString libPath = m_libPathCombo->currentText().trimmed();
    if (libPath.isEmpty()) {
        showStatus("Library path not set!", true);
        return;
    }
    addLogEntry(QString("💉 Start injection: PID %1 using %2").arg(m_selectedPid).arg(libPath));
    emit libraryPathChanged(libPath);
    m_injectionInProgress = true;
    m_injectBtn->setEnabled(false);
    m_refreshBtn->setEnabled(false);
    updateProcessInjectionStatus(m_selectedPid, "⏳ Injecting...");
    emit injectRequested(m_selectedPid);
    QTimer::singleShot(5000, this, [this]() {
        if (m_injectionInProgress) {
            m_injectionInProgress = false;
            m_refreshBtn->setEnabled(true);
            m_injectBtn->setEnabled(true);
            addLogEntry(QString("⚠️ Injection timeout for PID %1").arg(m_selectedPid));
            showStatus("Injection timeout", true);
        }
    });
}

void ViewProcessInjector::onKillClicked() {
    if (m_selectedPid <= 0) return;
    addLogEntry(QString("💀 Kill signal sent to PID %1").arg(m_selectedPid));
    emit killRequested(m_selectedPid);
    updateProcessInjectionStatus(m_selectedPid, "❌ Killed");
}

void ViewProcessInjector::onMapsRefreshClicked() {
    if (m_selectedPid <= 0) return;
    emit memMapsRequested(m_selectedPid);
}

void ViewProcessInjector::onCustomContextMenuRequested(const QPoint &pos) {
    QTreeWidgetItem *item = m_treeWidget->itemAt(pos);
    if (!item) return;

    QTreeWidgetItem *processItem = (item->parent() == nullptr) ? item : item->parent();
    m_selectedPid = processItem->text(0).toInt();

    QMenu menu(this);
    menu.addAction("💉 Inject Library", this, &ViewProcessInjector::onInjectClicked);
    menu.addAction("📟 Refresh Maps", this, &ViewProcessInjector::onMapsRefreshClicked);
    menu.addSeparator();
    menu.addAction("📋 Copy PID", [this]() {
        QApplication::clipboard()->setText(QString::number(m_selectedPid));
    });
    menu.addSeparator();
    menu.addAction("💀 Kill Process (Instant)", this, &ViewProcessInjector::onKillClicked);
    
    menu.exec(m_treeWidget->viewport()->mapToGlobal(pos));
}

void ViewProcessInjector::onProcessSelectionChanged() {
    QTreeWidgetItem *item = m_treeWidget->currentItem();
    if (item) {
        QTreeWidgetItem *processItem = (item->parent() == nullptr) ? item : item->parent();
        m_selectedPid = processItem->text(0).toInt();
        m_injectBtn->setEnabled(!m_isRefreshing && !m_injectionInProgress);
        m_killBtn->setEnabled(!m_isRefreshing);
        m_mapsRefreshBtn->setEnabled(true);
    }
}

void ViewProcessInjector::clearProcessList() {
    m_treeWidget->clear();
    m_selectedPid = -1;
}

void ViewProcessInjector::showStatus(const QString &msg, bool isError) {
    m_statusLabel->setText(" " + msg);
    if (isError) m_statusLabel->setStyleSheet("font-size: 9px; color: white; background-color: #d32f2f;");
    else m_statusLabel->setStyleSheet("font-size: 9px; color: #444;");
}

void ViewProcessInjector::addLogEntry(const QString &entry) {
    qDebug() << "[Injector]" << entry;
    emit logRequested(entry);
    showStatus(entry, false);
}

void ViewProcessInjector::onListFinished() {
    m_isRefreshing = false;
    m_refreshBtn->setEnabled(true);
    int count = m_treeWidget->topLevelItemCount();
    showStatus(QString("✓ Process list ready (%1 processes)").arg(count), false);
}

void ViewProcessInjector::onInjectionSuccess(int pid) {
    m_injectionInProgress = false;
    m_injectBtn->setEnabled(true);
    m_refreshBtn->setEnabled(true);
    updateProcessInjectionStatus(pid, "✓ Injected");
    showStatus("✓ Injection successful", false);
}

void ViewProcessInjector::onInjectionFailed(int pid, const QString &error) {
    m_injectionInProgress = false;
    m_refreshBtn->setEnabled(true);
    m_injectBtn->setEnabled(true);
    updateProcessInjectionStatus(pid, "❌ Failed");
    QMessageBox::critical(this, "Injection Error", 
        QString("Failed to inject PID %1\nReason: %2").arg(pid).arg(error));
    showStatus("❌ Injection failed: " + error, true);
}

void ViewProcessInjector::setConnected(bool connected) {
    showStatus(connected ? "✓ Connected" : "✗ Disconnected", !connected);
}

void ViewProcessInjector::onStopClicked() {
    m_isRefreshing = false;
    m_refreshBtn->setEnabled(true);
}

void ViewProcessInjector::loadLibraryPathsFromConfig() {
    m_libPathCombo->addItem("/system/lib64/resident_lib.so");
	m_libPathCombo->addItem("/vendor/lib/resident_lib.so");
	m_libPathCombo->addItem("/data/local/tmp/test_lib.so");
}
