#include "mainwindow.h"
#include "view_swipe_builder.h"
#include "commandexecutor.h"
#include "settingsdialog.h"
#include "sequencerunner.h"
#include "argsparser.h"
#include "view_hardware_grabbed.h"
#include "view_sequencerunner.h"
#include "hw_keyboard.h"
#include "nlohmann/json.hpp"

#include <fstream>
#include <iostream>
#include <QApplication>
#include <QCoreApplication>
#include <QStandardItemModel>
#include <QSortFilterProxyModel>
#include <QTreeView>
#include <QDockWidget>
#include <QListWidget>
#include <QMenuBar>
#include <QInputDialog>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QLineEdit>
#include <QTextEdit>
#include <QMessageBox>
#include <QFileInfo>
#include <QEvent>
#include <QKeyEvent>
#include <QFileDialog>
#include <QDir>
#include <QHeaderView>
#include <QAction>
#include <QCloseEvent>
#include <QMenu>
#include <QContextMenuEvent>
#include <QStandardItem>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QComboBox>
#include <QRegularExpression>
#include <QStatusBar>

class LogDialog : public QDialog {
public:
    explicit LogDialog(QWidget *parent = nullptr) : QDialog(parent) {
        setWindowTitle("headless");
        resize(900, 400);
        setStyleSheet("background: #000; border: none;");
        auto layout = new QVBoxLayout(this);
        m_output = new QTextEdit();
        m_output->setReadOnly(true);
        m_output->setStyleSheet("background: #000; color: #f0f0f0; border: none; font-family: monospace;");
        layout->addWidget(m_output);
        m_fontSize = m_output->font().pointSize();
        installEventFilter(this);}
    void setDocument(QTextDocument *doc) {if (doc) m_output->setDocument(doc);}
    void appendText(const QString &text, const QColor &color = QColor("#f0f0f0")) {
        m_output->setTextColor(color);
        m_output->append(text);
        m_output->setTextColor(QColor("#f0f0f0"));}
    void clear() { m_output->clear(); }
    QString text() const { return m_output->toPlainText(); }
protected:
    bool eventFilter(QObject *obj, QEvent *event) override {
    if (event->type() == QEvent::KeyPress) {QKeyEvent *ke = static_cast<QKeyEvent*>(event);
    if (ke->modifiers() == Qt::ControlModifier && ke->key() == Qt::Key_C) {return false;}}
    return QDialog::eventFilter(obj, event);}
    void wheelEvent(QWheelEvent *event) override {
    if (QApplication::keyboardModifiers() == Qt::ControlModifier) {
    int delta = event->angleDelta().y();
    if (delta > 0) m_fontSize = qMin(m_fontSize + 1, 32);
    else m_fontSize = qMax(m_fontSize - 1, 8);
    QFont f = m_output->font(); f.setPointSize(m_fontSize); m_output->setFont(f);
    event->accept();} else QDialog::wheelEvent(event);}
private:
    QTextEdit *m_output = nullptr;
    int m_fontSize = 8;};

MainWindow::MainWindow(QWidget *parent, const QString &adbPath, const QString &targetSerial)
    : QMainWindow(parent){
    setWindowTitle("🖥️ hardware_sequence® ⛓️");
    resize(1100, 720);
    setAcceptDrops(true);
    ensureJsonPathLocal();
    m_executor = new CommandExecutor(this);
    m_executor->setAdbPath(adbPath);
    m_executor->setTargetDevice(targetSerial);
    m_videoClient = new VideoClient(this);
    m_videoClient->setAdbPath(adbPath);
    m_videoClient->setDeviceSerial(targetSerial);
    m_commandTimer = new QTimer(this);
    connect(m_commandTimer, &QTimer::timeout, this, &MainWindow::executeScheduledCommand); 
    m_sequenceRunner = new SequenceRunner(m_executor, this);
    connect(m_sequenceRunner, &SequenceRunner::sequenceStarted, this, &MainWindow::onSequenceStarted);
    connect(m_sequenceRunner, &SequenceRunner::sequenceFinished, this, &MainWindow::onSequenceFinished);
    connect(m_sequenceRunner, &SequenceRunner::commandExecuting, this, &MainWindow::onSequenceCommandExecuting);
    connect(m_sequenceRunner, &SequenceRunner::logMessage, this, &MainWindow::handleSequenceLog);
	m_sequenceIntervalTimer = new QTimer(this);
	m_sequenceIntervalTimer->setSingleShot(true);
	connect(m_sequenceIntervalTimer, &QTimer::timeout, this, &MainWindow::startIntervalSequence);
	connect(m_sequenceRunner, &SequenceRunner::scheduleRestart, this, [this](int interval){
		if (m_sequenceIntervalToggle && !m_sequenceIntervalToggle->isChecked()) {
			return;}
		if (!m_sequenceIntervalTimer) return;
		if (m_sequenceIntervalTimer->isActive()) m_sequenceIntervalTimer->stop();
		m_sequenceIntervalTimer->setInterval(interval * 1000);
		m_sequenceIntervalTimer->start();
		updateTimerDisplay();});
    m_displayTimer = new QTimer(this);
    m_displayTimer->setInterval(100);
    connect(m_displayTimer, &QTimer::timeout, this, &MainWindow::updateTimerDisplay);
    m_displayTimer->start();
    m_isRootShell = m_settings.value("isRootShell", false).toBool();
    connect(m_executor, &CommandExecutor::outputReceived, this, &MainWindow::onOutput);
    connect(m_executor, &CommandExecutor::errorReceived, this, &MainWindow::onError);
    connect(m_executor, &CommandExecutor::started, this, &MainWindow::onProcessStarted);
    connect(m_executor, &CommandExecutor::finished, this, &MainWindow::onProcessFinished);
    setupMenus();
// Kategorie
    m_categoryList = new QListWidget();
    m_categoryList->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_categoryList, &QListWidget::currentItemChanged, this, &MainWindow::onCategoryChanged);
    m_dockCategories = new QDockWidget(tr("Categories"), this);
    m_dockCategories->setObjectName("dockCategories");
    m_dockCategories->setWidget(m_categoryList);
    m_dockCategories->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::LeftDockWidgetArea, m_dockCategories);
