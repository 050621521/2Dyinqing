#pragma once

#include <QJsonValue>
#include <QString>

class AssetRegistry;

struct AssetRef {
    QString raw;

    AssetRef() = default;
    explicit AssetRef(QString value) : raw(std::move(value)) {}
    bool isEmpty() const { return raw.trimmed().isEmpty(); }
    QString displayName() const;
};

struct SoftAssetRef {
    QString assetId;
    QString path;
    QString expectedType;

    bool isEmpty() const;
    QString displayName() const;
    QJsonValue toJson() const;

    static SoftAssetRef fromString(const QString& value, const QString& expectedType = {});
    static SoftAssetRef fromVariant(const QJsonValue& value, const QString& expectedType = {});
};

class AssetResolver {
public:
    explicit AssetResolver(QString projectRoot = {}, const AssetRegistry* registry = nullptr);

    void setProjectRoot(const QString& projectRoot) { m_projectRoot = projectRoot; }
    void setRegistry(const AssetRegistry* registry) { m_registry = registry; }
    QString resolve(const AssetRef& ref, const QString& defaultFolder = {}, const QString& suffix = {}) const;
    QString resolve(const SoftAssetRef& ref, const QString& defaultFolder = {}, const QString& suffix = {}) const;
    bool exists(const AssetRef& ref, const QString& defaultFolder = {}, const QString& suffix = {}) const;
    bool exists(const SoftAssetRef& ref, const QString& defaultFolder = {}, const QString& suffix = {}) const;
    QString missingMessage(const QString& label, const AssetRef& ref,
                           const QString& defaultFolder = {}, const QString& suffix = {}) const;
    QString missingMessage(const QString& label, const SoftAssetRef& ref,
                           const QString& defaultFolder = {}, const QString& suffix = {}) const;

private:
    QString m_projectRoot;
    const AssetRegistry* m_registry = nullptr;
};
