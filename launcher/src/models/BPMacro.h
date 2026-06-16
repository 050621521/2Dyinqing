#pragma once
#include "LevelDocument.h"
#include <QString>
#include <QList>
#include <QJsonObject>

// 宏式自定义节点的对外引脚定义。
// kind 对应编辑器的 ValueKind 名（"Text"/"Bool"/"LevelRef"/"ActorRef"/"UIRef"/"WidgetRef"）。
struct MacroPin {
    QString key;
    QString label;
    bool    isExec = false;
    QString kind   = "Text";

    QJsonObject toJson() const;
    static MacroPin fromJson(const QJsonObject& obj);
};

// 宏资产：一段可复用子图 + 对外输入/输出引脚。
// 存于 {project}/Blueprints/Macros/<name>.bpmacro（JSON）。
struct BPMacro {
    QString             id;          // 稳定唯一 id（调用节点按它引用）
    QString             name;        // 显示名
    QString             filePath;    // 存盘路径
    QList<MacroPin>     inputPins;   // 对外输入（入口节点的输出引脚）
    QList<MacroPin>     outputPins;  // 对外输出（出口节点的输入引脚）
    QList<BPNode>       nodes;       // 内部子图
    QList<BPConnection> connections;

    QJsonObject toJson() const;
    static BPMacro fromJson(const QJsonObject& obj, const QString& filePath = {});

    bool save() const;
    static BPMacro load(const QString& filePath);

    // {project}/Blueprints/Macros 目录；listAll 读取其下所有 *.bpmacro
    static QString        macrosDir(const QString& projectRoot);
    static QList<BPMacro> listAll(const QString& projectRoot);
};