// Komendy
    m_commandModel = new QStandardItemModel(this);
    m_commandModel->setHorizontalHeaderLabels(QStringList{"Command", "Description"});
    m_commandProxy = new QSortFilterProxyModel(this);
    m_commandProxy->setSourceModel(m_commandModel);
    m_commandProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_commandProxy->setFilterKeyColumn(-1);
    m_commandView = new QTreeView();
    m_commandView->setModel(m_commandProxy);
    m_commandView->setRootIsDecorated(false);
    m_commandView->setAlternatingRowColors(true);
    m_commandView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_commandView->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	m_commandView->header()->setSectionResizeMode(1, QHeaderView::Stretch);
	m_commandView->header()->setStretchLastSection(true);
	m_commandView->setEditTriggers(QAbstractItemView::NoEditTriggers);
	connect(m_commandView, &QTreeView::doubleClicked, this, &MainWindow::onCommandDoubleClicked);
// Kontekstowe menu
    m_commandView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_commandView, &QWidget::customContextMenuRequested, this, [this](const QPoint &pt){
        QModelIndex idx = m_commandView->indexAt(pt);
        if (!idx.isValid()) return;
        QModelIndex s = m_commandProxy->mapToSource(idx);
        if (!s.isValid()) return;
        QString cmd = m_commandModel->item(s.row(), 0)->text();
        QMenu menu(this);
        menu.addAction("Execute", [this, cmd](){ m_commandEdit->setText(cmd); runCommand(); });
        menu.addAction("Edit", [this, s](){ m_commandView->selectionModel()->clear(); m_commandView->setCurrentIndex(m_commandProxy->mapFromSource(s)); editCommand(); });
        menu.addAction("Remove", [this, s](){ m_commandView->selectionModel()->clear(); m_commandView->setCurrentIndex(m_commandProxy->mapFromSource(s)); removeCommand(); });
        menu.exec(m_commandView->viewport()->mapToGlobal(pt));});
    m_dockCommands = new QDockWidget(tr("Commands"), this);
    m_dockCommands->setObjectName("dockCommands");
    m_dockCommands->setWidget(m_commandView);
    m_dockCommands->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::TopDockWidgetArea);
    addDockWidget(Qt::LeftDockWidgetArea, m_dockCommands);
    splitDockWidget(m_dockCategories, m_dockCommands, Qt::Horizontal);
// Log Output
    m_log = new QTextEdit();
    m_log->setReadOnly(true);
    m_log->setStyleSheet("background: #000; color: #f0f0f0; font-family: monospace;");
    m_dockLog = new QDockWidget(tr("Execution Console"), this);
    m_dockLog->setObjectName("dockLog");
    m_dockLog->setWidget(m_log);
    m_dockLog->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::BottomDockWidgetArea, m_dockLog);
    m_dockControls = new QDockWidget(tr("Controls"), this);
    m_dockControls->setObjectName("dockControls");
    m_dockControls->setWidget(createControlsWidget());
    m_dockControls->setAllowedAreas(Qt::TopDockWidgetArea | Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
    addDockWidget(Qt::TopDockWidgetArea, m_dockControls);
    connect(m_commandView, &QTreeView::clicked, [this](const QModelIndex &idx){
        QModelIndex s = m_commandProxy->mapToSource(idx);
        if (s.isValid()) {
            QString cmd = m_commandModel->item(s.row(), 0)->text();
            if (m_commandEdit) {
                m_commandEdit->setText(cmd);}
            if (m_dockControls) {
                m_dockControls->setVisible(true);
                m_dockControls->raise();
                if (m_commandEdit) { m_commandEdit->setFocus(); m_commandEdit->selectAll(); } }}});
    setupSequenceDock();
// SWIPE_BUILDER
    m_swipeBuilder = new ViewSwipeBuilder(m_executor, m_videoClient, this);
    m_swipeBuilder->setAdbPath(m_executor->adbPath());
    connect(m_swipeBuilder, &ViewSwipeBuilder::adbStatus,
            this, [this](const QString &message, bool isError) {
        appendLog(message, isError ? "#F44336" : "#00BCD4");});
    connect(m_swipeBuilder, &ViewSwipeBuilder::runFullSequenceRequested, this, [this](){
        if (m_swipeBuilder->model()->actions().isEmpty()) {
            appendLog("[SwipeBuilder]: Cannot run empty sequence.", "#F44336");
            return;}
        QJsonArray sequenceArray = m_swipeBuilder->model()->toJsonSequence();
        m_sequenceRunner->clearSequence();
        if (m_sequenceRunner->loadSequenceFromJsonArray(sequenceArray)) {
            appendLog("[SwipeBuilder]: Loaded sequence into runner. starting execution...", "#4CAF50");
            m_sequenceRunner->startSequence();
            if (m_dockSequence) {
                 m_dockSequence->show();
                 m_dockSequence->raise();}
        } else {
            appendLog("[SwipeBuilder]: failed to load sequence from model.", "#F44336");}});
    connect(m_sequenceRunner, &SequenceRunner::commandExecuting, 
            m_swipeBuilder, &ViewSwipeBuilder::onSequenceCommandExecuting);
    m_dockBuilder = new QDockWidget(tr("swipe_builder"), this);
    m_dockBuilder->setObjectName("dockBuilder");
    m_dockBuilder->setWidget(m_swipeBuilder);
    m_dockBuilder->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, m_dockBuilder);
    m_dockBuilder->close();
    connect(m_dockBuilder, &QDockWidget::visibilityChanged, this, [this](bool visible){
        if (visible) m_swipeBuilder->startMonitoring();
        else m_swipeBuilder->stopMonitoring();});
    connect(m_swipeBuilder, &ViewSwipeBuilder::sequenceGenerated, this, [this](const QString &path){
        appendLog(QString("Sequence saved to JSON: %1. Use 'Load Sequence' to view in Runner.").arg(QFileInfo(path).fileName()), "#4CAF50");});
