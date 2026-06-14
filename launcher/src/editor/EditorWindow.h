#pragma once
#include "models/ProjectInfo.h"
#include "models/LevelDocument.h"
#include <QMainWindow>
#include <QMap>
#include <QSet>
#include <QButtonGroup>
#include <QStackedWidget>

class QTabBar;
class QMenu;
class QToolButton;
class QLabel;
class QButtonGroup;
class SceneOutliner;
class DetailsPanel;
class Viewport2D;
class GameViewport;
class BlueprintEditor;
class BPRuntime;
class UIRuntime;
class UIEditor;
class UIDocument;
#include "ActorBPRuntime.h"
#include "models/BPClass.h"
class ContentBrowser;
class LayoutManager;
class DocTabBar;

namespace ads {
    class CDockManager;
    class CDockWidget;
}

class EditorWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit EditorWindow(const ProjectInfo& project, QWidget* parent = nullptr);

signals:
    void editorClosed();

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void setupMenuBar();
    void setupDocTabBar();
    void setupMainToolBar();
    void setupCentralArea();
    void setupBottomBar();
    void setupWindowMenu();
    void rebuildLayoutMenu();

    QWidget* buildViewportToolBar(QWidget* parent);
    QWidget* buildGameViewToolBar(QWidget* parent);
    void     updateGameViewToolbar(LevelDocument* doc);
    void     openLevelTab(const QString& path);
    void     saveCurrentLevel();
    void     saveAllLevels();
    void     updateTabTitle(int index);
    void     updateSaveLabel();
    void openBlueprintTab();
    void floatBlueprint(QPoint globalPos);
    void embedBlueprint();
    void openBpClassTab(const QString& bpClassPath);
    void openUIDocTab(const QString& uiFilePath);

private slots:
    void onTabChanged(int index);
    void onTabClosed(int index);
    void onProjectSettings();
    void startRuntime();
    void stopRuntime();
    void togglePauseRuntime();

private:
    ProjectInfo  m_project;

    DocTabBar* m_docTabBar = nullptr;

    Viewport2D*      m_viewport        = nullptr;
    GameViewport*    m_gameViewport   = nullptr;
    QWidget*         m_gameViewPage   = nullptr;
    QLabel*          m_gvCamNameLabel = nullptr;
    QLabel*          m_gvResLabel     = nullptr;
    SceneOutliner*   m_sceneOutliner   = nullptr;
    DetailsPanel*    m_detailsPanel    = nullptr;
    BlueprintEditor* m_blueprintEditor = nullptr;

    ads::CDockManager* m_dockManager  = nullptr;
    ads::CDockWidget*  m_viewportDock = nullptr;
    ads::CDockWidget*  m_outlineDockW = nullptr;
    ads::CDockWidget*  m_detailsDockW = nullptr;
    ads::CDockWidget*  m_cbDockW      = nullptr;
    ContentBrowser*    m_contentBrowser = nullptr;

    QStackedWidget* m_centralStack    = nullptr;
    QWidget*        m_viewportPage    = nullptr;
    QWidget*        m_bpWrapper       = nullptr;
    ads::CDockWidget* m_bpDockW          = nullptr;
    QTimer*           m_bpDropCheckTimer = nullptr;
    bool              m_bpDropFirstDone  = false;
    QString           m_activeLevelPath;

    LayoutManager* m_layoutManager = nullptr;
    QMenu*         m_windowMenu    = nullptr;
    QMenu*         m_layoutMenu    = nullptr;

    QButtonGroup* m_toolBtnGroup = nullptr;
    QToolButton*  m_runBtn       = nullptr;
    QToolButton*  m_pauseBtn     = nullptr;
    QToolButton*  m_stopBtn      = nullptr;
    QLabel*       m_saveLabel    = nullptr;

    // 视口辅助功能控件
    QList<QToolButton*> m_viewportAlignBtns;

    BPRuntime* m_runtime = nullptr;
    UIRuntime* m_uiRuntime = nullptr;
    UIEditor*  m_uiEditor  = nullptr;
    QList<ActorBPRuntime*>   m_actorRuntimes;
    QMap<QString, BPClass*>  m_openBpClasses;
    QMap<QString, UIDocument*> m_openUIDocs;
    float m_ppu = 100.0f;

    QMap<QString, LevelDocument*> m_openLevels;
    QSet<QString>                 m_dirtyBpClasses; // 记录已修改但未保存的 .bp 路径
    QList<QMetaObject::Connection> m_tabConnections;
};
