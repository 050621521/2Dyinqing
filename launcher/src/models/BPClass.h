#pragma once
#include "LevelDocument.h"
#include <QString>
#include <QStringList>
#include <QMap>
#include <QVariant>
#include <QJsonObject>

struct BPClass {
    QString                  name;
    QString                  filePath;   // 空字符串 = 内置类
    QString                  blueprintType = "Actor"; // Actor | Effect | Component（后续）
    QStringList              components;
    QStringList              componentBlueprints; // Actor 类默认挂载的组件蓝图
    QList<ComponentInstance> componentInstances;  // Actor 类默认挂载的组件实例
    QMap<QString, QVariant>  defaults;
    QList<BPNode>            nodes;
    QList<BPConnection>      connections;

    bool isBuiltin() const { return filePath.isEmpty(); }
    bool hasNodes()  const { return !nodes.isEmpty(); }

    bool save() const;
    static BPClass load(const QString& filePath);

    QJsonObject toJson() const;
    static BPClass fromJson(const QJsonObject& obj, const QString& filePath = {});

    // 内置类工厂：bpClass 字符串如 "builtin/Sprite" → BPClass，未找到返回 nullptr
    static const BPClass* findBuiltin(const QString& bpClass);
    static QList<BPClass> builtinClasses();
};