// KEYBOARD_ADB (input_keyevent)
	m_keyboardWidget = new KeyboardWidget(this);
	m_dockKeyboard = new QDockWidget(tr("input_keyevent"), this);
	m_dockKeyboard->setObjectName("dockKeyboard");
	m_dockKeyboard->setWidget(m_keyboardWidget);
	m_dockKeyboard->setAllowedAreas(Qt::AllDockWidgetAreas);
	addDockWidget(Qt::RightDockWidgetArea, m_dockKeyboard);
	connect(m_keyboardWidget, &KeyboardWidget::adbCommandGenerated, this, [this](const QString &cmd){
//		m_executor->executeAdbCommand(QString("shell su -c \"%1\"").arg(cmd));
		m_executor->executeSequenceCommand(cmd, "root");
		if (m_keyboardWidget->isRecordModeActive()) {
			QString keyOnly = cmd.startsWith("input keyevent") ? cmd.mid(15).trimmed() : cmd;
			m_swipeBuilder->model()->addKey(keyOnly, 100);}});
// HW_RESIDENT
	m_hardwareGrabbedWidget = new ViewHardwareGrabbed(m_executor, this);
	m_executor->setHardwareInterface(m_hardwareGrabbedWidget->hardwareLogic());
	connect(m_hardwareGrabbedWidget, &ViewHardwareGrabbed::hwKeyGenerated, this, [this](int linuxCode){
    if (m_hardwareGrabbedWidget->isRecordModeActive()) {
        if (m_swipeBuilder->model()) {
			m_swipeBuilder->model()->addCommand(QString("HW_SOCKET_SEND %1").arg(linuxCode), 100, "ioctl");}}});
//	m_hardwareGrabbedWidget->setRecordModel(m_swipeBuilder->model());
	m_dockHardwareGrabbed = new QDockWidget(tr("hw_resident"), this);
	m_dockHardwareGrabbed->setObjectName("dockHardwareGrabbed");
	m_dockHardwareGrabbed->setWidget(m_hardwareGrabbedWidget);
	m_dockHardwareGrabbed->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);
	addDockWidget(Qt::RightDockWidgetArea, m_dockHardwareGrabbed);
	m_viewHardwareGrabbedAct = m_dockHardwareGrabbed->toggleViewAction();
// Menu Widoku
    m_viewCategoriesAct = m_dockCategories->toggleViewAction();
    m_viewCommandsAct = m_dockCommands->toggleViewAction();
    m_viewLogAct = m_dockLog->toggleViewAction();
    m_viewControlsAct = m_dockControls->toggleViewAction();
    m_viewSequenceAct = m_dockSequence->toggleViewAction();
    m_viewBuilderAct = m_dockBuilder->toggleViewAction();
    m_viewKeyboardAct = m_dockKeyboard->toggleViewAction();
    QMenu *viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction(m_viewCategoriesAct);
    viewMenu->addAction(m_viewCommandsAct);
    viewMenu->addAction(m_viewControlsAct);
    viewMenu->addAction(m_viewSequenceAct);
    viewMenu->addAction(m_viewBuilderAct);
    viewMenu->addAction(m_viewLogAct);
	viewMenu->addAction(m_viewHardwareGrabbedAct);
	viewMenu->addAction(m_viewKeyboardAct);
// Detached Log Dialog
    LogDialog *dlg = new LogDialog(this);
    dlg->setDocument(m_log->document());
    dlg->move(this->x() + this->width() + 20, this->y());
    dlg->show();
    m_detachedLogDialog = dlg;
    loadCommands();
    populateCategoryList();
    if (m_categoryList->count() > 0) m_categoryList->setCurrentRow(0);
    if (!m_settings.contains("windowState")) { restoreDefaultLayout(); }
    restoreWindowStateFromSettings();
    m_commandView->installEventFilter(this);
    m_categoryList->installEventFilter(this);
    if (m_commandEdit) m_commandEdit->installEventFilter(this);
    refreshDeviceList();}

MainWindow::~MainWindow() {
	if (m_commandTimer && m_commandTimer->isActive()) m_commandTimer->stop();
	if (m_sequenceIntervalTimer && m_sequenceIntervalTimer->isActive()) m_sequenceIntervalTimer->stop();
	if (m_displayTimer && m_displayTimer->isActive()) m_displayTimer->stop();
	saveCommands();
	saveWindowStateToSettings();}

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {if (event->mimeData()->hasUrls()) {event->acceptProposedAction();}}

void MainWindow::dropEvent(QDropEvent *event) {
    const QMimeData* mimeData = event->mimeData();
    if (mimeData->hasUrls()) {for (const QUrl &url : mimeData->urls()) {
		QString filePath = url.toLocalFile();
		if (filePath.endsWith(".json", Qt::CaseInsensitive)) {
			if (m_viewSequencerunner) {
				m_viewSequencerunner->loadSequenceFromFile(filePath);}}}}}

