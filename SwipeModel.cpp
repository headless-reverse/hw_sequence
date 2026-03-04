#include "SwipeModel.h"
//#include <QDebug>
#include <QJsonObject>
#include <QJsonArray>
//#include <algorithm>
//#include <QVector>

SwipeModel::SwipeModel(QObject *parent) : QObject(parent) {}

void SwipeModel::addTap(int x,int y,int delayMs,const QString &mode){
    m_actions.append(SwipeAction(SwipeAction::Tap, x, y, 0, 0, 0, delayMs, QString(), mode));
    emit modelChanged();}

void SwipeModel::addSwipe(int x1,int y1,int x2,int y2,int duration,int delayMs,const QString &mode){
    m_actions.append(SwipeAction(SwipeAction::Swipe,x1,y1,x2,y2,duration,delayMs,QString(),mode));
    emit modelChanged();}

void SwipeModel::addLongPress(int x,int y,int durationMs,int delayMs,const QString &mode){
    m_actions.append(SwipeAction(SwipeAction::LongPress,x,y,x,y,durationMs,delayMs,QString(),mode));
    emit modelChanged();}

void SwipeModel::addText(const QString &text,int delayMs,const QString &mode){
    m_actions.append(SwipeAction(SwipeAction::Text,0,0,0,0,0,delayMs,text,mode));
    emit modelChanged();}

void SwipeModel::addCommand(const QString &command,int delayMs,const QString &mode){
    m_actions.append(SwipeAction(SwipeAction::Command,0,0,0,0,0,delayMs,command,mode));
    emit modelChanged();}


void SwipeModel::addKey(const QString &keyCommand,int delayMs,const QString &mode){
    m_actions.append(SwipeAction(SwipeAction::Key,0,0,0,0,0,delayMs,keyCommand,mode));
    emit modelChanged();}

void SwipeModel::addWait(int delayMs){
    m_actions.append(SwipeAction(SwipeAction::Wait,0,0,0,0,0,delayMs));
    emit modelChanged();}

void SwipeModel::addAdbWifi(){
    m_actions.append(SwipeAction(SwipeAction::AdbWifi));
    emit modelChanged();}

void SwipeModel::addGrabControl(bool enable){
    m_actions.append(SwipeAction(enable?SwipeAction::GrabOn:SwipeAction::GrabOff));
    emit modelChanged();}

void SwipeModel::addMacroControl(int actionType){
    SwipeAction::Type t=SwipeAction::MacroPlay;
    if(actionType==0)t=SwipeAction::MacroRec;
    else if(actionType==1)t=SwipeAction::MacroStop;
    m_actions.append(SwipeAction(t));
    emit modelChanged();}

SwipeAction SwipeModel::actionAt(int index) const{
    if(index>=0&&index<m_actions.size())return m_actions.at(index);
    return SwipeAction();}

void SwipeModel::editActionAt(int index,const SwipeAction &action){
    if(index>=0&&index<m_actions.size()){m_actions[index]=action;emit modelChanged();}}

void SwipeModel::removeActionAt(int index){
    if(index>=0&&index<m_actions.size()){m_actions.removeAt(index);emit modelChanged();}}

void SwipeModel::moveAction(int from,int to){
    if(from!=to&&from>=0&&from<m_actions.size()&&to>=0&&to<m_actions.size()){m_actions.move(from,to);emit modelChanged();}}

void SwipeModel::clear(){m_actions.clear(); emit modelChanged();}

QJsonArray SwipeModel::toJsonArray() const{
    QJsonArray array;
    for(const SwipeAction &a:m_actions)array.append(actionToJson(a));
    return array;}

QJsonArray SwipeModel::toJsonSequence() const{return toJsonArray();}

