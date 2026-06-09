#pragma once
#include <QString>
#include <QDateTime>

struct ProjectInfo {
    QString name;
    QString path;
    QString thumbnailPath;
    QDateTime lastOpened;
    QString engineVersion;

    bool isValid() const { return !name.isEmpty() && !path.isEmpty(); }
};

struct TemplateCategory {
    QString id;
    QString displayName;
    QString coverImagePath;
};
