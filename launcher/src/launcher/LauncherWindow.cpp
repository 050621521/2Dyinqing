#include "LauncherWindow.h"
#include "CategoryList.h"
#include "ProjectGrid.h"
#include "SearchBar.h"
#include "NewProjectDialog.h"
#include "models/ProjectManager.h"
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QFileDialog>
#include <QDir>
#include <QMessageBox>
#include <QApplication>
#include <QFile>
#include <QTimer>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>

LauncherWindow::LauncherWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("2D引擎 — 项目浏览器");
    setMinimumSize(900, 620);
    resize(1100, 720);

    applyStyleSheet();
    setupUi();
    loadData();
}

void LauncherWindow::applyStyleSheet() {
    QFile f(":/styles/launcher.qss");
    if (f.open(QIODevice::ReadOnly))
        qApp->setStyleSheet(QString::fromUtf8(f.readAll()));
}

void LauncherWindow::setupUi() {
    auto* central = new QWidget(this);
    setCentralWidget(central);

    auto* rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // ── top search bar ──────────────────────────────────────────────
    m_searchBar = new SearchBar(this);
    m_searchBar->setObjectName("topBar");
    rootLayout->addWidget(m_searchBar);

    // ── main content (left category + right grid) ───────────────────
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setObjectName("mainSplitter");
    splitter->setHandleWidth(1);

    m_categoryList = new CategoryList(this);
    splitter->addWidget(m_categoryList);

    m_grid = new ProjectGrid(this);
    splitter->addWidget(m_grid);
    splitter->setStretchFactor(1, 1);

    rootLayout->addWidget(splitter, 1);

    // ── bottom bar ──────────────────────────────────────────────────
    auto* bottomBar = new QWidget(this);
    bottomBar->setObjectName("bottomBar");
    bottomBar->setFixedHeight(56);

    auto* bottomLayout = new QHBoxLayout(bottomBar);
    bottomLayout->setContentsMargins(12, 8, 12, 8);
    bottomLayout->setSpacing(8);

    auto* pathLabel = new QLabel("项目位置", this);
    pathLabel->setObjectName("bottomLabel");
    m_pathEdit = new QLineEdit(this);
    m_pathEdit->setObjectName("bottomEdit");
    m_pathEdit->setReadOnly(true);
    m_pathEdit->setPlaceholderText("——");

    auto* nameLabel = new QLabel("项目名称", this);
    nameLabel->setObjectName("bottomLabel");
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setObjectName("bottomEdit");
    m_nameEdit->setReadOnly(true);
    m_nameEdit->setFixedWidth(200);
    m_nameEdit->setPlaceholderText("——");

    m_browseBtn = new QPushButton("浏览……", this);
    m_browseBtn->setObjectName("secondaryBtn");

    m_newBtn = new QPushButton("新建项目", this);
    m_newBtn->setObjectName("secondaryBtn");

    m_openBtn = new QPushButton("打开", this);
    m_openBtn->setObjectName("primaryBtn");
    m_openBtn->setEnabled(false);

    m_cancelBtn = new QPushButton("取消", this);
    m_cancelBtn->setObjectName("cancelBtn");

    bottomLayout->addWidget(pathLabel);
    bottomLayout->addWidget(m_pathEdit, 1);
    bottomLayout->addWidget(nameLabel);
    bottomLayout->addWidget(m_nameEdit);
    bottomLayout->addSpacing(12);
    bottomLayout->addWidget(m_browseBtn);
    bottomLayout->addWidget(m_newBtn);
    bottomLayout->addWidget(m_openBtn);
    bottomLayout->addWidget(m_cancelBtn);

    rootLayout->addWidget(bottomBar);

    // ── connections ─────────────────────────────────────────────────
    connect(m_searchBar, &SearchBar::filterChanged, m_grid, &ProjectGrid::filter);
    connect(m_searchBar, &SearchBar::refreshRequested, this, &LauncherWindow::loadData);
    connect(m_categoryList, &CategoryList::categorySelected, this, &LauncherWindow::onCategorySelected);
    connect(m_grid, &ProjectGrid::selectionChanged, this, &LauncherWindow::onSelectionChanged);
    connect(m_grid, &ProjectGrid::projectOpened, this, &LauncherWindow::onProjectOpened);
    connect(m_grid, &ProjectGrid::removeRequested, this, [this](const ProjectInfo& info) {
        ProjectManager::instance().removeProject(info.path);
        m_openBtn->setEnabled(false);
        QTimer::singleShot(0, this, &LauncherWindow::loadData);
    });
    connect(m_grid, &ProjectGrid::deleteRequested, this, [this](const ProjectInfo& info) {
        QMessageBox box(this);
        box.setWindowTitle("彻底删除");
        box.setText(QString("确定要删除「%1」吗？").arg(info.name));
        box.setInformativeText(QString("将永久删除文件夹：\n%1\n\n此操作不可撤销。").arg(info.path));
        box.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
        box.setDefaultButton(QMessageBox::Cancel);
        box.button(QMessageBox::Yes)->setText("删除");
        box.button(QMessageBox::Cancel)->setText("取消");
        if (box.exec() != QMessageBox::Yes) return;

        QDir(info.path).removeRecursively();
        ProjectManager::instance().removeProject(info.path);
        m_openBtn->setEnabled(false);   // 立即禁用，防止残留鼠标事件触发打开
        QTimer::singleShot(0, this, &LauncherWindow::loadData);  // 下次事件循环再重建网格
    });
    connect(m_openBtn, &QPushButton::clicked, this, &LauncherWindow::onOpenProject);
    connect(m_browseBtn, &QPushButton::clicked, this, &LauncherWindow::onBrowse);
    connect(m_newBtn, &QPushButton::clicked, this, &LauncherWindow::onNewProject);
    connect(m_cancelBtn, &QPushButton::clicked, qApp, &QApplication::quit);
}

