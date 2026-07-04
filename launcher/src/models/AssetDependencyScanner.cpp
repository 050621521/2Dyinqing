#include "AssetDependencyScanner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

AssetDependencyScanner::AssetDependencyScanner(QString projectRoot)
    : m_projectRoot(std::move(projectRoot)) {}

void AssetDependencyScanner::setProjectRoot(const QString& projectRoot) {
    m_projectRoot = projectRoot;
}

QList<AssetDependency> AssetDependencyScanner::findReferences(const AssetRecord& target) const {
    QList<AssetDependency> refs;
    if (target.path.isEmpty()) return refs;

    AssetRegistry registry(m_projectRoot);
    registry.rebuild(false);
    for (const AssetRecord& source : registry.records()) {
        if (source.path == target.path) continue;
        const QString absPath = registry.absolutePath(source.path);
        QFile f(absPath);
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (!doc.isObject() && !doc.isArray()) continue;

        QStringList details;
        if (!jsonContainsReference(doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array()),
                                   target, {}, &details)) {
            continue;
        }

        AssetDependency dep;
        dep.sourcePath = source.path;
        dep.sourceName = source.name;
        dep.sourceType = typeDisplayName(source.type);
        dep.detail = details.join("、");
        refs << dep;
    }
    return refs;
}

bool AssetDependencyScanner::jsonContainsReference(const QJsonValue& value, const AssetRecord& target,
                                                  const QString& currentKey, QStringList* details) const {
    bool found = false;
    if (value.isString()) {
        if (stringMatchesTarget(value.toString(), target)) {
            details->append(currentKey.isEmpty() ? "路径引用" : currentKey);
            return true;
        }
        return false;
    }

    if (value.isObject()) {
        const QJsonObject obj = value.toObject();
        const QString id = obj.value("id").toString(obj.value("assetId").toString());
        const QString path = obj.value("path").toString();
        const bool idMatches = !target.id.isEmpty() && id == target.id;
        const bool pathMatches = stringMatchesTarget(path, target);
        if (idMatches || pathMatches) {
            details->append(currentKey.isEmpty() ? "软引用" : currentKey);
            found = true;
        }
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
            if (jsonContainsReference(it.value(), target, it.key(), details))
                found = true;
        }
        return found;
    }

    if (value.isArray()) {
        const QJsonArray arr = value.toArray();
        for (const QJsonValue& v : arr) {
            if (jsonContainsReference(v, target, currentKey, details))
                found = true;
        }
    }
    return found;
}

bool AssetDependencyScanner::stringMatchesTarget(const QString& value, const AssetRecord& target) const {
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) return false;

    const QString normalizedValue = QDir::cleanPath(QFileInfo(trimmed).isAbsolute()
        ? QDir(m_projectRoot).relativeFilePath(trimmed)
        : trimmed);
    const QString normalizedTarget = QDir::cleanPath(target.path);
    if (normalizedValue == normalizedTarget) return true;
    if (QFileInfo(normalizedValue).fileName() == QFileInfo(normalizedTarget).fileName()
        && normalizedValue.endsWith(QFileInfo(normalizedTarget).fileName())
        && QFileInfo(normalizedTarget).suffix() == QFileInfo(normalizedValue).suffix()) {
        return true;
    }
    return !target.id.isEmpty() && trimmed == target.id;
}

QString AssetDependencyScanner::typeDisplayName(const QString& type) const {
    if (type == "level") return "关卡";
    if (type == "bp") return "Actor 蓝图";
    if (type == "bp.effect") return "效果蓝图";
    if (type == "bp.component") return "组件蓝图";
    if (type == "ui") return "UI";
    if (type == "enum") return "枚举";
    if (type == "anim") return "动画";
    if (type == "datatable") return "数据表";
    if (type == "image") return "图片";
    return "资产";
}
