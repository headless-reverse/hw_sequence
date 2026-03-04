#include "hw_keyboard.h"
#include "hw_keyboard_map.h"

HardwareKeyboard::HardwareKeyboard(HardwareGrabbed *logic, QWidget *parent) 
    : QWidget(parent), m_logic(logic) {
    m_layout = new QGridLayout(this);
    m_layout->setSpacing(2);
    m_layout->setContentsMargins(2, 2, 2, 2);
    setupQwerty();}

void HardwareKeyboard::addKey(const QString &label, uint16_t scanCode, int r, int c, int colSpan) {
    QPushButton *btn = new QPushButton(label);
    btn->setMinimumSize(30, 35);
    btn->setFocusPolicy(Qt::NoFocus);
    connect(btn, &QPushButton::pressed, this, [this, scanCode]() {
        if (m_logic && m_logic->isConnected()) {
            m_logic->sendKey(scanCode, true);
        }
        emit keyTriggered(static_cast<int>(scanCode));
    });
    connect(btn, &QPushButton::released, this, [this, scanCode]() {
        if (m_logic && m_logic->isConnected()) {
            m_logic->sendKey(scanCode, false);
        }
    });
    m_layout->addWidget(btn, r, c, 1, colSpan);
}

void HardwareKeyboard::setupQwerty() {
    int r = 0;
// ===== ROW 0 =====
    addKey("F1", LinuxKeys::KEY_F1, r, 0);
    addKey("F2", LinuxKeys::KEY_F2, r, 1);
    addKey("1",  LinuxKeys::KEY_1,  r, 2);
    addKey("2",  LinuxKeys::KEY_2,  r, 3);
    addKey("3",  LinuxKeys::KEY_3,  r, 4);
    addKey("4",  LinuxKeys::KEY_4,  r, 5);
    addKey("5",  LinuxKeys::KEY_5,  r, 6);
    addKey("6",  LinuxKeys::KEY_6,  r, 7);
    addKey("7",  LinuxKeys::KEY_7,  r, 8);
    addKey("8",  LinuxKeys::KEY_8,  r, 9);
    addKey("9",  LinuxKeys::KEY_9,  r, 10);
    addKey("0",  LinuxKeys::KEY_0,  r, 11);
// ===== ROW 1 =====
    r++;
    addKey("Q", LinuxKeys::KEY_Q, r, 0);
    addKey("W", LinuxKeys::KEY_W, r, 1);
    addKey("E", LinuxKeys::KEY_E, r, 2);
    addKey("R", LinuxKeys::KEY_R, r, 3);
    addKey("T", LinuxKeys::KEY_T, r, 4);
    addKey("Y", LinuxKeys::KEY_Y, r, 5);
    addKey("U", LinuxKeys::KEY_U, r, 6);
    addKey("I", LinuxKeys::KEY_I, r, 7);
    addKey("O", LinuxKeys::KEY_O, r, 8);
    addKey("P", LinuxKeys::KEY_P, r, 9);
    addKey("V+", LinuxKeys::KEY_VOLUMEUP, r, 10);
    addKey("⌫", LinuxKeys::KEY_BACKSPACE, r, 11);
// ===== ROW 2 =====
    r++;
    addKey("A", LinuxKeys::KEY_A, r, 0);
    addKey("S", LinuxKeys::KEY_S, r, 1);
    addKey("D", LinuxKeys::KEY_D, r, 2);
    addKey("F", LinuxKeys::KEY_F, r, 3);
    addKey("G", LinuxKeys::KEY_G, r, 4);
    addKey("H", LinuxKeys::KEY_H, r, 5);
    addKey("J", LinuxKeys::KEY_J, r, 6);
    addKey("K", LinuxKeys::KEY_K, r, 7);
    addKey("L", LinuxKeys::KEY_L, r, 8);
    addKey("V-", LinuxKeys::KEY_VOLUMEDOWN, r, 10);
    addKey("Ent", LinuxKeys::KEY_ENTER, r, 11);
// ===== ROW 3 =====
    r++;
    addKey("⇧", LinuxKeys::KEY_LEFTSHIFT, r, 0);
    addKey("Z", LinuxKeys::KEY_Z, r, 1);
    addKey("X", LinuxKeys::KEY_X, r, 2);
    addKey("C", LinuxKeys::KEY_C, r, 3);
    addKey("V", LinuxKeys::KEY_V, r, 4);
    addKey("B", LinuxKeys::KEY_B, r, 5);
    addKey("N", LinuxKeys::KEY_N, r, 6);
    addKey("M", LinuxKeys::KEY_M, r, 7);
    addKey(",", LinuxKeys::KEY_COMMA, r, 8);
    addKey(".", LinuxKeys::KEY_DOT, r, 9);
    addKey("↑", LinuxKeys::KEY_UP, r, 10);
// ===== ROW 4 =====
    r++;
    addKey("Ctrl", LinuxKeys::KEY_LEFTCTRL, r, 0);
    addKey("Alt",  LinuxKeys::KEY_RIGHTALT, r, 1);
    addKey("␣ Space", LinuxKeys::KEY_SPACE, r, 2, 4); 
    addKey("HOME", LinuxKeys::KEY_HOME, r, 6);
    addKey("BACK", LinuxKeys::KEY_BACK, r, 7);
    addKey("⚡",  LinuxKeys::KEY_POWER, r, 8);
    addKey("←", LinuxKeys::KEY_LEFT, r, 9);
    addKey("↓", LinuxKeys::KEY_DOWN, r, 10);
    addKey("→", LinuxKeys::KEY_RIGHT, r, 11);
}

void HardwareKeyboard::onKeyPressed() {
    uint16_t code = sender()->property("scanCode").value<uint16_t>();
	emit keyTriggered(static_cast<int>(code));}

void HardwareKeyboard::onSpecialKeyPressed() {
    if (!m_logic || !m_logic->isConnected()) return;
    QWidget *w = qobject_cast<QWidget*>(sender());
    if (!w) return;
    uint16_t code = w->property("scanCode").value<uint16_t>();
    m_logic->sendKey(code);}
