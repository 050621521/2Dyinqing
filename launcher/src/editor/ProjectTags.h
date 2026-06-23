#pragma once
#include <QString>
#include <QStringList>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

// 项目级「标签」表的读写（存于 {project}/project.json 的 "tags" 数组）。
//
// 标签是 Actor 级身份标记（细节面板「标签」下拉），用于碰撞过滤等逻辑识别。
// 用户可自定义新增（类似 Unity Add Tag），项目内持久化、跨会话保留。

namespace ProjectTags {

// 默认标签（project.json 无 tags 键时的初始集）
inline QStringList defaults() {
    return {"未标记", "玩家", "敌人", "地面", "障碍", "拾取物", "墙"};
}

inline QString jsonPath(const QString& projectRoot) {
    return projectRoot + "/project.json";
}

inline QStringList read(const QString& projectRoot) {
    if (projectRoot.isEmpty()) return defaults();
    QFile f(jsonPath(projectRoot));
    if (!f.open(QIODevice::ReadOnly)) return defaults();
    const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    if (!obj.contains("tags")) return defaults();
    QStringList out;
    for (const QJsonValue& v : obj.value("tags").toArray()) {
        const QString s = v.toString();
        if (!s.isEmpty() && !out.contains(s)) out.append(s);
    }
    if (!out.contains("未标记")) out.prepend("未标记");
    return out.isEmpty() ? defaults() : out;
}

// 写回 tags，保留 project.json 其余键
inline bool write(const QString& projectRoot, const QStringList& tags) {
    if (projectRoot.isEmpty()) return false;
    QJsonObject obj;
    QFile f(jsonPath(projectRoot));
    if (f.open(QIODevice::ReadOnly)) {
        obj = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
    }
    QJsonArray arr;
    for (const QString& t : tags) arr.append(t);
    obj["tags"] = arr;
    QSaveFile sf(jsonPath(projectRoot));
    if (!sf.open(QIODevice::WriteOnly)) return false;
    sf.write(QJsonDocument(obj).toJson());
    return sf.commit();
}

// 新增一个标签（已存在则忽略），返回最新标签表
inline QStringList add(const QString& projectRoot, const QString& tag) {
    QStringList tags = read(projectRoot);
    const QString t = tag.trimmed();
    if (!t.isEmpty() && !tags.contains(t)) {
        tags.append(t);
        write(projectRoot, tags);
    }
    return tags;
}

// 删除标签（「未标记」不可删），返回最新标签表
inline QStringList remove(const QString& projectRoot, const QString& tag) {
    QStringList tags = read(projectRoot);
    if (tag != "未标记") {
        tags.removeAll(tag);
        write(projectRoot, tags);
    }
    return tags;
}

} // namespace ProjectTags
