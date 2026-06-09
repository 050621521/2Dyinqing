#pragma once
#include "models/LevelDocument.h"
#include <QWidget>
#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QList>
#include <QString>

class QPainter;
class QFrame;

class BlueprintEditor : public QWidget {
    Q_OBJECT
public:
    explicit BlueprintEditor(QWidget* parent = nullptr);
    void loadLevel(LevelDocument* doc);

signals:
    void documentModified();

protected:
    void paintEvent(QPaintEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* e) override;

private:
    struct PinDef {
        QString key;
        QString label;
        bool    isExec;
        bool    isOutput;
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
    bool    m_panning = false;

    // 交互状态机
    enum class DragState { None, DraggingNode, DraggingWire };
    DragState m_dragState     = DragState::None;
    QString   m_selectedNodeId;
    QString   m_draggingNodeId;
    QPointF   m_dragOffset;

    // 连线拖拽
    QString  m_wireFromNode;
    QString  m_wireFromPin;
    bool     m_wireFromIsOutput = false;
    QPointF  m_wireCursorPos;

    // 拖线松开弹窗
    QFrame*  m_wireDropPopup    = nullptr;
    QPointF  m_wireDropCanvasPos;

    // 引脚值编辑弹窗
    QFrame*  m_paramEditPopup   = nullptr;
    QString  m_paramEditNodeId;
    QString  m_paramEditPinKey;

    LevelDocument* m_doc = nullptr;

    // 几何常量（画布单位）
    static constexpr float kNodeW   = 160.0f;
    static constexpr float kHeaderH = 24.0f;
    static constexpr float kRowH    = 22.0f;
    static constexpr float kPinR    = 5.0f;
    static constexpr float kPinSq   = 4.5f;

    // 坐标变换
    QPointF canvasToScreen(QPointF c) const;
    QPointF screenToCanvas(QPointF s) const;

    // 节点几何
    float   nodeHeight(const BPNode& node) const;
    QRectF  nodeRect(const BPNode& node) const;
    QPointF pinCenter(const BPNode& node, const QString& pinKey, bool isOutput) const;

    // 命中检测
    struct Hit {
        enum Type { None, Node, Pin, PinValue } type = None;
        QString nodeId;
        QString pinName;
        bool    pinIsOutput = false;
        bool    pinIsExec   = false;
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

    // 引脚值编辑弹窗
    void showParamEditPopup(QPoint screenPos, const QString& nodeId, const QString& pinKey);
    void hideParamEditPopup();
    void onParamValueConfirmed(const QString& value);
};
