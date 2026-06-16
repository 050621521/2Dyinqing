#pragma once
#include "models/LevelDocument.h"
#include "models/BPClass.h"
#include "models/BPMacro.h"
#include "GlobalVars.h"
#include <QWidget>
#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QList>
#include <QMap>
#include <QHash>
#include <QSet>
#include <QString>
#include <QUndoStack>

class QPainter;
class QFrame;
class QLineEdit;

class BlueprintEditor : public QWidget {
    Q_OBJECT
public:
    explicit BlueprintEditor(QWidget* parent = nullptr);
    // 全局变量声明（由 EditorWindow 在加载/面板变更时推入），用于节点类型/菜单
    void setGlobalVarDefs(const QList<GlobalVarDef>& defs) { m_globalVarDefs = defs; update(); }
    void setEnumDefs(const QList<EnumDef>& defs) { m_enumDefs = defs; update(); }
    void loadLevel(LevelDocument* doc);
    void loadBpClass(BPClass* bpClass);
    void saveBpClass();
    void setProjectRoot(const QString& root);
    QString currentBpClassPath() const;
    QUndoStack* bpUndoStack() const { return m_bpUndoStack; }
    void frameAll();
    void duplicateSelectedNode();
    void deleteSelected();

signals:
    void documentModified();
    void bpClassModified();

protected:
    void paintEvent(QPaintEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void showContextMenu(const QPoint& pos, const QPoint& globalPos);
    bool eventFilter(QObject* obj, QEvent* e) override;

private:
    // 引脚值类型：决定该引脚未连线时的内联编辑器形态（学虚幻，类型驱动）
    enum class ValueKind {
        Text,       // 自由文本/数值，原地 QLineEdit 打字
        Bool,       // 勾选框，点一下切换 true/false
        LevelRef,   // 下拉：项目 Levels 目录里的关卡
        ActorRef,   // 下拉：场景 Actor 列表（写回 id）
        UIRef,      // 下拉：项目 UI 文件（含控件展开，特殊弹窗）
        WidgetRef,  // 下拉：项目所有 UI 的控件（写回 "UI名::控件名"）
        EnumRef     // 下拉：某枚举的选项（选项按节点实例推导）
    };
    struct PinDef {
        QString   key;
        QString   label;
        bool      isExec;
        bool      isOutput;
        ValueKind kind = ValueKind::Text;   // 默认 Text，不破坏现有 4 字段初始化列表
    };
    struct NodeDef {
        QString        typeId;
        QString        displayName;
        QColor         headerColor;
        QList<PinDef>  pins;
    };
    static const QList<NodeDef>& nodeDefs();
    static const NodeDef*        findNodeDef(const QString& typeId);

    // 视图状态
    float   m_zoom   = 1.0f;
    QPointF m_offset = {0.0, 0.0};
    QPoint  m_lastMouse;
    QPoint  m_panStartPos;
    bool    m_panning = false;
    bool    m_panIsRight = false;   // 平移是否由右键发起（右键未拖动则弹菜单）

    // 交互状态机
    enum class DragState { None, DraggingNode, DraggingWire };
    DragState m_dragState     = DragState::None;
    QString   m_selectedNodeId;      // 主选中（细节展示 / 分支等单节点操作）
    QString   m_draggingNodeId;

    // 多选 + 框选
    QSet<QString> m_selectedNodeIds; // 全部选中（含主选中）
    bool          m_marquee = false; // 正在框选
    QPoint        m_marqueeStart;
    QPoint        m_marqueeCur;
    bool          m_marqueeAdditive = false;
    QList<BPNode> m_groupBefore;      // 整组拖拽起始快照（Undo 用）
    QPointF       m_groupDragLast;    // 上次画布坐标（增量移动）
    void selectSingleNode(const QString& id);
    void clearNodeSelection();
    // 折叠选中节点为一个本地自定义节点（宏）
    void foldSelectionToMacro();
    // 解开折叠：把一个自定义节点展开回其内部节点，外部连线接回
    void unfoldMacroNode(const QString& nodeId);
    // 提升为宏库资产：把本地折叠节点写成 .bpmacro 文件，改为库引用（跨蓝图复用）
    void promoteMacroToLibrary(const QString& nodeId);

    // 连线拖拽
    QString  m_wireFromNode;
    QString  m_wireFromPin;
    bool     m_wireFromIsOutput = false;
    QPointF  m_wireCursorPos;
    QString  m_selectedConnId;

    // 拖线松开弹窗
    QFrame*  m_wireDropPopup    = nullptr;
    QPointF  m_wireDropCanvasPos;

    // 引脚值内联编辑（原地 QLineEdit，不弹窗）
    QLineEdit* m_inlineEdit      = nullptr;
    QString    m_inlineEditNodeId;
    QString    m_inlineEditPinKey;
    // 非空 = 当前内联编辑写回的是分支控制某分支的比较值（而非 params[key]）
    QString    m_inlineSwitchBranchId;
    // actorId 选择仍用弹窗
    QFrame*  m_paramEditPopup   = nullptr;
    QString  m_paramEditNodeId;
    QString  m_paramEditPinKey;

    // 宏（自定义节点）：库资产缓存 + 当前正在编辑的宏（编辑子图时设置入口/出口引脚来源）
    mutable QHash<QString, BPMacro> m_macroCache;
    BPMacro* m_editingMacro = nullptr;
    // 进宏内部编辑：把子图装进临时 BPClass 复用编辑机制
    bool           m_inMacroEdit = false;
    BPClass        m_macroEditClass;   // 临时承载宏子图（nodes/connections）
    BPMacro        m_macroEditInfo;    // 宏接口（inputPins/outputPins）+ id/name/filePath
    QString        m_macroCallNodeId;  // 进入时所点的调用节点（本地折叠回写用）
    LevelDocument* m_returnDoc     = nullptr;
    BPClass*       m_returnBpClass = nullptr;
    void enterMacroEdit(const QString& nodeId);
    void exitMacroEdit();
    const BPMacro* findMacro(const QString& id) const;
    // 取某调用节点引用的宏接口（库资产 or 本地折叠子图）；成功返回 true
    bool macroInterface(const BPNode& node, QList<MacroPin>& ins,
                        QList<MacroPin>& outs) const;
    static ValueKind kindFromString(const QString& s);
    static QString   kindToString(ValueKind k);

    // 全局变量 / 枚举声明缓存
    QList<GlobalVarDef> m_globalVarDefs;
    QList<EnumDef>      m_enumDefs;
    QString   globalVarType(const QString& name) const;
    ValueKind kindFromGlobalType(const QString& type) const;   // enum:→EnumRef
    // 某引脚（全局变量值 / 分支比较值）对应的枚举选项；非枚举返回空
    QStringList enumValuesForPin(const BPNode& node, const QString& key) const;
    // 按节点实例求某引脚的 kind（兼容动态节点：分支控制/宏/全局变量）
    ValueKind pinKindForNode(const BPNode& node, const QString& key) const;

    // UI 资产选择器
    QString  m_projectRoot;
    QFrame*  m_uiAssetPopup     = nullptr;
    QString  m_uiAssetNodeId;
    mutable QMap<QString, QStringList> m_uiWidgetCache;
    QStringList loadWidgetNames(const QString& uiName) const;

    LevelDocument* m_doc     = nullptr;
    BPClass*       m_bpClass = nullptr;
    QUndoStack*    m_bpUndoStack = nullptr;

    const QList<BPNode>&       activeNodes() const;
    const QList<BPConnection>& activeConns() const;
    void notifyModified();
    bool isSelfNodeVisible(const QString& typeId) const;

    // 几何常量（画布单位）
    static constexpr float kNodeW   = 160.0f;
    static constexpr float kHeaderH = 24.0f;
    static constexpr float kRowH    = 22.0f;
    static constexpr float kPinR    = 5.0f;
    static constexpr float kPinSq   = 4.5f;

    // 坐标变换
    QPointF canvasToScreen(QPointF c) const;
    QPointF screenToCanvas(QPointF s) const;

    // 节点实例的有效引脚：普通节点=静态 def->pins；
    // 动态节点（后续 Flow.Switch / Macro:: 调用节点）按实例配置计算。
    // 单值分支与自定义节点共用的底层入口。
    QList<PinDef> effectivePins(const BPNode& node) const;

    // 节点几何
    float   nodeHeight(const BPNode& node) const;
    QRectF  nodeRect(const BPNode& node) const;
    QPointF pinCenter(const BPNode& node, const QString& pinKey, bool isOutput) const;

    // 命中检测
    struct Hit {
        // SwitchAdd/Del/Default/Value：分支控制节点上的可点区域（branchId 存于 pinName）
        enum Type { None, Node, Pin, PinValue, Wire,
                    SwitchAdd, SwitchDel, SwitchDefault, SwitchValue } type = None;
        QString nodeId;
        QString pinName;
        bool    pinIsOutput = false;
        bool    pinIsExec   = false;
        QString connId;   // for Wire hits
    };
    Hit hitTest(QPointF screenPos) const;

    // 辅助查询
    const BPNode* findNode(const QString& id) const;
    bool isPinConnected(const QString& nodeId, const QString& pinKey, bool isOutput) const;
    bool isPinExec(const QString& typeId, const QString& pinKey, bool isOutput) const;

    // 绘制
    void drawBackground(QPainter& p);
    void drawConnections(QPainter& p);
    void drawNodes(QPainter& p);
    void drawDanglingWire(QPainter& p);
    void drawNode(QPainter& p, const BPNode& node);
    void drawPin(QPainter& p, QPointF center, bool isExec, bool connected);
    void drawBezier(QPainter& p, QPointF from, QPointF to, bool isExec);

    // 拖线松开弹窗
    void showWireDropPopup(QPoint screenPos);
    void hideWireDropPopup();
    void onWireDropSelected(const QString& typeId, const QString& compatPin);

    // 引脚值内联编辑
    void showInlineEdit(const QString& nodeId, const QString& pinKey);
    void commitInlineEdit();
    void cancelInlineEdit();
    // 通用下拉列表选择器（actorId / levelName / widgetRef 等可枚举引脚共用）
    // items: (显示文本, 写回值) 列表
    void showListPicker(QPoint screenPos, const QString& nodeId, const QString& pinKey,
                        const QString& title,
                        const QList<QPair<QString, QString>>& items,
                        const QString& current);
    void hideParamEditPopup();
    void onParamValueConfirmed(const QString& value);
    // 按引脚 kind 分发内联编辑
    ValueKind pinKindOf(const QString& typeId, const QString& key) const;
    void toggleBoolParam(const QString& nodeId, const QString& pinKey);
    // 分支控制（Flow.Switch）编辑：增删分支 / 切换默认出口（均走 Undo）
    void addSwitchBranch(const QString& nodeId);
    void removeSwitchBranch(const QString& nodeId, const QString& branchId);
    void toggleSwitchDefault(const QString& nodeId);
    // 绑定/解绑枚举（空名=解绑）；绑定时为缺失的枚举值各生成一个分支
    void bindSwitchEnum(const QString& nodeId, const QString& enumName);
    // 各 kind 的下拉选项来源
    QList<QPair<QString, QString>> buildActorItems() const;
    QList<QPair<QString, QString>> buildLevelItems() const;
    QList<QPair<QString, QString>> buildWidgetItems() const;
    // UI 资产选择器
    void showUIAssetPicker(QPoint screenPos, const QString& nodeId);
    void hideUIAssetPicker();
};
