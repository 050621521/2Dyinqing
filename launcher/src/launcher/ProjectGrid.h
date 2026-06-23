#pragma once
#include "models/ProjectInfo.h"
#include <QScrollArea>
#include <QList>

class ProjectCard;

class ProjectGrid : public QScrollArea {
    Q_OBJECT
public:
    explicit ProjectGrid(QWidget* parent = nullptr);

    void setProjects(const QList<ProjectInfo>& projects);
    void filter(const QString& text);

    ProjectInfo selectedProject() const;
    bool hasSelection() const;

signals:
    void selectionChanged(const ProjectInfo& info);
    void projectOpened(const ProjectInfo& info);
    void moveRequested(const ProjectInfo& info);
    void removeRequested(const ProjectInfo& info);
    void deleteRequested(const ProjectInfo& info);

private:
    void rebuild();
    void selectCard(ProjectCard* card);

    QList<ProjectInfo>  m_allProjects;
    QList<ProjectInfo>  m_filtered;
    QList<ProjectCard*> m_cards;
    ProjectCard*        m_selected = nullptr;
    QString             m_filterText;

    QWidget* m_container;
};