void MainWindow::setupMenus() {
    QMenu *file = menuBar()->addMenu("&File");
    QAction *addAct = file->addAction("Add Command...");
    addAct->setShortcut(QKeySequence(tr("Ctrl+A")));
    connect(addAct, &QAction::triggered, this, &MainWindow::addCommand);
    QAction *editAct = file->addAction("Edit Command...");
    editAct->setShortcut(QKeySequence(tr("Ctrl+E")));
    connect(editAct, &QAction::triggered, this, &MainWindow::editCommand);
    QAction *removeAct = file->addAction("Remove Command");
    removeAct->setShortcut(QKeySequence(tr("Ctrl+R")));
    connect(removeAct, &QAction::triggered, this, &MainWindow::removeCommand);
    file->addSeparator();
    QAction *loadAct = file->addAction("Load commands…");
    QAction *saveAct = file->addAction("Save commands as…");
    file->addSeparator();
    QAction *quitAct = file->addAction("Quit");    
    connect(loadAct, &QAction::triggered, this, [this]{
        QDir startDir = QDir("/usr/local/etc/adb_shell");
        QString fn = QFileDialog::getOpenFileName(
                                             this,
                                             tr("Load JSON"),
                                             startDir.path(),
                                             tr("JSON files (*.json);;All files (*)"));
        if (!fn.isEmpty()) {
            m_jsonFile = fn;
            loadCommands();
            populateCategoryList();}});
    connect(saveAct, &QAction::triggered, this, [this]{
        QDir startDir = QDir("/usr/local/etc/adb_shell");
        QString fn = QFileDialog::getSaveFileName(
                                             this,
                                             tr("Save JSON"),
                                             startDir.filePath("adb_commands.json"),
                                             tr("JSON files (*.json);;All files (*)"));
        if (!fn.isEmpty()) {
            m_jsonFile = fn;
            saveCommands();}});
    connect(quitAct, &QAction::triggered, this, &QMainWindow::close);    
    QMenu *proc = menuBar()->addMenu("&Process");
    QAction *stopAct = proc->addAction("Stop current command");
    connect(stopAct, &QAction::triggered, this, &MainWindow::stopCommand);    
    QMenu *settings = menuBar()->addMenu("&Settings");
    QAction *restoreAct = settings->addAction("Restore default layout");
    connect(restoreAct, &QAction::triggered, this, &MainWindow::restoreDefaultLayout);
    QAction *appSettingsAct = settings->addAction("Application settings...");
    connect(appSettingsAct, &QAction::triggered, this, &MainWindow::showSettingsDialog);}

// /usr/local/etc/adb_shell
void MainWindow::ensureJsonPathLocal() {m_jsonFile = QDir("/usr/local/etc/adb_shell").filePath("adb_commands.json");}

void MainWindow::setupSequenceDock() {
    if (!m_sequenceRunner) {
        m_sequenceRunner = new SequenceRunner(m_executor, this);
        connect(m_sequenceRunner, &SequenceRunner::sequenceStarted, this, &MainWindow::onSequenceStarted);
        connect(m_sequenceRunner, &SequenceRunner::sequenceFinished, this, &MainWindow::onSequenceFinished);
        connect(m_sequenceRunner, &SequenceRunner::commandExecuting, this, &MainWindow::onSequenceCommandExecuting);
        connect(m_sequenceRunner, &SequenceRunner::logMessage, this, &MainWindow::handleSequenceLog);
        connect(m_sequenceRunner, &SequenceRunner::scheduleRestart, this, [this](int interval){
            if (m_sequenceIntervalToggle && !m_sequenceIntervalToggle->isChecked()) {
                return;}
            if (!m_sequenceIntervalTimer) return;
            if (m_sequenceIntervalTimer->isActive()) m_sequenceIntervalTimer->stop();
            m_sequenceIntervalTimer->setInterval(interval * 1000);
            m_sequenceIntervalTimer->start();
            updateTimerDisplay();});}
    m_viewSequencerunner = new ViewSequencerunner(m_sequenceRunner, m_executor, this);
    m_dockSequence = new QDockWidget(tr("sequence runner"), this);
    m_dockSequence->setObjectName("dockSequence");
    m_dockSequence->setWidget(m_viewSequencerunner); 
    addDockWidget(Qt::RightDockWidgetArea, m_dockSequence);}