void SwipeModel::fromJsonArray(const QJsonArray &array) {
    m_actions.clear();
    for (const QJsonValue &value : array) {
        QJsonObject obj = value.toObject();
        SwipeAction::Type type = static_cast<SwipeAction::Type>(obj["type"].toInt());
        int x1 = obj.contains("x1") ? obj["x1"].toInt() : 0;
        int y1 = obj.contains("y1") ? obj["y1"].toInt() : 0;
        int x2 = obj.contains("x2") ? obj["x2"].toInt() : 0;
        int y2 = obj.contains("y2") ? obj["y2"].toInt() : 0;
        int duration = obj.contains("duration") ? obj["duration"].toInt() : 0;
        int delay = obj["delayAfterMs"].toInt();
        QString cmd = obj["command"].toString();
        QString mode = obj["runMode"].toString();

        if (x1 == 0 && y1 == 0 && (type == SwipeAction::Tap || type == SwipeAction::Swipe)) {
            QStringList parts = cmd.split(' ', Qt::SkipEmptyParts);
            int offset = (parts.value(0) == "input") ? 2 : 1;
            x1 = parts.value(offset).toInt();
            y1 = parts.value(offset + 1).toInt();
            if (type == SwipeAction::Swipe) {
                x2 = parts.value(offset + 2).toInt();
                y2 = parts.value(offset + 3).toInt();
                duration = parts.value(offset + 4).toInt();
            }
        }
        m_actions.append(SwipeAction(type, x1, y1, x2, y2, duration, delay, cmd, mode));
    }
    emit modelChanged();}

QJsonObject SwipeModel::actionToJson(const SwipeAction &action) const {
	QJsonObject obj;
	QString finalCommand;
	bool isIoctl = (action.runMode.toLower() == "ioctl");
    obj["type"] = static_cast<int>(action.type);
	if (action.delayAfterMs > 0)
	obj["delayAfterMs"] = action.delayAfterMs;
    obj["runMode"] = action.runMode.isEmpty() ? "shell" : action.runMode;
    obj["stopOnError"] = true;
    if (action.type == SwipeAction::Tap || action.type == SwipeAction::Swipe || action.type == SwipeAction::LongPress) {
        obj["x1"] = action.x1;
        obj["y1"] = action.y1;
        obj["x2"] = action.x2;
        obj["y2"] = action.y2;
        if (action.duration > 0)
            obj["duration"] = action.duration;
    }
    switch (action.type) {
        case SwipeAction::Tap:
            finalCommand = isIoctl ? QString("tap %1 %2").arg(action.x1).arg(action.y1)
                                   : QString("input tap %1 %2").arg(action.x1).arg(action.y1);
            break;
        case SwipeAction::Swipe:
        case SwipeAction::LongPress:
            finalCommand = isIoctl ? QString("swipe %1 %2 %3 %4 %5").arg(action.x1).arg(action.y1).arg(action.x2).arg(action.y2).arg(action.duration)
                                   : QString("input swipe %1 %2 %3 %4 %5").arg(action.x1).arg(action.y1).arg(action.x2).arg(action.y2).arg(action.duration);
            break;
        case SwipeAction::Key: {
            QString cleanKey = action.command;
            cleanKey.remove("input keyevent ", Qt::CaseInsensitive);
            cleanKey.remove("key ", Qt::CaseInsensitive);
            finalCommand = isIoctl ? QString("key %1").arg(cleanKey.trimmed())
                                   : QString("input keyevent %1").arg(cleanKey.trimmed());
            break;
        }
        case SwipeAction::Wait:      finalCommand = "wait"; break;
        case SwipeAction::AdbWifi:   finalCommand = "adb_wifi"; break;
        case SwipeAction::GrabOn:    finalCommand = "grab 1"; break;
        case SwipeAction::GrabOff:   finalCommand = "grab 0"; break;
        case SwipeAction::MacroRec:  finalCommand = "rec_start"; break;
        case SwipeAction::MacroStop: finalCommand = "rec_stop"; break;
        case SwipeAction::MacroPlay: finalCommand = "replay"; break;
        case SwipeAction::Text:      finalCommand = QString("input text %1").arg(action.command); break;
        default:                     finalCommand = action.command; break;
    }
    obj["command"] = finalCommand;
    return obj;
}
