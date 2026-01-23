#ifndef HW_KEYBOARD_MAP_H
#define HW_KEYBOARD_MAP_H

#include <stdint.h>
#include <map>
#include <QKeyEvent>

namespace LinuxKeys {
    enum ScanCode : uint16_t {
        KEY_RESERVED = 0,
        KEY_ESC = 1,
        KEY_1 = 2, KEY_2 = 3, KEY_3 = 4, KEY_4 = 5, KEY_5 = 6, 
        KEY_6 = 7, KEY_7 = 8, KEY_8 = 9, KEY_9 = 10, KEY_0 = 11,
        KEY_MINUS = 12, KEY_EQUAL = 13, KEY_BACKSPACE = 14,
        KEY_TAB = 15, KEY_Q = 16, KEY_W = 17, KEY_E = 18, KEY_R = 19,
        KEY_T = 20, KEY_Y = 21, KEY_U = 22, KEY_I = 23, KEY_O = 24, KEY_P = 25,
        KEY_LEFTBRACE = 26, KEY_RIGHTBRACE = 27,
        KEY_ENTER = 28, KEY_LEFTCTRL = 29, 
        KEY_A = 30, KEY_S = 31, KEY_D = 32, KEY_F = 33, KEY_G = 34, 
        KEY_H = 35, KEY_J = 36, KEY_K = 37, KEY_L = 38, KEY_SEMICOLON = 39,
        KEY_APOSTROPHE = 40, KEY_GRAVE = 41, 
        KEY_LEFTSHIFT = 42, KEY_BACKSLASH = 43, 
        KEY_Z = 44, KEY_X = 45, KEY_C = 46, KEY_V = 47, KEY_B = 48, 
        KEY_N = 49, KEY_M = 50, KEY_COMMA = 51, KEY_DOT = 52, KEY_SLASH = 53,
        KEY_RIGHTSHIFT = 54, KEY_KPASTERISK = 55, KEY_LEFTALT = 56, KEY_SPACE = 57,
        KEY_CAPSLOCK = 58,
// Funkcyjne
        KEY_F1 = 59, KEY_F2 = 60, KEY_F3 = 61, KEY_F4 = 62, KEY_F5 = 63,
        KEY_F6 = 64, KEY_F7 = 65, KEY_F8 = 66, KEY_F9 = 67, KEY_F10 = 68,
// Specjalne i Nawigacja
        KEY_RIGHTALT = 100,
        KEY_HOME = 102,
        KEY_UP = 103,
        KEY_PAGEUP = 104,
        KEY_LEFT = 105,
        KEY_RIGHT = 106,
        KEY_END = 107,
        KEY_DOWN = 108,
        KEY_PAGEDOWN = 109,
        KEY_INSERT = 110,
        KEY_DELETE = 111,
        KEY_MUTE = 113,
        KEY_VOLUMEDOWN = 114,
        KEY_VOLUMEUP = 115,
        KEY_POWER = 116,
        KEY_MENU = 139,
        KEY_BACK = 158,
        KEY_HOMEPAGE = 172
    };
    inline uint16_t fromQtKey(int qtKey) {
// Mapa dla klawiszy nietekstowych
        static const std::map<int, uint16_t> specialMap = {
            {Qt::Key_Return,    KEY_ENTER},
            {Qt::Key_Enter,     KEY_ENTER},
            {Qt::Key_Escape,    KEY_ESC},
            {Qt::Key_Backspace, KEY_BACKSPACE},
            {Qt::Key_Tab,       KEY_TAB},
            {Qt::Key_Space,     KEY_SPACE},
            {Qt::Key_Control,   KEY_LEFTCTRL},
            {Qt::Key_Alt,       KEY_LEFTALT},
            {Qt::Key_AltGr,     KEY_RIGHTALT},
            {Qt::Key_Shift,     KEY_LEFTSHIFT},
            {Qt::Key_CapsLock,  KEY_CAPSLOCK},
            {Qt::Key_Up,        KEY_UP},
            {Qt::Key_Down,      KEY_DOWN},
            {Qt::Key_Left,      KEY_LEFT},
            {Qt::Key_Right,     KEY_RIGHT},
            {Qt::Key_Home,      KEY_HOME},
            {Qt::Key_End,       KEY_END},
            {Qt::Key_PageUp,    KEY_PAGEUP},
            {Qt::Key_PageDown,  KEY_PAGEDOWN},
            {Qt::Key_Delete,    KEY_DELETE},
            {Qt::Key_Insert,    KEY_INSERT},
            {Qt::Key_F1,        KEY_F1},
            {Qt::Key_F10,       KEY_F10}
        };
        if (specialMap.count(qtKey)) return specialMap.at(qtKey);
// Automatyczne mapowanie A-Z (Linux kody 30, 48, 46...)
        if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z) {
            static const uint16_t alpha[] = { 
                30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38, 50, 49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44 
            };
            return alpha[qtKey - Qt::Key_A];
        }
// Automatyczne mapowanie 0-9
        if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9) {
            return (qtKey == Qt::Key_0) ? KEY_0 : (uint16_t)(qtKey - Qt::Key_1 + KEY_1);
        }
        return 0;
    }
}
#endif
