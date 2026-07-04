#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

struct AssetRecord {
    QString id;
    QString path;   // 项目相对路径
    QString type;   // level / bp / bp.effect / bp.component / ui / enum / anim / datatable / image
    QString name;
    QString suffix;
};

class AssetRegistry {
public:
    explicit AssetRegistry(QString projectRoot = {});

    void setProjectRoot(const QString& projectRoot);
    QString projectRoot() const { return m_projectRoot; }

    bool load();
    bool save() const;
    bool rebuild(bool writeBackIds = true);

    QList<AssetRecord> records() const { return m_records; }
    AssetRecord findById(const QString& assetId) const;
    AssetRecord findByPath(const QString& path) const;
    AssetRecord recordForFile(const QString& absOrRelPath) const;
    QString redirectTargetId(const QString& oldPath) const;
    QStringList redirectPathsForAsset(const QString& assetId) const;
    bool noteAssetMoved(const QString& oldAbsOrRelPath, const QString& newAbsOrRelPath);

    QString normalizePath(const QString& absOrRelPath) const;
    QString absolutePath(const QString& projectRelativePath) const;

private:
    struct CachedRecord {
        QString id;
        QString type;
    };

    QString registryPath() const;
    QHash<QString, CachedRecord> loadCachedByPath() const;
    QHash<QString, AssetRecord> loadCachedById() const;
    QHash<QString, QString> loadRedirects() const;
    AssetRecord inspectFile(const QString& absPath, const QHash<QString, CachedRecord>& cachedByPath,
                            bool writeBackIds) const;
    QString detectType(const QString& absPath, const QJsonObject& obj) const;
    QString displayNameFor(const QString& absPath, const QJsonObject& obj) const;
    QString ensureJsonAssetId(const QString& absPath, QJsonObject& obj, const QString& cachedId,
                              bool writeBackIds) const;
    void pruneRedirects();
    void rebuildIndexes();

    QString m_projectRoot;
    QList<AssetRecord> m_records;
    QHash<QString, AssetRecord> m_byId;
    QHash<QString, AssetRecord> m_byPath;
    QHash<QString, QString> m_redirects; // old project-relative path -> assetId
};
