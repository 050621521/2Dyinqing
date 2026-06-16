#pragma once
#include <QString>
#include <QList>

// 全局变量声明（项目级，存 project.json 的 globalVariables 数组）
// type: "number" / "bool" / "string"（后续扩展 "enum:<名>"）
struct GlobalVarDef {
    QString name;
    QString type = "string";
};

namespace GlobalVars {
    // 从 {projectRoot}/project.json 读取声明列表
    QList<GlobalVarDef> load(const QString& projectRoot);
    // 写回声明列表（read-modify-write，不破坏 project.json 其它字段）
    bool save(const QString& projectRoot, const QList<GlobalVarDef>& vars);
    // 类型 → 中文名（面板/显示用）
    QString typeLabel(const QString& type);
}
