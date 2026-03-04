#pragma once
#include <QObject>
#include <QVector>
#include <QJsonArray>
#include <QJsonObject>

struct SwipeAction {
    enum Type {
        Tap,
        Swipe,
        LongPress,
        Text,
        Command,
        Key,
        Wait,
        AdbWifi,
        MacroRec,
        MacroStop,
        MacroPlay,
        GrabOn,
        GrabOff
    };

    Type type;
    int x1, y1;
    int x2, y2;
    int duration;
    int delayAfterMs;
    QString command;
    QString runMode;

    SwipeAction(Type t = Command,
                int x1 = 0, int y1 = 0,
                int x2 = 0, int y2 = 0,
                int duration = 0,
                int delayAfterMs = 0,
                const QString &cmd = QString(),
                const QString &mode = "shell")
        : type(t),
          x1(x1), y1(y1),
          x2(x2), y2(y2),
          duration(duration),
          delayAfterMs(delayAfterMs),
          command(cmd),
          runMode(mode) {}
};

class SwipeModel : public QObject {
    Q_OBJECT
public:
    explicit SwipeModel(QObject *parent = nullptr);

    void addTap(int x, int y, int delayMs = 30, const QString &mode = "shell");
    void addSwipe(int x1, int y1, int x2, int y2, int duration, int delayMs = 70, const QString &mode = "shell");
    void addLongPress(int x, int y, int durationMs, int delayMs = 70, const QString &mode = "shell");
	void addText(const QString &text, int delayMs = 30);
	void addText(const QString &text, int delayMs = 30, const QString &mode = "shell");
    void addCommand(const QString &command, int delayMs = 50, const QString &runMode = "shell");
    void addKey(const QString &keyCommand, int delayMs = 1, const QString &mode = "shell");
    void addWait(int delayMs);
    void addAdbWifi();
    void addGrabControl(bool enable);
    void addMacroControl(int actionType);
    void editActionAt(int index, const SwipeAction &action);
    void moveAction(int from, int to);
    void removeActionAt(int index);
    void clear();

    SwipeAction actionAt(int index) const;
    int count() const { return m_actions.size(); }
    const QVector<SwipeAction>& actions() const { return m_actions; }

    QJsonArray toJsonArray() const;
    QJsonArray toJsonSequence() const;
    void fromJsonArray(const QJsonArray &array);

signals:
    void modelChanged();

private:
    QVector<SwipeAction> m_actions;
    QJsonObject actionToJson(const SwipeAction &action) const;
};
