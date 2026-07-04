#pragma once

#include "AssetRegistry.h"

#include <QJsonValue>
#include <QList>
#include <QString>

struct AssetDependency {
    QString sourcePath;
    QString sourceName;
    QString sourceType;
    QString detail;
};

class AssetDependencyScanner {
public:
    explicit AssetDependencyScanner(QString projectRoot = {});

    void setProjectRoot(const QString& projectRoot);
    QList<AssetDependency> findReferences(const AssetRecord& target) const;

private:
    bool jsonContainsReference(const QJsonValue& value, const AssetRecord& target,
                               const QString& currentKey, QStringList* details) const;
    bool stringMatchesTarget(const QString& value, const AssetRecord& target) const;
    QString typeDisplayName(const QString& type) const;

    QString m_projectRoot;
};