QWidget* MainWindow::createControlsWidget() {
    QWidget *w = new QWidget();
    w->setMinimumHeight(90); 
    w->setMinimumWidth(350);
    auto mainLayout = new QVBoxLayout(w);
    mainLayout->setContentsMargins(5, 5, 5, 5);
    m_commandEdit = new QLineEdit();
    connect(m_commandEdit, &QLineEdit::returnPressed, this, &MainWindow::runCommand);
    mainLayout->addWidget(m_commandEdit);
    auto mixedLayout = new QHBoxLayout();
    m_deviceCombo = new QComboBox();
    m_deviceCombo->setMinimumWidth(90); 
    m_deviceCombo->addItem("detecting...");
    connect(m_deviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onDeviceSelected);
    mixedLayout->addWidget(m_deviceCombo, 1);
    m_refreshDevicesBtn = new QPushButton("↻"); 
    m_refreshDevicesBtn->setMaximumWidth(30);
    connect(m_refreshDevicesBtn, &QPushButton::clicked, this, &MainWindow::refreshDeviceList);
    mixedLayout->addWidget(m_refreshDevicesBtn);
    mixedLayout->addSpacing(10);
    m_intervalSpinBox = new QSpinBox();
    m_intervalSpinBox->setRange(1, 86400000);
    m_intervalSpinBox->setValue(5000);
    m_intervalSpinBox->setSuffix(" ms");
    m_intervalSpinBox->setMaximumWidth(100);
    mixedLayout->addWidget(m_intervalSpinBox);
    m_intervalToggle = new QCheckBox("Interval");
    m_intervalToggle->setChecked(false);
    mixedLayout->addWidget(m_intervalToggle);    
    m_commandTimerLabel = new QLabel("(0ms)");
    m_commandTimerLabel->setStyleSheet("color: black; font-weight: normal;");
    mixedLayout->addWidget(m_commandTimerLabel);
	m_scheduleBtn = new QPushButton("Schedule");
	m_scheduleBtn->setMaximumWidth(70);
	m_scheduleBtn->setStyleSheet("background-color: #FFAA00; color: black;");
    connect(m_scheduleBtn, &QPushButton::clicked, this, &MainWindow::onScheduleButtonClicked);
    mixedLayout->addWidget(m_scheduleBtn);    
    QPushButton *stopTimerBtn = new QPushButton("Stop");
	stopTimerBtn->setMaximumWidth(50);
	stopTimerBtn->setStyleSheet("background-color: #FF0000; color: black;");
    connect(stopTimerBtn, &QAbstractButton::clicked, this, [this]{
        if (m_commandTimer->isActive()) {
            m_commandTimer->stop();
			m_intervalToggle->setChecked(false);
			appendLog("Timer stopped.", "#E68D8D");
			m_commandTimerLabel->setText("(0ms)");}});
    mixedLayout->addWidget(stopTimerBtn);
    mainLayout->addLayout(mixedLayout);
	auto btnLayout = new QHBoxLayout();
// Checkbox IOCTL (Direct Hardware)
	m_ioctlToggle = new QCheckBox("ioctl");
	m_ioctlToggle->setToolTip("Direct Hardware Injection via hw_resident");
	m_ioctlToggle->setChecked(m_settings.value("isIoctlCommand", false).toBool());
	connect(m_ioctlToggle, &QCheckBox::checkStateChanged, this, [this](int state) {
		bool isChecked = (state == Qt::Checked);
		m_settings.setValue("isIoctlCommand", isChecked);
		if(isChecked) {
			m_rootToggle->setChecked(false);
			m_shellToggle->setChecked(false);
		}
	});
	btnLayout->addWidget(m_ioctlToggle);
// Checkbox root
    m_rootToggle = new QCheckBox("root");
    m_rootToggle->setChecked(m_isRootShell);
    connect(m_rootToggle, &QCheckBox::checkStateChanged, this, [this](int state) {
        m_isRootShell = (state == Qt::Checked);
        m_settings.setValue("isRootShell", m_isRootShell);});
    btnLayout->addWidget(m_rootToggle);    
    m_shellToggle = new QCheckBox("shell");
    m_shellToggle->setChecked(m_settings.value("isShellCommand", false).toBool());
    connect(m_shellToggle, &QCheckBox::checkStateChanged, this, [this](int state) {
        m_settings.setValue("isShellCommand", (state == Qt::Checked));});
    btnLayout->addWidget(m_shellToggle);    
    btnLayout->addSpacing(20);
    m_runBtn = new QPushButton("Execute");
    m_runBtn->setStyleSheet("background-color: #3CB043;");
    connect(m_runBtn, &QPushButton::clicked, this, &MainWindow::runCommand);
    btnLayout->addWidget(m_runBtn);    
    m_stopBtn = new QPushButton("Stop Process");
    connect(m_stopBtn, &QPushButton::clicked, this, &MainWindow::stopCommand);
    btnLayout->addWidget(m_stopBtn);    
    m_clearBtn = new QPushButton("Clear Log");
    connect(m_clearBtn, &QPushButton::clicked, [this](){ m_log->clear(); m_detachedLogDialog->clear(); });
    btnLayout->addWidget(m_clearBtn);    
    m_saveBtn = new QPushButton("Save Log");
    connect(m_saveBtn, &QPushButton::clicked, [this](){
        QString defDir = "/usr/local/log";
        QDir d(defDir);
        if (!d.exists()) QDir().mkpath(defDir);
        QString fn = QFileDialog::getSaveFileName(this, "Save log", d.filePath("log.txt"));
        if (!fn.isEmpty()) {
            QFile f(fn);
            if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                f.write(m_log->toPlainText().toUtf8());
                f.close();}}});
    btnLayout->addWidget(m_saveBtn);
    mainLayout->addLayout(btnLayout);
    return w;}

void MainWindow::onSequenceStarted() {
    appendLog("--- [sequence runner]  STARTED ---", "#4CAF50");
    if (m_dockSequence) {
        m_dockSequence->setVisible(true);
        m_dockSequence->raise();}
    if (m_swipeBuilder) {
        m_swipeBuilder->setRunSequenceButtonEnabled(false);}}

void MainWindow::onSequenceFinished(bool success) {
    if (m_swipeBuilder) {m_swipeBuilder->setRunSequenceButtonEnabled(true);}
    statusBar()->showMessage(success ? tr("Sequence completed.") : tr("Sequence failed."), 3000);}

void MainWindow::onSequenceCommandExecuting(const QString &cmd, int index, int total) {
    appendLog(QString(">> sequencerunner [%1/%2]: %3").arg(index).arg(total).arg(cmd), "#00BCD4");}

void MainWindow::handleSequenceLog(const QString &text, const QString &color) {appendLog(text, color);}

void MainWindow::onScheduleButtonClicked() {
    const QString cmdText = m_commandEdit->text().trimmed();
    const int interval = m_intervalSpinBox->value();
    const bool periodic = m_intervalToggle->isChecked();    
    if (cmdText.isEmpty()) {
        QMessageBox::warning(this, "Schedule Error", "Command cannot be empty.");
        return;}
    bool safeMode = m_settings.value("safeMode", false).toBool();
    if (safeMode && isDestructiveCommand(cmdText)) {
        QMessageBox::warning(this, "Safe mode", "Application is in Safe Mode. Destructive commands are blocked from scheduling.");
        return;}
    if (m_commandTimer->isActive()) {
        m_commandTimer->stop();
        appendLog("Previous scheduled command canceled.", "#FFAA66");}
    m_scheduledCommand = cmdText;
    m_commandTimer->setSingleShot(!periodic);
    m_commandTimer->setInterval(interval * 1000);    
    if (periodic) {
        appendLog(QString("Scheduled periodic command: %1 (every %2 s)").arg(cmdText).arg(interval), "#4CAF50");
    } else {
        appendLog(QString("Scheduled one-shot command: %1 (in %2 s)").arg(cmdText).arg(interval), "#00BCD4");}
    m_commandTimer->start();}

