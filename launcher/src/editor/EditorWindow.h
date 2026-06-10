#pragma once
#include "models/ProjectInfo.h"
#include "models/LevelDocument.h"
#include <QMainWindow>
#include <QMap>
#include <QButtonGroup>

class QTabBar;
class QStackedWidget;
class QDockWidget;
class SceneOutliner;
class DetailsPanel;
class Viewport2D;
class BlueprintEditor;
class BPRuntime;
class QToolButton;

class QLabel;

class EditorWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit EditorWindow(const ProjectInfo& project, QWidget* parent = nullptr);

signals:
    void editorClosed();

protected:
    void closeEvent(QCloseEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* e) override;

private:
    void setupMenuBar();
    void setupDocTabBar();
    void setupMainToolBar();
    void setupCentralArea();
    void setupBottomBar();
    void positionCBPanel();
    void positionBPDockedBar();
    void dockBlueprintAsTab();
    void undockBlueprintFromTab();

    QWidget* buildViewportToolBar(QWidget* parent);
    QWidget* buildPanelHeader(const QString& title, QWidget* parent);
    void     openLevelTab(const QString& path);
    void     saveCurrentLevel();
    void     saveAllLevels();
    void     updateTabTitle(int index);
    void     updateSaveLabel();

private slots:
    void onTabChanged(int index);
    void onTabClosed(int index);
    void onProjectSettings();
    void startRuntime();
    void stopRuntime();

private:
    ProjectInfo  m_project;
    QTabBar*     m_docTabBar           = nullptr;
    QWidget*     m_viewportWrap        = nullptr;
    QWidget*     m_contentBrowserPanel = nullptr;
    QLabel*      m_saveLabel           = nullptr;
    Viewport2D*      m_viewport          = nullptr;
    BlueprintEditor* m_blueprintEditor  = nullptr;
    QStackedWidget*  m_viewStack        = nullptr;
    SceneOutliner*   m_sceneOutliner    = nullptr;
    DetailsPanel*    m_detailsPanel     = nullptr;
    QButtonGroup*    m_toolBtnGroup     = nullptr;
    QToolButton*     m_runBtn           = nullptr;
    QToolButton*     m_stopBtn          = nullptr;
    BPRuntime*       m_runtime          = nullptr;
    QWidget*         m_bpPanel          = nullptr;
    QWidget*         m_bpTitleBar       = nullptr;
    QWidget*         m_bpDockedBar      = nullptr;
    bool             m_bpDragging       = false;
    QPoint           m_bpDragOffset;
    bool             m_bpDocked         = false;
    int              m_bpTabIndex       = -1;
    int              m_bpGhostTabIndex  = -1;
    QDockWidget* m_outlineDock  = nullptr;
    QDockWidget* m_detailsDock  = nullptr;
    QMap<QString, LevelDocument*> m_openLevels;
    QList<QMetaObject::Connection> m_tabConnections;
};
