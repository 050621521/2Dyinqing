#pragma once
#include "ProjectInfo.h"
#include <QList>
#include <QString>

class ProjectManager {
public:
    static ProjectManager& instance();

    void load();
    void save() const;

    QList<ProjectInfo> recentProjects() const;
    void addRecentProject(const ProjectInfo& info);
    void removeProject(const QString& path);

    QList<TemplateCategory> templateCategories() const;

    QString configPath() const;

private:
    ProjectManager() = default;
    QList<ProjectInfo> m_recent;
    QList<TemplateCategory> m_categories;
};
