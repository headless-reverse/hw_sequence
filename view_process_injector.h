#ifndef VIEW_PROCESS_INJECTOR_H
#define VIEW_PROCESS_INJECTOR_H

#include <QDockWidget>
#include <QTreeWidget>
#include <QPushButton>
#include <QComboBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTextEdit>
#include <QProgressBar>
#include <QSet>
#include <QApplication>
#include <QDateTime>

class ViewProcessInjector : public QDockWidget {
    Q_OBJECT

public:
    explicit ViewProcessInjector(QWidget *parent = nullptr);

    void addProcessItem(int pid, const QString &name);
    void clearProcessList();
    void setInjectButtonEnabled(bool enabled);
    void showStatus(const QString &msg, bool isError);
    void onListFinished();
    void displayMemMaps(int pid, const QString &maps);
    void updateProcessInjectionStatus(int pid, const QString &status);
    void onInjectionSuccess(int pid);
    void onInjectionFailed(int pid, const QString &error);
    void setConnected(bool connected);
    void addLogEntry(const QString &entry);

signals:
    void refreshRequested();
    void injectRequested(int pid);
    void killRequested(int pid);
    void memMapsRequested(int pid);
    void libraryPathChanged(const QString &newPath);
    void logRequested(const QString &msg);

private slots:
    void onRefreshClicked();
    void onStopClicked();
    void onInjectClicked();
    void onKillClicked();
    void onMapsRefreshClicked();
    void onCustomContextMenuRequested(const QPoint &pos);
    void onProcessSelectionChanged();
    void onBrowseLibPath();
    void onSetLibraryPath();

private:
    void setupUi();
    void setupConnections();
    void loadLibraryPathsFromConfig();
    QLabel              *m_statusLabel              = nullptr;
    QTreeWidget         *m_treeWidget               = nullptr;
    QTextEdit           *m_mapsDisplay              = nullptr;
    QTextEdit           *m_injectionLog             = nullptr;
    QProgressBar        *m_injectionProgress        = nullptr;

    QComboBox           *m_libPathCombo             = nullptr;

    QPushButton         *m_refreshBtn               = nullptr;
    QPushButton         *m_stopBtn                  = nullptr;
    QPushButton         *m_injectBtn                = nullptr;
    QPushButton         *m_killBtn                  = nullptr;
    QPushButton         *m_mapsRefreshBtn           = nullptr;

    bool                m_isRefreshing              = false;
    bool                m_injectionInProgress       = false;
    int                 m_selectedPid               = -1;
    
    QSet<QString>       m_savedLibPaths;
};

#endif // VIEW_PROCESS_INJECTOR_H
