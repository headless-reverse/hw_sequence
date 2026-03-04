#ifndef RECORDACTION_H
#define RECORDACTION_H

#include <QDialog>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QComboBox>

class RecordAction : public QDialog {
    Q_OBJECT
public:
    explicit RecordAction(QWidget *parent = nullptr);
    ~RecordAction();

signals:
    void actionTriggered(const QString &actionName, const QString &runMode);

private slots:
    void onActionClicked();
    void updateHighlight();

private:
    QLineEdit *m_edit;
    QLabel *m_highlight;
    QPushButton *m_btn;
    QComboBox *m_runModeCombo;
};

#endif // RECORDACTION_H
