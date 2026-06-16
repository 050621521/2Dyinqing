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

// 枚举资产：存为内容浏览器里的 .enum 文件（文件名 = 枚举名）
struct EnumDef {
    QString     name;
    QStringList values;        // 键值：逻辑里用它比较/存储（稳定标识）
    QStringList displays;      // 显示命名：UI 下拉里显示（可空→回退到键值）
    QStringList descriptions;  // 描述：仅 UI 备注（可空）
    QString     filePath;      // 来源文件（扫描时填）

    bool           save(const QString& filePath) const;
    static EnumDef load(const QString& filePath);
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
    // 递归扫描工程内所有 .enum 资产
    QList<EnumDef> loadAll(const QString& projectRoot);
    // 在声明列表里找某枚举的选项
    QStringList valuesOf(const QList<EnumDef>& enums, const QString& name);
}
