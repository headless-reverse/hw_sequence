#ifndef HW_KEYBOARD_H
#define HW_KEYBOARD_H

#include <QWidget>
#include <QGridLayout>
#include <QPushButton>
#include "hardware_grabbed.h"
#include "hw_keyboard_map.h"

class HardwareKeyboard : public QWidget {
    Q_OBJECT
public:
    explicit HardwareKeyboard(HardwareGrabbed *logic, QWidget *parent = nullptr);

signals:
    void keyTriggered(int linuxCode);

private slots:
    void onKeyPressed();
    void onSpecialKeyPressed();

private:
    HardwareGrabbed *m_logic;
    QGridLayout *m_layout;
    void addKey(const QString &label, uint16_t scanCode, int r, int c, int colSpan = 1);
    void setupQwerty();
};

#endif