void MainWindow::executeScheduledCommand() {
    const QString cmdToRun = m_scheduledCommand;
    if (m_commandTimer->isSingleShot()) {
        m_commandTimer->stop();}
    m_commandEdit->setText(cmdToRun);
    runCommand();}

void MainWindow::loadCommands() {
    m_commands.clear();
    std::ifstream ifs(m_jsonFile.toStdString());
    if (!ifs.is_open()) {
        QStringList cats = {"System", "shell", "config"};
        for (const QString &c: cats) m_commands.insert(c, {});
        saveCommands();
        return;}
    try {
        nlohmann::json j;
        ifs >> j;
        for (auto it = j.begin(); it != j.end(); ++it) {
            const QString category = QString::fromStdString(it.key());
            QVector<SystemCmd> vec;
            if (it.value().is_array()) {
                for (const auto& cmd_obj : it.value()) {
                    if (cmd_obj.is_object() && cmd_obj.contains("command") && cmd_obj.contains("description")) {
                        SystemCmd c;
                        c.command = QString::fromStdString(cmd_obj.at("command").get<std::string>());
                        c.description = QString::fromStdString(cmd_obj.at("description").get<std::string>());
                        vec.append(c);}}}
            m_commands.insert(category, vec);}
    } catch (const nlohmann::json::exception& e) {
        QMessageBox::warning(this, "Error JSON", QString("Cannot parse JSON file: %1\nError: %2").arg(m_jsonFile).arg(e.what()));
        QStringList cats = { "System", "shell", "config" };
        for (const QString &c: cats) m_commands.insert(c, {});
        return;}}

void MainWindow::saveCommands() {
    nlohmann::json j_root = nlohmann::json::object();
    for (auto it = m_commands.begin(); it != m_commands.end(); ++it) {
        nlohmann::json j_array = nlohmann::json::array();
        for (const SystemCmd &c: it.value()) {
            j_array.push_back({
                {"command", c.command.toStdString()},
                {"description", c.description.toStdString()}});}
        j_root[it.key().toStdString()] = j_array;}
    QFileInfo fileInfo(m_jsonFile);
    QDir dir;
    if (!dir.mkpath(fileInfo.absolutePath())) {
        QMessageBox::critical(this, "Error Save JSON", QString("Cannot create directory: %1").arg(fileInfo.absolutePath()));
        return;}
    try {
        std::ofstream ofs(m_jsonFile.toStdString());
        if (ofs.is_open()) {
            ofs << j_root.dump(4);
            ofs.close();
        } else {
            QMessageBox::critical(this, "Error Save JSON", QString("Cannot write JSON: %1").arg(m_jsonFile));}
    } catch (const std::exception& e) {
        QMessageBox::warning(this, "Error JSON Save", QString("Cannot save JSON file: %1").arg(e.what()));}}

void MainWindow::populateCategoryList() {
    m_categoryList->clear();
    for (auto it = m_commands.constBegin(); it != m_commands.constEnd(); ++it) m_categoryList->addItem(it.key());}

void MainWindow::populateCommandList(const QString &category) {
    m_commandModel->removeRows(0, m_commandModel->rowCount());
    auto vec = m_commands.value(category);
    for (const SystemCmd &c: vec) {
        QList<QStandardItem*> row;
        row << new QStandardItem(c.command) << new QStandardItem(c.description);
        m_commandModel->appendRow(row);}
    m_commandView->resizeColumnToContents(1);}

void MainWindow::onCategoryChanged(QListWidgetItem *current, QListWidgetItem *) {
    if (!current) return;
    populateCommandList(current->text());
    m_commandEdit->clear();
    m_inputHistoryIndex = -1;}

void MainWindow::onCommandSelected(const QModelIndex &current, const QModelIndex &) {
    if (!current.isValid()) return;
    QModelIndex s = m_commandProxy->mapToSource(current);
    if (s.isValid()) {
        QString cmd = m_commandModel->item(s.row(), 0)->text();
        m_commandEdit->setText(cmd);
        if (m_dockControls) { 
            m_dockControls->setVisible(true); m_dockControls->raise(); 
            if (m_commandEdit) { m_commandEdit->setFocus(); m_commandEdit->selectAll(); } }}}

void MainWindow::onCommandDoubleClicked(const QModelIndex &index) {
    if (!index.isValid()) return;
    QModelIndex s = m_commandProxy->mapToSource(index);
    if (s.isValid()) {
        QString cmd = m_commandModel->item(s.row(), 0)->text();
        m_commandEdit->setText(cmd);
        runCommand();}}


