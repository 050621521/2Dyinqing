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
    QStringList              components;
    QMap<QString, QVariant>  defaults;
    QList<BPNode>            nodes;
    QList<BPConnection>      connections;

    bool isBuiltin() const { return filePath.isEmpty(); }
    bool hasNodes()  const { return !nodes.isEmpty(); }

    bool save() const;
    static BPClass load(const QString& filePath);

    QJsonObject toJson() const;
    static BPClass fromJson(const QJsonObject& obj, const QString& filePath = {});

    // 内置类工厂：bpClass 字符串如 "builtin/Sprite" → BPClass
    static const BPClass* findBuiltin(const QString& bpClass);
    static QList<BPClass> builtinClasses();
};
