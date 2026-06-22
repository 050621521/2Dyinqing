#pragma once
#include "models/ProjectInfo.h"
#include "models/LevelDocument.h"
#include <QMainWindow>
#include <QMap>
#include <QSet>
#include <QButtonGroup>
#include <QStackedWidget>
#include <QUndoStack>
#include <QElapsedTimer>

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
#include "GlobalVars.h"
class GlobalVarPanel;
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
    bool eventFilter(QObject* o, QEvent* e) override;   // 菜单栏尺寸变化时重定位录制按钮

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
    void     updateTabTooltip(int index);
    void     updateSaveLabel();
    void openBlueprintTab();
    void floatBp(const QString& tabId);
    void embedBp(const QString& tabId);
    void openBpClassTab(const QString& bpClassPath);
    void openUIDocTab(const QString& uiFilePath);

private slots:
    void onTabChanged(int index);
    void onTabClosed(int index);
    void onProjectSettings();
    void startRuntime();
    void stopRuntime();
    void togglePauseRuntime();
    void toggleRecording();        // 复现录制：开始/停止
    void positionRecordButton();   // 把录制按钮摆到“窗口”菜单右侧

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

    ads::CDockManager* m_dockManager  = nullptr;
    ads::CDockWidget*  m_viewportDock = nullptr;
    ads::CDockWidget*  m_outlineDockW = nullptr;
    ads::CDockWidget*  m_detailsDockW = nullptr;
    ads::CDockWidget*  m_cbDockW      = nullptr;
    ads::CDockWidget*  m_gvDock       = nullptr;
    ads::CDockWidget*  m_varDetailsDock = nullptr;  // 蓝图上下文的"细节"（变量属性）
    ads::CDockWidget*  m_localVarDock = nullptr;        // 局部变量列表（关卡蓝图）
    ads::CDockWidget*  m_localVarDetailsDock = nullptr; // 局部变量细节
    ContentBrowser*    m_contentBrowser = nullptr;

    QStackedWidget* m_centralStack    = nullptr;
    QWidget*        m_viewportPage    = nullptr;
    QString           m_activeLevelPath;

    // ── 多蓝图编辑器实例（关卡蓝图 + Actor .bp 蓝图统一管理）──────────────
    // 每个打开的蓝图各自拥有一个 BlueprintEditor 实例，常驻直到关闭。
    struct BpInstance {
        BlueprintEditor*  editor    = nullptr;  // 独立编辑器实例
        bool              isLevelBp = false;    // true=关卡蓝图，false=Actor .bp 蓝图
        QString           dataPath;             // 关卡蓝图=关卡路径；Actor 蓝图=.bp 路径
        ads::CDockWidget* dock      = nullptr;  // 非空=已浮动；nullptr=嵌入中央 stack
    };
    QMap<QString, BpInstance> m_bpInstances;    // key = tabId
    QTimer*           m_bpDropCheckTimer = nullptr;
    bool              m_bpDropFirstDone  = false;

    // 蓝图实例 helper
    static bool      isLevelBlueprintTab(const QString& tabData);
    static QString   levelPathOfBlueprintTab(const QString& tabData);
    static bool      isAnyBlueprintTab(const QString& tabData);  // 关卡蓝图 或 .bp
    BlueprintEditor* ensureBpInstance(const QString& tabId);     // 不存在则创建
    BlueprintEditor* activeBpEditor() const;                     // 当前聚焦/显示的蓝图
    void             destroyBpInstance(const QString& tabId);

    LayoutManager* m_layoutManager = nullptr;
    QMenu*         m_windowMenu    = nullptr;
    QMenu*         m_layoutMenu    = nullptr;

    // 复现录制
    QToolButton*  m_recordBtn     = nullptr;
    QTimer*       m_recordTimer    = nullptr;   // 录制中刷新计时显示
    QElapsedTimer m_recordClock;

    QButtonGroup* m_toolBtnGroup = nullptr;
    QToolButton*  m_runBtn       = nullptr;
    QToolButton*  m_pauseBtn     = nullptr;
    QToolButton*  m_stopBtn      = nullptr;
    QLabel*       m_saveLabel    = nullptr;

    // 视口辅助功能控件
    QList<QToolButton*> m_viewportAlignBtns;

    BPRuntime* m_runtime = nullptr;
    QStringList m_levelNavStack;   // 运行期关卡历史栈：跳转关卡时压栈，返回上一关时弹栈
    // 全局变量：声明（项目级）+ 运行时值（跨关卡保留、点运行清空）
    QList<GlobalVarDef>     m_globalVarDefs;
    QList<EnumDef>          m_enumDefs;
    QMap<QString, BPValue>  m_globalVars;
    GlobalVarPanel*         m_globalVarPanel = nullptr;
    GlobalVarPanel*         m_localVarPanel  = nullptr;
    class EnumEditor*       m_enumEditor = nullptr;   // 中央页签：枚举编辑
    void reloadGlobalVarDefs();   // 从 project.json 重读并推给所有蓝图编辑器
    void openEnumTab(const QString& enumPath);
    UIRuntime* m_uiRuntime = nullptr;
    UIEditor*  m_uiEditor  = nullptr;
    QList<ActorBPRuntime*>   m_actorRuntimes;
    // 跨上下文单键编辑快捷键（F/Esc/Delete/退格）：运行时挂起，避免抢占游戏按键
    QList<class QShortcut*>   m_editorSingleKeyShortcuts;
    QMap<QString, BPClass*>  m_openBpClasses;
    QMap<QString, UIDocument*> m_openUIDocs;
    float m_ppu = 100.0f;

    QMap<QString, LevelDocument*> m_openLevels;
    QSet<QString>                 m_dirtyBpClasses; // 记录已修改但未保存的 .bp 路径
    QList<QMetaObject::Connection> m_tabConnections;

    QUndoStack* m_activeUndoStack = nullptr;
    ActorData   m_detailsBeforeEdit;

    bool isTextInputFocused() const;
};
