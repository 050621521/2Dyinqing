#pragma once
#include <QString>
#include <QStringList>
#include <QList>

// 全局变量声明（项目级，存 project.json 的 globalVariables 数组）
// type: "number" / "bool" / "string" / "enum:<枚举名>"
struct GlobalVarDef {
    QString name;
    QString type = "string";
};

// 枚举声明（项目级，存 project.json 的 enums 数组）
struct EnumDef {
    QString     name;
    QStringList values;
};

namespace GlobalVars {
    // 从 {projectRoot}/project.json 读取声明列表
    QList<GlobalVarDef> load(const QString& projectRoot);
    // 写回声明列表（read-modify-write，不破坏 project.json 其它字段）
    bool save(const QString& projectRoot, const QList<GlobalVarDef>& vars);
    // 类型 → 中文名（面板/显示用）
    QString typeLabel(const QString& type);
}

namespace Enums {
    QList<EnumDef> load(const QString& projectRoot);
    bool save(const QString& projectRoot, const QList<EnumDef>& enums);
    // 在声明列表里找某枚举的选项
    QStringList valuesOf(const QList<EnumDef>& enums, const QString& name);
}