void MainWindow::runCommand() {
    const QString cmdText = m_commandEdit->text().trimmed();
    if (cmdText.isEmpty()) return;
// Logika bezpieczeństwa (Safe Mode)
    bool safeMode = m_settings.value("safeMode", false).toBool();
    if (safeMode && isDestructiveCommand(cmdText)) {
        QMessageBox::warning(this, "Safe mode", "Application is in Safe Mode. Destructive commands are blocked.");
        return;}
    if (isDestructiveCommand(cmdText)) {
        auto reply = QMessageBox::question(this, "Confirm", 
            QString("Command looks destructive:\n%1\nContinue?").arg(cmdText), 
            QMessageBox::Yes | QMessageBox::No);
        if (reply != QMessageBox::Yes) return;}
// Zarządzanie historią komend
    if (m_inputHistory.isEmpty() || m_inputHistory.last() != cmdText) {
        m_inputHistory.append(cmdText);}
    m_inputHistoryIndex = -1;    
    QString mode = "adb";
    QString logColor = "#FFE066";
    QString logPrefix = ">>> adb: ";
    if (m_ioctlToggle && m_ioctlToggle->isChecked()) {
        mode = "ioctl";
        logColor = "#BB86FC";
        logPrefix = ">>> 💉ioctl: ";
    } else if (m_rootToggle->isChecked()) {
        mode = "root";
        logColor = "#FF0000";
        logPrefix = ">>> root: ";
    } else if (m_shellToggle->isChecked()) {
        mode = "shell";
        logColor = "#00BCD4";
        logPrefix = ">>> shell: ";
    }
    appendLog(logPrefix + cmdText, logColor);
    m_executor->executeSequenceCommand(cmdText, mode);
}

void MainWindow::stopCommand() {if (m_executor) {m_executor->stop();appendLog("Process stopped by user (adb terminated).", "#FFAA66");}}

void MainWindow::onOutput(const QString &text) {
    const QStringList lines = text.split('\n');
    for (const QString &l : lines) {
        if (!l.trimmed().isEmpty()) {
            appendLog(l.trimmed(), "#A9FFAC");}}}

void MainWindow::onError(const QString &text) {
    const QStringList lines = text.split('\n');
    for (const QString &l : lines) {
        if (!l.trimmed().isEmpty()) {
            appendLog(QString("!!! %1").arg(l.trimmed()), "#FF6565");}}
    logErrorToFile(text);}

void MainWindow::onProcessStarted() { appendLog("adb command started.", "#8ECAE6"); }

void MainWindow::addCommand() {
    bool ok;
    QString cmd = QInputDialog::getText(this, "Add command", "Command:", QLineEdit::Normal, "", &ok);
    if (!ok || cmd.isEmpty()) return;
    QString desc = QInputDialog::getText(this, "Add command", "Description:", QLineEdit::Normal, "", &ok);
    if (!ok) return;
    QString category = m_categoryList->currentItem() ? m_categoryList->currentItem()->text() : QString();
    if (category.isEmpty()) { QMessageBox::warning(this, "No category", "Select a category first."); return; }
    m_commands[category].append({cmd, desc});
    populateCommandList(category);
    saveCommands();}

void MainWindow::editCommand() {
    QModelIndex idx = m_commandView->currentIndex();
    if (!idx.isValid()) return;
    QModelIndex s = m_commandProxy->mapToSource(idx);
    if (!s.isValid()) return;
    QString cmd = m_commandModel->item(s.row(), 0)->text();
    QString desc = m_commandModel->item(s.row(), 1)->text();
    bool ok;
    QString ncmd = QInputDialog::getText(this, "Edit command", "command:", QLineEdit::Normal, cmd, &ok);
    if (!ok) return;
    QString ndesc = QInputDialog::getText(this, "Edit command", "description:", QLineEdit::Normal, desc, &ok);
    if (!ok) return;
    QString category = m_categoryList->currentItem() ? m_categoryList->currentItem()->text() : QString();
    if (category.isEmpty()) return;
    auto &vec = m_commands[category];
    for (int i=0; i<vec.size(); ++i) {
        if (vec[i].command == cmd && vec[i].description == desc) { 
            vec[i].command = ncmd; 
            vec[i].description = ndesc; 
            break; }}
    populateCommandList(category);
    saveCommands();}

void MainWindow::removeCommand() {
    QModelIndex idx = m_commandView->currentIndex();
    if (!idx.isValid()) return;
    QModelIndex s = m_commandProxy->mapToSource(idx);
    if (!s.isValid()) return;
    QString cmd = m_commandModel->item(s.row(), 0)->text();
    QString category = m_categoryList->currentItem() ? m_categoryList->currentItem()->text() : QString();
    if (category.isEmpty()) return;
    auto &vec = m_commands[category];
    for (int i=0; i<vec.size(); ++i) {
        if (vec[i].command == cmd) { vec.removeAt(i); break; }}
    populateCommandList(category);
    saveCommands();}

void MainWindow::onProcessFinished(int exitCode, QProcess::ExitStatus) {
    appendLog(QString("adb process finished. Exit code: %1").arg(exitCode), "#BDBDBD");
    if (exitCode != 0) appendLog(QString("Command finished with error code: %1").arg(exitCode), "#FF6565");}

void MainWindow::appendLog(const QString &text, const QString &color) {
    QString line = text;
    if (!color.isEmpty()) m_log->setTextColor(QColor(color)); else m_log->setTextColor(QColor("#F0F0F0"));
    m_log->append(line);
    m_log->setTextColor(QColor("#F0F0F0"));}

// /usr/local/log/adb_shell.log
void MainWindow::logErrorToFile(const QString &text) {
    const QString logDir = "/usr/local/log";
    QDir d(logDir);
    if (!d.exists()) QDir().mkpath(logDir);
    QString logFile = d.filePath("adb_shell.log");
    QFile f(logFile);
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&f);
        out << QDateTime::currentDateTime().toString(Qt::ISODate) << " - " << text << "\n";
        f.close();}}

bool MainWindow::isDestructiveCommand(const QString &cmd) {
    QString c = cmd.toLower();
    return c.contains("rm ") || c.contains("wipe") || c.contains("format") || c.contains("dd ") || c.contains("flashall");}

