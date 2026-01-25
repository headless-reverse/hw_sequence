#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "systemcmd.h"
#include "view_swipe_builder.h"
#include "video_client.h"
#include "view_hardware_grabbed.h"
#include "KeyboardWidget.h"
#include <QMainWindow>
#include <QMap>
#include <QVector>
#include <QString>
#include <QJsonObject>
#include <QProcess>
#include <QSettings>
#include <QTimer>
#include <QElapsedTimer>
#include <QDir>
#include <QComboBox>
#include <QStandardItemModel>
#include <QSortFilterProxyModel>

class SequenceRunner;
class ViewSequencerunner;
class QListWidget;
class QListWidgetItem;
class QLineEdit;
class QTextEdit;
class QDockWidget;
class QTreeView;
class QStandardItemModel;
class QSortFilterProxyModel;
class QPushButton;
class CommandExecutor;
class QAction;
class LogDialog;
class SequenceRunner;
class QSpinBox;
class QCheckBox;
class QModelIndex;
class QHBoxLayout;
class QLabel;
class QDragEnterEvent;
class QDropEvent;
class KeyboardWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr, 
                        const QString &adbPath = QString(), 
                        const QString &targetSerial = QString());
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void onCategoryChanged(QListWidgetItem *current, QListWidgetItem *previous);
    void onCommandSelected(const QModelIndex &current, const QModelIndex &previous);
    void onCommandDoubleClicked(const QModelIndex &index);
    void runCommand();
    void stopCommand();
    void addCommand();
    void editCommand();
    void removeCommand();
    void saveCommands();
    void loadCommands();
    void onOutput(const QString &text);
    void onError(const QString &text);
    void onProcessStarted();
	void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
	void onCommandFinished(int exitCode);
	void restoreDefaultLayout();
	void showSettingsDialog();
	void navigateHistory(int direction);    
    void onScheduleButtonClicked();
    void executeScheduledCommand();
	void updateTimerDisplay();
	void onCountdownTick();
    void startIntervalSequence();
    void onSequenceStarted();
    void onSequenceFinished(bool success);
    void onSequenceCommandExecuting(const QString &cmd, int index, int total);
    void handleSequenceLog(const QString &text, const QString &color);    
    void refreshDeviceList(); 
    void onDeviceSelected(int index);

private:
    QDockWidget *m_dockBuilder = nullptr;
    ViewSwipeBuilder *m_swipeBuilder = nullptr;
    QAction *m_viewBuilderAct = nullptr;
    QDockWidget *m_dockCategories = nullptr;
    QDockWidget *m_dockCommands = nullptr;
    QDockWidget *m_dockLog = nullptr;
    QDockWidget *m_dockControls = nullptr;
    QDockWidget *m_dockSequence = nullptr;
    QListWidget *m_categoryList = nullptr;
    QTreeView *m_commandView = nullptr;
    QStandardItemModel *m_commandModel = nullptr;
    QSortFilterProxyModel *m_commandProxy = nullptr;
    QLineEdit *m_commandEdit = nullptr;
	QTextEdit *m_log = nullptr;
	
    QPushButton *m_runBtn = nullptr;
    QPushButton *m_stopBtn = nullptr;
    QPushButton *m_clearBtn = nullptr;
    QPushButton *m_saveBtn = nullptr;
    QPushButton *m_scheduleBtn = nullptr;
    QComboBox *m_deviceCombo = nullptr;
    QPushButton *m_refreshDevicesBtn = nullptr;
    QMap<QString, QVector<SystemCmd>> m_commands;
    CommandExecutor *m_executor = nullptr;
    VideoClient *m_videoClient = nullptr;
    QString m_adbPath = QStringLiteral("adb");
    QString m_jsonFile = QStringLiteral("adb_commands.json");
    QStringList m_inputHistory;
    int m_inputHistoryIndex = -1;    
    QCheckBox *m_shellToggle = nullptr;
	QCheckBox *m_rootToggle = nullptr;
	QCheckBox *m_ioctlToggle = nullptr;
	QCheckBox *m_measureTimeControlsCheck = nullptr;
    bool m_isRootShell = false;
	QTimer *m_commandTimer = nullptr;
	QElapsedTimer m_commandExecutionTimer;
	qint64 m_remainingMs = 0;
    QLabel *m_commandTimerLabel = nullptr;
    QSpinBox *m_intervalSpinBox = nullptr;
    QCheckBox *m_intervalToggle = nullptr;
    QString m_scheduledCommand;

    QVector<QString> m_sequenceQueue;
	QTimer *m_displayTimer = nullptr;
    QAction *m_addCommandAct = nullptr;
    QAction *m_editCommandAct = nullptr;
    QAction *m_removeCommandAct = nullptr;
    QAction *m_viewCategoriesAct = nullptr;
    QAction *m_viewCommandsAct = nullptr;
    QAction *m_viewLogAct = nullptr;
    QAction *m_viewControlsAct = nullptr;
    QAction *m_viewSequenceAct = nullptr;

	QTimer *m_sequenceIntervalTimer = nullptr;
	QCheckBox *m_sequenceIntervalToggle = nullptr;
	QSpinBox *m_sequenceIntervalSpinBox = nullptr;
	QLabel *m_sequenceIntervalLabel = nullptr;
	
    QSettings m_settings{"AdbShell", "adb_shell"};
    LogDialog *m_detachedLogDialog = nullptr;

    void populateCategoryList();
    void populateCommandList(const QString &category);
    void appendLog(const QString &text, const QString &color = QString());
    void logErrorToFile(const QString &text);
    bool isDestructiveCommand(const QString &cmd);
    QWidget* createControlsWidget();
    QWidget* createSequenceWidget();
    void setupSequenceDock();
    void ensureJsonPathLocal();
    void setupMenus();
    void restoreWindowStateFromSettings();
    void saveWindowStateToSettings();
	QModelIndex currentCommandModelIndex() const;
	ViewSequencerunner *m_viewSequencerunner = nullptr;
	SequenceRunner *m_sequenceRunner = nullptr;
	
	ViewHardwareGrabbed *m_hardwareGrabbedWidget;
	QDockWidget *m_dockHardwareGrabbed;
	QAction *m_viewHardwareGrabbedAct;
	
	KeyboardWidget *m_keyboardWidget = nullptr;
	QDockWidget *m_dockKeyboard = nullptr;
	QAction *m_viewKeyboardAct = nullptr;

};

#endif // MAINWINDOW_H
