#pragma once
#include <QDialog>

class QLineEdit;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    ~SettingsDialog();

    void setAdbPath(const QString &p);
    QString adbPath() const;

private:
    QLineEdit *m_adbEdit = nullptr;
};