void MainWindow::navigateHistory(int direction) {
    if (m_inputHistory.isEmpty()) {return;}
    if (m_inputHistoryIndex == -1) {
    if (direction == -1) {m_inputHistoryIndex = m_inputHistory.size() - 1;} else {return;}} else {m_inputHistoryIndex += direction;
    if (m_inputHistoryIndex < 0) {m_inputHistoryIndex = 0;return;}
    if (m_inputHistoryIndex >= m_inputHistory.size()) {m_commandEdit->clear();m_inputHistoryIndex = -1;return;}}
    m_commandEdit->setText(m_inputHistory.at(m_inputHistoryIndex));
    m_commandEdit->selectAll();}

void MainWindow::restoreDefaultLayout() {
    m_settings.remove("geometry");
    m_settings.remove("windowState");
    addDockWidget(Qt::TopDockWidgetArea, m_dockControls);
    addDockWidget(Qt::TopDockWidgetArea, m_dockSequence);
    splitDockWidget(m_dockControls, m_dockSequence, Qt::Horizontal);
    addDockWidget(Qt::LeftDockWidgetArea, m_dockCategories);
    addDockWidget(Qt::LeftDockWidgetArea, m_dockCommands);
    splitDockWidget(m_dockCategories, m_dockCommands, Qt::Horizontal);
    addDockWidget(Qt::BottomDockWidgetArea, m_dockLog);
    saveWindowStateToSettings();}

void MainWindow::showSettingsDialog() {
    SettingsDialog dlg(this);
    dlg.setSafeMode(m_settings.value("safeMode", false).toBool());
    if (dlg.exec() == QDialog::Accepted) {
        m_settings.setValue("safeMode", dlg.safeMode());}}

void MainWindow::restoreWindowStateFromSettings() {
    if (m_settings.contains("geometry")) restoreGeometry(m_settings.value("geometry").toByteArray());
    if (m_settings.contains("windowState")) restoreState(m_settings.value("windowState").toByteArray());
    m_isRootShell = m_settings.value("isRootShell", false).toBool();
    if (m_rootToggle) m_rootToggle->setChecked(m_isRootShell);}

void MainWindow::saveWindowStateToSettings() {
    m_settings.setValue("geometry", saveGeometry());
    m_settings.setValue("windowState", saveState());
    m_settings.setValue("isRootShell", m_isRootShell);}

QModelIndex MainWindow::currentCommandModelIndex() const {
    QModelIndex idx = m_commandView->currentIndex();
    if (!idx.isValid()) return QModelIndex();
    return m_commandProxy->mapToSource(idx);}

bool MainWindow::eventFilter(QObject *obj, QEvent *event) {
    if ((obj == m_commandView || obj == m_categoryList || obj == m_commandEdit) && event->type() == QEvent::KeyPress) {
        QKeyEvent *ke = static_cast<QKeyEvent*>(event);
        if (obj == m_commandEdit) {
            if (ke->key() == Qt::Key_Up) {
                navigateHistory(-1);
                return true;}
            if (ke->key() == Qt::Key_Down) {
                navigateHistory(1);
                return true;}}
        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
            if (obj == m_categoryList) {
                if (m_commandModel->rowCount() > 0) {
                    QModelIndex firstIndex = m_commandView->model()->index(0, 0);
                    if (firstIndex.isValid()) {
                        m_commandView->setCurrentIndex(firstIndex);
                        m_commandView->setFocus();
                        return true;}}
                if (m_commandEdit) {
                    m_commandEdit->setFocus();
                    return true;}}
            if (obj == m_commandView) {
                onCommandDoubleClicked(m_commandView->currentIndex());
                return true;}
            if (obj == m_commandEdit) {
                return false;}}
        return false;}
    return QMainWindow::eventFilter(obj, event);}

void MainWindow::updateTimerDisplay() {
    if (m_commandTimer && m_commandTimer->isActive()) {
        int remaining = m_commandTimer->remainingTime();
        if (remaining < 0) remaining = 0;
        int seconds = (remaining + 999) / 1000;
        if (m_commandTimerLabel) {
            m_commandTimerLabel->setText(QString("(%1s)").arg(seconds));}
    } else {
        if (m_commandTimerLabel) {
            m_commandTimerLabel->setText("(0s)");}}}

void MainWindow::startIntervalSequence() {appendLog("--- interval sequence reset ---", "#4CAF50");m_sequenceRunner->startSequence();}

void MainWindow::closeEvent(QCloseEvent *event) {saveWindowStateToSettings();QMainWindow::closeEvent(event);}

void MainWindow::refreshDeviceList() {
    m_deviceCombo->clear();
    m_deviceCombo->addItem("Searching...");
    QProcess *p = new QProcess(this);
    connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, p](int exitCode, QProcess::ExitStatus) {
        m_deviceCombo->clear();
        if (exitCode == 0) {
            QString output = p->readAllStandardOutput();
            QStringList lines = output.split('\n', Qt::SkipEmptyParts);
            for (const QString &line : lines) {
                if (line.startsWith("List of")) continue;
                if (line.trimmed().isEmpty()) continue;
                QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                if (parts.size() >= 2) {
                    QString serial = parts[0];
                    QString status = parts[1];
                    m_deviceCombo->addItem(QString("%1 (%2)").arg(serial, status), serial);}}}
        if (m_deviceCombo->count() == 0) {
            m_deviceCombo->addItem("No devices found");
        } else {
            onDeviceSelected(0);}
        p->deleteLater();});
    p->start(m_executor->adbPath(), QStringList() << "devices");}

void MainWindow::onDeviceSelected(int index) {
    if (index < 0 || index >= m_deviceCombo->count()) return;
    QString serial = m_deviceCombo->itemData(index).toString();
    if (!serial.isEmpty()) {
        m_executor->setTargetDevice(serial);
        appendLog(QString("Target device set to: %1").arg(serial), "#2196F3");
        if (m_swipeBuilder) {
            m_swipeBuilder->setAdbPath(m_executor->adbPath());}
    } else {
        m_executor->setTargetDevice("");}}
