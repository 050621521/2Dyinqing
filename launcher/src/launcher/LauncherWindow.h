#pragma once
#include "models/ProjectInfo.h"
#include <QMainWindow>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QCloseEvent>

class CategoryList;
class ProjectGrid;
class SearchBar;

class LauncherWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit LauncherWindow(QWidget* parent = nullptr);

signals:
    void projectSelected(const ProjectInfo& info);

private:
    void setupUi();
    void loadData();
    void applyStyleSheet();

    void onCategorySelected(const QString& id);
    void onSelectionChanged(const ProjectInfo& info);
    void onProjectOpened(const ProjectInfo& info);
    void onNewProject();
    void onOpenProject();
    void onBrowse();

    CategoryList* m_categoryList;
    ProjectGrid*  m_grid;
    SearchBar*    m_searchBar;

    QLineEdit*   m_pathEdit;
    QLineEdit*   m_nameEdit;
    QPushButton* m_openBtn;
    QPushButton* m_browseBtn;
    QPushButton* m_cancelBtn;
    QPushButton* m_newBtn;

    QString m_currentCategory;
};