void LauncherWindow::loadData() {
    auto& mgr = ProjectManager::instance();
    mgr.load();

    m_categoryList->setCategories(mgr.templateCategories());
    m_currentCategory = "recent";
    m_grid->setProjects(mgr.recentProjects());
}

void LauncherWindow::onCategorySelected(const QString& id) {
    m_currentCategory = id;
    auto& mgr = ProjectManager::instance();

    if (id == "recent") {
        m_grid->setProjects(mgr.recentProjects());
    } else {
        // template categories — show empty or placeholder projects
        m_grid->setProjects({});
    }
}

void LauncherWindow::onSelectionChanged(const ProjectInfo& info) {
    m_pathEdit->setText(info.path);
    m_nameEdit->setText(info.name);
    m_openBtn->setEnabled(info.isValid());
}

void LauncherWindow::onProjectOpened(const ProjectInfo& info) {
    ProjectManager::instance().addRecentProject(info);
    emit projectSelected(info);
}

void LauncherWindow::onOpenProject() {
    if (m_grid->hasSelection())
        onProjectOpened(m_grid->selectedProject());
}

void LauncherWindow::onBrowse() {
    QString path = QFileDialog::getExistingDirectory(this, "选择项目目录");
    if (path.isEmpty()) return;

    ProjectInfo info;
    info.name       = QDir(path).dirName();
    info.path       = path;
    info.lastOpened = QDateTime::currentDateTime();
    ProjectManager::instance().addRecentProject(info);
    loadData();
}

void LauncherWindow::onNewProject() {
    NewProjectDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;

    ProjectInfo info = dlg.result();
    QDir().mkpath(info.path);

    // 自动创建 Levels/Default.level
    QDir(info.path).mkpath("Levels");
    const QString lvlPath = info.path + "/Levels/Default.level";
    if (!QFile::exists(lvlPath)) {
        QJsonObject obj;
        obj["name"]    = "Default";
        obj["version"] = "0.1";
        obj["objects"] = QJsonArray();
        QFile f(lvlPath);
        if (f.open(QIODevice::WriteOnly))
            f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    }

    onProjectOpened(info);
}
