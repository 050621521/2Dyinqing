#include "AssetRef.h"
#include "AssetRegistry.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonObject>

QString AssetRef::displayName() const {
    return QFileInfo(raw).completeBaseName();
}

bool SoftAssetRef::isEmpty() const {
    return assetId.trimmed().isEmpty() && path.trimmed().isEmpty();
}

QString SoftAssetRef::displayName() const {
    return QFileInfo(path).completeBaseName();
}

QJsonValue SoftAssetRef::toJson() const {
    if (assetId.trimmed().isEmpty() && expectedType.trimmed().isEmpty())
        return path;
    QJsonObject obj;
    if (!assetId.trimmed().isEmpty()) obj["id"] = assetId;
    if (!path.trimmed().isEmpty()) obj["path"] = path;
    if (!expectedType.trimmed().isEmpty()) obj["type"] = expectedType;
    return obj;
}

SoftAssetRef SoftAssetRef::fromString(const QString& value, const QString& expectedType) {
    SoftAssetRef ref;
    ref.path = value.trimmed();
    ref.expectedType = expectedType;
    return ref;
}

SoftAssetRef SoftAssetRef::fromVariant(const QJsonValue& value, const QString& expectedType) {
    if (value.isObject()) {
        const QJsonObject obj = value.toObject();
        SoftAssetRef ref;
        ref.assetId = obj.value("id").toString(obj.value("assetId").toString()).trimmed();
        ref.path = obj.value("path").toString().trimmed();
        ref.expectedType = obj.value("type").toString(expectedType).trimmed();
        if (ref.expectedType.isEmpty()) ref.expectedType = expectedType;
        return ref;
    }
    return fromString(value.toString(), expectedType);
}

AssetResolver::AssetResolver(QString projectRoot, const AssetRegistry* registry)
    : m_projectRoot(std::move(projectRoot)), m_registry(registry) {}

QString AssetResolver::resolve(const AssetRef& ref, const QString& defaultFolder, const QString& suffix) const {
    return resolve(SoftAssetRef::fromString(ref.raw), defaultFolder, suffix);
}

QString AssetResolver::resolve(const SoftAssetRef& ref, const QString& defaultFolder, const QString& suffix) const {
    if (m_registry && !ref.assetId.trimmed().isEmpty()) {
        const AssetRecord record = m_registry->findById(ref.assetId);
        if (!record.path.isEmpty()
            && (ref.expectedType.trimmed().isEmpty() || record.type == ref.expectedType))
            return m_registry->absolutePath(record.path);
    }

    if (m_registry && !ref.path.trimmed().isEmpty()) {
        const AssetRecord record = m_registry->findByPath(ref.path);
        if (!record.path.isEmpty()
            && (ref.expectedType.trimmed().isEmpty() || record.type == ref.expectedType))
            return m_registry->absolutePath(record.path);
    }

    QString value = ref.path.trimmed();
    if (value.isEmpty()) return {};
    const QFileInfo info(value);
    if (info.isAbsolute()) return value;

    const bool hasSuffix = suffix.isEmpty() || value.endsWith(suffix);
    if (!hasSuffix) value += suffix;
    if (!defaultFolder.isEmpty() && !value.contains('/'))
        value = defaultFolder + "/" + value;
    return QDir(m_projectRoot).filePath(value);
}

bool AssetResolver::exists(const AssetRef& ref, const QString& defaultFolder, const QString& suffix) const {
    const QString path = resolve(ref, defaultFolder, suffix);
    return !path.isEmpty() && QFileInfo::exists(path);
}

bool AssetResolver::exists(const SoftAssetRef& ref, const QString& defaultFolder, const QString& suffix) const {
    const QString path = resolve(ref, defaultFolder, suffix);
    return !path.isEmpty() && QFileInfo::exists(path);
}

QString AssetResolver::missingMessage(const QString& label, const AssetRef& ref,
                                      const QString& defaultFolder, const QString& suffix) const {
    return QString("%1不存在：%2").arg(label, resolve(ref, defaultFolder, suffix));
}

QString AssetResolver::missingMessage(const QString& label, const SoftAssetRef& ref,
                                      const QString& defaultFolder, const QString& suffix) const {
    QString detail = resolve(ref, defaultFolder, suffix);
    if (detail.isEmpty()) {
        detail = ref.path;
        if (!ref.assetId.isEmpty()) detail += QString("（ID：%1）").arg(ref.assetId);
    }
    return QString("%1不存在或类型不匹配：%2").arg(label, detail);
}
