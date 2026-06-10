#pragma once
#include <QString>
#include <QStringList>
#include <QColor>

// 所有文件共享的 Actor 类型工具函数，避免重复定义

static const QStringList kActorTypes = {"Camera", "Sprite", "Light", "Trigger", "Empty"};

inline QStringList defaultComponents(const QString& type) {
    if (type == "Camera")  return {"变换", "摄像机组件"};
    if (type == "Sprite")  return {"变换", "精灵渲染器"};
    if (type == "Light")   return {"变换", "点光源"};
    if (type == "Trigger") return {"变换", "碰撞盒"};
    return {"变换"};
}

inline QString typeLabel(const QString& type) {
    if (type == "Camera")  return "摄像机";
    if (type == "Sprite")  return "精灵";
    if (type == "Light")   return "光源";
    if (type == "Trigger") return "触发区域";
    return "空对象";
}

inline QColor actorTypeColor(const QString& type) {
    if (type == "Camera")  return QColor(80,  160, 240);
    if (type == "Sprite")  return QColor(80,  200, 100);
    if (type == "Light")   return QColor(255, 220,  50);
    if (type == "Trigger") return QColor(220, 130,  50);
    return                        QColor(160, 160, 160);
}
