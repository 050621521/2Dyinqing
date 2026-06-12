#include "BlueprintEditor.h"
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QUuid>
#include <cmath>
#include <QFrame>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QListWidget>
#include <QTimer>

// ── bpClass/doc 统一读写辅助 ─────────────────────────────────────────

static void addNodeToActive(BPClass* bc, LevelDocument* doc, const BPNode& node) {
    if (bc) bc->nodes.append(node);
    else if (doc) doc->addBPNode(node);
}
static void removeNodeFromActive(BPClass* bc, LevelDocument* doc, const QString& id) {
    if (bc) bc->nodes.removeIf([&](const BPNode& n){ return n.id == id; });
    else if (doc) doc->removeBPNode(id);
}
static void updateNodeInActive(BPClass* bc, LevelDocument* doc, const BPNode& node) {
    if (bc) {
        for (BPNode& n : bc->nodes) if (n.id == node.id) { n = node; return; }
        qWarning() << "updateNodeInActive: node" << node.id << "not found in BPClass";
    } else if (doc) doc->updateBPNode(node);
}
static void addConnToActive(BPClass* bc, LevelDocument* doc, const BPConnection& conn) {
    if (bc) bc->connections.append(conn);
    else if (doc) doc->addBPConnection(conn);
}
static void removeConnFromActive(BPClass* bc, LevelDocument* doc, const QString& id) {
    if (bc) bc->connections.removeIf([&](const BPConnection& c){ return c.id == id; });
    else if (doc) doc->removeBPConnection(id);
}

// ── 节点类型注册 ──────────────────────────────────────────────────────

const QList<BlueprintEditor::NodeDef>& BlueprintEditor::nodeDefs() {
    static QList<NodeDef> defs = {
        {
            "Event.BeginPlay", "开始运行", QColor("#6a2a8a"),
            {
                {"exec_out", "exec", true, true}
            }
        },
        {
            "Event.KeyDown", "按键按下", QColor("#6a2a8a"),
            {
                {"exec_out", "exec", true,  true},
                {"key",      "key",  false, false}
            }
        },
        {
            "Action.Print", "打印字符串", QColor("#1a4a8a"),
            {
                {"exec_in",  "exec", true,  false},
                {"exec_out", "exec", true,  true},
                {"text",     "text", false, false}
            }
        },
        {
            "Action.MoveActor", "移动对象", QColor("#1a5a2a"),
            {
                {"exec_in",  "exec",    true,  false},
                {"exec_out", "exec",    true,  true},
                {"actorId",  "对象ID",  false, false},
                {"dx",       "X偏移",   false, false},
                {"dy",       "Y偏移",   false, false}
            }
        },
        {
            "Action.SetActive", "设置激活", QColor("#1a5a2a"),
            {
                {"exec_in",  "exec",   true,  false},
                {"exec_out", "exec",   true,  true},
                {"actorId",  "对象ID", false, false},
                {"active",   "激活",   false, false}
            }
        },
        {
            "Flow.Branch", "条件分支", QColor("#3a3a3a"),
            {
                {"exec_in",   "exec",      true,  false},
                {"true",      "true",      true,  true},
                {"false",     "false",     true,  true},
                {"condition", "条件",      false, false}
            }
        },
        {
            "Var.GetActorPos", "获取位置", QColor("#2a4a1a"),
            {
                {"actorId", "对象ID", false, false},
                {"x",       "X",      false, true},
                {"y",       "Y",      false, true}
            }
        },
        {
            "Var.ActorRef", "Actor引用", QColor("#4a2a0a"),
            {
                {"actorId", "对象ID", false, true}
            }
        },
        // ── Self 变换（所有 Actor）──────────────────────────────────────
        {
            "Self.GetPosition", "获取自身位置", QColor("#1a4a4a"),
            {{"x","X",false,true},{"y","Y",false,true}}
        },
        {
            "Self.SetPosition", "设置自身位置", QColor("#1a4a4a"),
            {{"exec_in","exec",true,false},{"exec_out","exec",true,true},
             {"x","X",false,false},{"y","Y",false,false}}
        },
        {
            "Self.GetRotation", "获取自身旋转", QColor("#1a4a4a"),
            {{"angle","角度",false,true}}
        },
        {
            "Self.SetRotation", "设置自身旋转", QColor("#1a4a4a"),
            {{"exec_in","exec",true,false},{"exec_out","exec",true,true},
             {"angle","角度",false,false}}
        },
        {
            "Self.IsActive", "获取激活状态", QColor("#1a4a4a"),
            {{"active","激活",false,true}}
        },
        {
            "Self.SetActive", "设置激活状态", QColor("#1a4a4a"),
            {{"exec_in","exec",true,false},{"exec_out","exec",true,true},
             {"active","激活",false,false}}
        },
        {
            "Self.GetName", "获取自身名称", QColor("#1a4a4a"),
            {{"name","名称",false,true}}
        },
        // ── Self 精灵渲染器 ─────────────────────────────────────────────
        {
            "Self.Sprite.SetImage", "设置精灵图片", QColor("#2a4a1a"),
            {{"exec_in","exec",true,false},{"exec_out","exec",true,true},
             {"path","路径",false,false}}
        },
        {
            "Self.Sprite.SetColor", "设置精灵颜色", QColor("#2a4a1a"),
            {{"exec_in","exec",true,false},{"exec_out","exec",true,true},
             {"r","R",false,false},{"g","G",false,false},
             {"b","B",false,false},{"a","A",false,false}}
        },
        {
            "Self.Sprite.SetFlipX", "水平翻转", QColor("#2a4a1a"),
            {{"exec_in","exec",true,false},{"exec_out","exec",true,true},
             {"flip","翻转",false,false}}
        },
        {
            "Self.Sprite.SetFlipY", "垂直翻转", QColor("#2a4a1a"),
            {{"exec_in","exec",true,false},{"exec_out","exec",true,true},
             {"flip","翻转",false,false}}
        },
        {
            "Self.Sprite.SetVisible", "设置精灵可见", QColor("#2a4a1a"),
            {{"exec_in","exec",true,false},{"exec_out","exec",true,true},
             {"visible","可见",false,false}}
        },
        // ── Self 摄像机 ─────────────────────────────────────────────────
        {
            "Self.Camera.SetSize", "设置摄像机尺寸", QColor("#1a2a4a"),
            {{"exec_in","exec",true,false},{"exec_out","exec",true,true},
             {"size","尺寸",false,false}}
        },
        {
            "Self.Camera.SetBackground", "设置背景色", QColor("#1a2a4a"),
            {{"exec_in","exec",true,false},{"exec_out","exec",true,true},
             {"r","R",false,false},{"g","G",false,false},{"b","B",false,false}}
        },
        // ── UI 节点 ──────────────────────────────────────────────────────────
        {
            "UI.Create", "创建UI", QColor("#4a1a6a"),
            {
                {"exec_in",    "exec",   true,  false},
                {"uiName",     "UI名称", false, false},
                {"exec_out",   "exec",   true,  true},
                {"instanceId", "UI引用", false, true},
            }
        },
        {
            "UI.Show", "显示UI", QColor("#1a4a6a"),
            {
                {"exec_in",    "exec",   true,  false},
                {"instanceId", "UI引用", false, false},
                {"exec_out",   "exec",   true,  true},
            }
        },
        {
            "UI.Hide", "隐藏UI", QColor("#1a4a6a"),
            {
                {"exec_in",    "exec",   true,  false},
                {"instanceId", "UI引用", false, false},
                {"exec_out",   "exec",   true,  true},
            }
        },
        {
            "UI.Destroy", "销毁UI", QColor("#6a1a1a"),
            {
                {"exec_in",    "exec",   true,  false},
                {"instanceId", "UI引用", false, false},
                {"exec_out",   "exec",   true,  true},
            }
        },
        {
            "UI.SetText", "设置文本", QColor("#1a4a6a"),
            {
                {"exec_in",    "exec",     true,  false},
                {"instanceId", "UI引用",   false, false},
                {"widgetName", "控件名",   false, false},
                {"text",       "文本内容", false, false},
                {"exec_out",   "exec",     true,  true},
            }
        },
        {
            "UI.SetValue", "设置进度值", QColor("#1a4a6a"),
            {
                {"exec_in",    "exec",   true,  false},
                {"instanceId", "UI引用", false, false},
                {"widgetName", "控件名", false, false},
                {"value",      "数值",   false, false},
                {"exec_out",   "exec",   true,  true},
            }
        },
        {
            "UI.SetPosition", "设置UI位置", QColor("#1a4a6a"),
            {
                {"exec_in",    "exec",   true,  false},
                {"instanceId", "UI引用", false, false},
                {"x",          "X",      false, false},
                {"y",          "Y",      false, false},
                {"exec_out",   "exec",   true,  true},
            }
        },
        {
            "UI.SetVisible", "设置控件可见", QColor("#1a4a6a"),
            {
                {"exec_in",    "exec",   true,  false},
                {"instanceId", "UI引用", false, false},
                {"widgetName", "控件名", false, false},
                {"visible",    "可见",   false, false},
                {"exec_out",   "exec",   true,  true},
            }
        },
        {
            "UI.Ref", "UI引用变量", QColor("#4a2a6a"),
            {
                {"instanceId", "UI引用", false, true},
            }
        },
        {
            "UI.OnButtonClick", "按钮点击时", QColor("#6a2a8a"),
            {
                {"instanceId", "UI引用", false, false},
                {"widgetName", "控件名", false, false},
                {"exec_out",   "exec",   true,  true},
            }
        },
        {
            "UI.OnDropdownChanged", "下拉选项改变时", QColor("#6a2a8a"),
            {
                {"instanceId", "UI引用",   false, false},
                {"widgetName", "控件名",   false, false},
                {"exec_out",   "exec",     true,  true},
                {"index",      "选中索引", false, true},
            }
        }
    };
    return defs;
}

const BlueprintEditor::NodeDef* BlueprintEditor::findNodeDef(const QString& typeId) {
    for (const NodeDef& def : nodeDefs()) {
        if (def.typeId == typeId) return &def;
    }
    return nullptr;
}

// ── 构造 ──────────────────────────────────────────────────────────────

BlueprintEditor::BlueprintEditor(QWidget* parent) : QWidget(parent) {
    setObjectName("blueprintEditor");
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMouseTracking(true);
}

void BlueprintEditor::loadLevel(LevelDocument* doc) {
    m_bpClass = nullptr;
    m_doc = doc;
    m_selectedNodeId.clear();
    m_dragState = DragState::None;
    hideWireDropPopup();
    cancelInlineEdit();
    update();
}

// ── 坐标变换 ──────────────────────────────────────────────────────────

QPointF BlueprintEditor::canvasToScreen(QPointF c) const {
    QPointF center(width() / 2.0 + m_offset.x(), height() / 2.0 + m_offset.y());
    return center + c * m_zoom;
}

QPointF BlueprintEditor::screenToCanvas(QPointF s) const {
    QPointF center(width() / 2.0 + m_offset.x(), height() / 2.0 + m_offset.y());
    return (s - center) / m_zoom;
}

// ── 节点几何 ──────────────────────────────────────────────────────────

float BlueprintEditor::nodeHeight(const BPNode& node) const {
    const NodeDef* def = findNodeDef(node.type);
    int pinCount = def ? def->pins.size() : 1;
    int extraRows = (node.type == "Var.ActorRef") ? 1 : 0;
    return kHeaderH + kRowH * qMax(1, pinCount + extraRows) + 4.0f;
}

QRectF BlueprintEditor::nodeRect(const BPNode& node) const {
    return QRectF(node.x, node.y, kNodeW, nodeHeight(node));
}

QPointF BlueprintEditor::pinCenter(const BPNode& node, const QString& pinKey, bool isOutput) const {
    const NodeDef* def = findNodeDef(node.type);
    if (!def) return {};
    QPointF topLeftScreen = canvasToScreen({node.x, node.y});
    int extraRows = (node.type == "Var.ActorRef") ? 1 : 0;
    int row = 0;
    for (const PinDef& pd : def->pins) {
        if (pd.key == pinKey && pd.isOutput == isOutput) {
            float cy = (float)(topLeftScreen.y() + (kHeaderH + (row + extraRows) * kRowH + kRowH * 0.5f) * m_zoom);
            float cx = isOutput
                ? (float)(topLeftScreen.x() + kNodeW * m_zoom - 10.0f)
                : (float)(topLeftScreen.x() + 10.0f);
            return {cx, cy};
        }
        ++row;
    }
    return {};
}

// ── 辅助查询 ──────────────────────────────────────────────────────────

const BPNode* BlueprintEditor::findNode(const QString& id) const {
    for (const BPNode& n : activeNodes())
        if (n.id == id) return &n;
    return nullptr;
}

bool BlueprintEditor::isPinConnected(const QString& nodeId, const QString& pinKey, bool isOutput) const {
    for (const BPConnection& c : activeConns()) {
        if (isOutput  && c.fromNode == nodeId && c.fromPin == pinKey) return true;
        if (!isOutput && c.toNode   == nodeId && c.toPin   == pinKey) return true;
    }
    return false;
}

bool BlueprintEditor::isPinExec(const QString& typeId, const QString& pinKey, bool isOutput) const {
    const NodeDef* def = findNodeDef(typeId);
    if (!def) return false;
    for (const PinDef& pd : def->pins)
        if (pd.key == pinKey && pd.isOutput == isOutput) return pd.isExec;
    return false;
}

// ── 命中检测 ──────────────────────────────────────────────────────────

BlueprintEditor::Hit BlueprintEditor::hitTest(QPointF screenPos) const {
    if (!m_doc && !m_bpClass) return {};
    // 逆序遍历（后添加的在最上层）
    const auto& nodes = activeNodes();
    for (int i = nodes.size() - 1; i >= 0; --i) {
        const BPNode& node = nodes[i];
        const NodeDef* def = findNodeDef(node.type);
        if (!def) continue;

        int row = 0;
        for (const PinDef& pd : def->pins) {
            QPointF pc = pinCenter(node, pd.key, pd.isOutput);
            float hitR = (pd.isExec ? kPinR : kPinSq) * m_zoom + 4.0f;
            QPointF delta = screenPos - pc;
            if (delta.x() * delta.x() + delta.y() * delta.y() <= hitR * hitR) {
                Hit h;
                h.type        = Hit::Pin;
                h.nodeId      = node.id;
                h.pinName     = pd.key;
                h.pinIsOutput = pd.isOutput;
                h.pinIsExec   = pd.isExec;
                return h;
            }
            ++row;
        }
    }
    // PinValue 命中：ActorRef 选择器 + 普通数据输入引脚值区域
    const auto& nodes2 = activeNodes();
    for (int i = nodes2.size() - 1; i >= 0; --i) {
        const BPNode& node = nodes2[i];
        const NodeDef* def = findNodeDef(node.type);
        if (!def) continue;
        QPointF tl = canvasToScreen({node.x, node.y});
        float nw = kNodeW * m_zoom;

        // Var.ActorRef：选择器按钮占据第 0 行（紧接 header）
        if (node.type == "Var.ActorRef") {
            float rowY = tl.y() + kHeaderH * m_zoom;
            float rowH = kRowH * m_zoom;
            if (screenPos.x() >= tl.x() + 4*m_zoom && screenPos.x() <= tl.x() + nw - 4*m_zoom &&
                screenPos.y() >= rowY && screenPos.y() <= rowY + rowH) {
                Hit h;
                h.type    = Hit::PinValue;
                h.nodeId  = node.id;
                h.pinName = "actorId";
                return h;
            }
            continue; // ActorRef 无数据输入引脚，跳过后续检测
        }

        // 其他节点：检测未连接的非 actorId 数据输入引脚
        int extraRows = 0;
        int row = 0;
        for (const PinDef& pd : def->pins) {
            if (!pd.isExec && !pd.isOutput && pd.key != "actorId"
                && !isPinConnected(node.id, pd.key, false)) {
                QPointF pc = pinCenter(node, pd.key, false);
                float rowY = tl.y() + (kHeaderH + (row + extraRows) * kRowH) * m_zoom;
                float rowH = kRowH * m_zoom;
                float valX0 = (float)(pc.x() + 10.0);
                float valX1 = (float)(tl.x() + nw - 6.0);
                if (screenPos.x() >= valX0 && screenPos.x() <= valX1 &&
                    screenPos.y() >= rowY  && screenPos.y() <= rowY + rowH) {
                    Hit h;
                    h.type    = Hit::PinValue;
                    h.nodeId  = node.id;
                    h.pinName = pd.key;
                    return h;
                }
            }
            ++row;
        }
    }

    for (int i = nodes.size() - 1; i >= 0; --i) {
        const BPNode& node = nodes[i];
        QPointF tl = canvasToScreen({node.x, node.y});
        float nw = kNodeW * m_zoom;
        float nh = nodeHeight(node) * m_zoom;
        if (screenPos.x() >= tl.x() && screenPos.x() <= tl.x() + nw &&
            screenPos.y() >= tl.y() && screenPos.y() <= tl.y() + nh) {
            Hit h;
            h.type   = Hit::Node;
            h.nodeId = node.id;
            return h;
        }
    }
    return {};
}

// ── 绘制 ──────────────────────────────────────────────────────────────

void BlueprintEditor::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    p.fillRect(rect(), QColor(0x16, 0x16, 0x16));
    drawBackground(p);
    drawConnections(p);
    drawNodes(p);
    drawDanglingWire(p);
}

void BlueprintEditor::drawBackground(QPainter& p) {
    const float baseStep = 20.0f;
    float step = baseStep * m_zoom;
    while (step < 10.0f) step *= 2.0f;
    while (step > 60.0f) step /= 2.0f;

    QPointF origin = canvasToScreen({0, 0});
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x2a, 0x2a, 0x2a));

    float startX = std::fmod((float)origin.x(), step);
    if (startX < 0) startX += step;
    float startY = std::fmod((float)origin.y(), step);
    if (startY < 0) startY += step;

    for (float x = startX; x < width(); x += step)
        for (float y = startY; y < height(); y += step)
            p.drawEllipse(QPointF(x, y), 1.2, 1.2);
}

void BlueprintEditor::drawBezier(QPainter& p, QPointF from, QPointF to, bool isExec) {
    const QColor color = isExec ? QColor(0xe0, 0x7a, 0x30) : QColor(0xcc, 0xcc, 0xcc);
    p.setPen(QPen(color, qMax(1.5, 2.0 * m_zoom), Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);

    float dx = qMax(80.0f * (float)m_zoom, (float)std::fabs(to.x() - from.x()) * 0.5f);
    QPainterPath path;
    path.moveTo(from);
    path.cubicTo(from + QPointF(dx, 0), to - QPointF(dx, 0), to);
    p.drawPath(path);
}

void BlueprintEditor::drawConnections(QPainter& p) {
    if (!m_doc && !m_bpClass) return;
    for (const BPConnection& conn : activeConns()) {
        const BPNode* fromNode = findNode(conn.fromNode);
        const BPNode* toNode   = findNode(conn.toNode);
        if (!fromNode || !toNode) continue;

        QPointF from = pinCenter(*fromNode, conn.fromPin, true);
        QPointF to   = pinCenter(*toNode,   conn.toPin,   false);
        bool isExec  = isPinExec(fromNode->type, conn.fromPin, true);
        drawBezier(p, from, to, isExec);
    }
}

void BlueprintEditor::drawPin(QPainter& p, QPointF center, bool isExec, bool connected) {
    const QColor execColor(0xe0, 0x7a, 0x30);
    const QColor dataColor(0xcc, 0xcc, 0xcc);
    const QColor emptyBg(0x22, 0x22, 0x22);

    if (isExec) {
        float r = kPinR * (float)m_zoom;
        p.setPen(QPen(execColor, 1.5));
        p.setBrush(connected ? execColor : emptyBg);
        p.drawEllipse(center, r, r);
    } else {
        float half = kPinSq * (float)m_zoom;
        QRectF sq(center.x() - half, center.y() - half, half * 2.0f, half * 2.0f);
        p.setPen(QPen(dataColor, 1.5));
        p.setBrush(connected ? dataColor : emptyBg);
        p.drawRect(sq);
    }
}

void BlueprintEditor::drawNode(QPainter& p, const BPNode& node) {
    const NodeDef* def = findNodeDef(node.type);
    if (!def) return;

    QPointF tl = canvasToScreen({node.x, node.y});
    float nw = kNodeW * (float)m_zoom;
    float nh = nodeHeight(node) * (float)m_zoom;
    float hh = kHeaderH * (float)m_zoom;
    float radius = 6.0f * (float)m_zoom;

    QRectF nodeRc(tl.x(), tl.y(), nw, nh);

    // 节点背景
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x22, 0x22, 0x22));
    p.drawRoundedRect(nodeRc, radius, radius);

    // 表头（仅顶部两角圆角）
    QRectF headerRc(tl.x(), tl.y(), nw, hh);
    QPainterPath headerPath;
    headerPath.moveTo(headerRc.bottomLeft());
    headerPath.lineTo(headerRc.topLeft() + QPointF(0, radius));
    headerPath.quadTo(headerRc.topLeft(), headerRc.topLeft() + QPointF(radius, 0));
    headerPath.lineTo(headerRc.topRight() - QPointF(radius, 0));
    headerPath.quadTo(headerRc.topRight(), headerRc.topRight() + QPointF(0, radius));
    headerPath.lineTo(headerRc.bottomRight());
    headerPath.closeSubpath();
    p.setBrush(def->headerColor);
    p.drawPath(headerPath);

    // 色块图标
    float iconSz = 8.0f * (float)m_zoom;
    float iconX  = tl.x() + 6.0f * (float)m_zoom;
    float iconY  = tl.y() + (hh - iconSz) * 0.5f;
    QColor iconColor = def->headerColor.lighter(150);
    p.setBrush(iconColor);
    p.drawRect(QRectF(iconX, iconY, iconSz, iconSz));

    // 节点标题文字
    float fontSize = qMax(8.0, 10.0 * m_zoom);
    QFont font; font.setPointSizeF(fontSize); font.setBold(false);
    p.setFont(font);
    p.setPen(QColor(0xff, 0xff, 0xff));
    float textX = iconX + iconSz + 5.0f * (float)m_zoom;
    p.drawText(QRectF(textX, tl.y(), nw - (textX - tl.x()) - 4, hh),
               Qt::AlignVCenter | Qt::AlignLeft, def->displayName);

    // 分隔线
    p.setPen(QPen(QColor(0x33, 0x33, 0x33), 1.0));
    p.drawLine(QPointF(tl.x(), tl.y() + hh), QPointF(tl.x() + nw, tl.y() + hh));

    // Var.ActorRef：选择器按钮（在 pin 行上方的额外行）
    if (node.type == "Var.ActorRef") {
        QString actorName = "(点击选择对象)";
        QString actorId = node.params.value("actorId");
        bool hasActor = !actorId.isEmpty();
        if (hasActor && m_doc && !m_bpClass) {
            for (const ActorData& a : m_doc->actors())
                if (a.id == actorId) { actorName = a.name; break; }
        }
        float btnRowY = tl.y() + kHeaderH * (float)m_zoom;
        float btnRowH = kRowH * (float)m_zoom;
        QRectF btnRc(tl.x() + 6.0f*(float)m_zoom, btnRowY + 3.0f*(float)m_zoom,
                     nw - 12.0f*(float)m_zoom, btnRowH - 6.0f*(float)m_zoom);
        p.setBrush(QColor(0x33, 0x28, 0x18));
        p.setPen(QPen(hasActor ? QColor(0x5a, 0x9f, 0xd4) : QColor(0x55, 0x44, 0x33), 1.0));
        p.drawRoundedRect(btnRc, 3.0*(float)m_zoom, 3.0*(float)m_zoom);
        QFont bf; bf.setPointSizeF(qMax(7.0, 9.0 * m_zoom));
        p.setFont(bf);
        p.setPen(hasActor ? QColor(0x5a, 0x9f, 0xd4) : QColor(0x77, 0x66, 0x55));
        p.drawText(btnRc, Qt::AlignCenter, actorName);
    }

    // Pin 行
    font.setBold(false);
    float pinFontSize = qMax(7.0, 9.0 * m_zoom);
    font.setPointSizeF(pinFontSize);
    p.setFont(font);

    int extraRows = (node.type == "Var.ActorRef") ? 1 : 0;
    int row = 0;
    for (const PinDef& pd : def->pins) {
        QPointF pc = pinCenter(node, pd.key, pd.isOutput);
        bool connected = isPinConnected(node.id, pd.key, pd.isOutput);
        drawPin(p, pc, pd.isExec, connected);

        // 标签
        p.setPen(QColor(0xaa, 0xaa, 0xaa));
        float rowY = tl.y() + (kHeaderH + (row + extraRows) * kRowH) * (float)m_zoom;
        float rowH = kRowH * (float)m_zoom;
        if (pd.isOutput) {
            QRectF labelRc(tl.x(), rowY, nw - 18.0f * (float)m_zoom, rowH);
            p.drawText(labelRc, Qt::AlignVCenter | Qt::AlignRight, pd.label);
        } else {
            QRectF labelRc(tl.x() + 18.0f * (float)m_zoom, rowY, nw - 18.0f * (float)m_zoom, rowH);
            p.drawText(labelRc, Qt::AlignVCenter | Qt::AlignLeft, pd.label);

            // 未连接的非 actorId 数据输入引脚：右侧显示当前参数值
            if (!pd.isExec && pd.key != "actorId" && !isPinConnected(node.id, pd.key, false)) {
                QString val = node.params.value(pd.key);
                QString display = val.isEmpty() ? "···" : val;
                p.setPen(val.isEmpty() ? QColor(0x55, 0x55, 0x55) : QColor(0x5a, 0x9f, 0xd4));
                QFont vf; vf.setPointSizeF(qMax(7.0, 8.5 * m_zoom));
                p.setFont(vf);
                QRectF valRc(tl.x() + 16.0f * (float)m_zoom, rowY, nw - 22.0f * (float)m_zoom, rowH);
                p.drawText(valRc, Qt::AlignVCenter | Qt::AlignRight, display);
                p.setFont(font); // 恢复 pin 字体
            }
        }
        ++row;
    }

    // 选中高亮（最后绘制，保证在所有内容上方）
    if (node.id == m_selectedNodeId) {
        p.setPen(QPen(QColor(0xff, 0xff, 0xff), 2.0));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(nodeRc.adjusted(-3, -3, 3, 3), radius + 2, radius + 2);
    }
}

void BlueprintEditor::drawNodes(QPainter& p) {
    if (!m_doc && !m_bpClass) return;
    for (const BPNode& node : activeNodes())
        drawNode(p, node);
}

void BlueprintEditor::drawDanglingWire(QPainter& p) {
    if (m_dragState != DragState::DraggingWire) return;
    const BPNode* fromNode = findNode(m_wireFromNode);
    if (!fromNode) return;

    QPointF from = pinCenter(*fromNode, m_wireFromPin, m_wireFromIsOutput);
    QPointF to   = canvasToScreen(m_wireCursorPos);
    bool isExec  = isPinExec(fromNode->type, m_wireFromPin, m_wireFromIsOutput);

    if (!m_wireFromIsOutput) qSwap(from, to);
    drawBezier(p, from, to, isExec);
}

// ── 滚轮缩放 ──────────────────────────────────────────────────────────

void BlueprintEditor::wheelEvent(QWheelEvent* e) {
    cancelInlineEdit();
    const float factor = e->angleDelta().y() > 0 ? 1.15f : (1.0f / 1.15f);
    float newZoom = qBound(0.1f, m_zoom * factor, 3.0f);
    // 以鼠标为中心缩放
    QPointF mouse = e->position();
    QPointF canvasBefore = screenToCanvas(mouse);
    m_zoom = newZoom;
    QPointF screenAfter = canvasToScreen(canvasBefore);
    m_offset += mouse - screenAfter;
    update();
}

// ── 鼠标事件 ──────────────────────────────────────────────────────────

void BlueprintEditor::mousePressEvent(QMouseEvent* e) {
    // 内联编辑框激活时，点击外部提交
    if (m_inlineEdit && m_inlineEdit->isVisible()) {
        if (!m_inlineEdit->geometry().contains(e->pos()))
            commitInlineEdit();
    }
    // 弹窗显示时，点击弹窗外部关闭它
    if (m_wireDropPopup && m_wireDropPopup->isVisible()) {
        if (!m_wireDropPopup->geometry().contains(e->pos())) {
            hideWireDropPopup();
            m_wireFromNode.clear();
            m_wireFromPin.clear();
        }
        return;
    }
    if (m_paramEditPopup && m_paramEditPopup->isVisible()) {
        if (!m_paramEditPopup->geometry().contains(e->pos())) {
            hideParamEditPopup();
        }
        return;
    }
    setFocus();
    if (e->button() == Qt::MiddleButton) {
        m_panning   = true;
        m_lastMouse = e->pos();
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (e->button() != Qt::LeftButton) return;

    Hit hit = hitTest(e->position());

    // 点中节点上的任意位置（含 pin/value 区域）都算选中该节点
    if (hit.type == Hit::Pin || hit.type == Hit::Node || hit.type == Hit::PinValue)
        m_selectedNodeId = hit.nodeId;

    // 点击引脚值区域
    if (hit.type == Hit::PinValue) {
        if (hit.pinName == "actorId") {
            // actorId 仍用弹窗（需要 Actor 列表选择器）
            showParamEditPopup(e->pos(), hit.nodeId, hit.pinName);
        } else {
            // 数值/文本 pin → 原地内联编辑，不弹窗
            showInlineEdit(hit.nodeId, hit.pinName);
        }
        update();
        return;
    }

    if (hit.type == Hit::Pin) {
        if (hit.pinIsOutput) {
            // 从输出 pin 开始连线
            m_dragState         = DragState::DraggingWire;
            m_wireFromNode      = hit.nodeId;
            m_wireFromPin       = hit.pinName;
            m_wireFromIsOutput  = true;
            m_wireCursorPos     = screenToCanvas(e->position());
        } else {
            // 从输入 pin 开始：若已有连接，断开旧连接并从旧的输出端开始拖拽
            if (!m_doc && !m_bpClass) return;
            QString oldConnId, oldFromNode, oldFromPin;
            for (const BPConnection& c : activeConns()) {
                if (c.toNode == hit.nodeId && c.toPin == hit.pinName) {
                    oldConnId   = c.id;
                    oldFromNode = c.fromNode;
                    oldFromPin  = c.fromPin;
                    break;
                }
            }
            if (!oldConnId.isEmpty()) {
                removeConnFromActive(m_bpClass, m_doc, oldConnId);
                notifyModified();
                m_dragState        = DragState::DraggingWire;
                m_wireFromNode     = oldFromNode;
                m_wireFromPin      = oldFromPin;
                m_wireFromIsOutput = true;
                m_wireCursorPos    = screenToCanvas(e->position());
            } else {
                // 未连接的输入 pin，从该 pin 向外拖拽（方向输入→输出）
                m_dragState        = DragState::DraggingWire;
                m_wireFromNode     = hit.nodeId;
                m_wireFromPin      = hit.pinName;
                m_wireFromIsOutput = false;
                m_wireCursorPos    = screenToCanvas(e->position());
            }
        }
        update();
        return;
    }

    if (hit.type == Hit::Node) {
        m_selectedNodeId  = hit.nodeId;
        m_dragState       = DragState::DraggingNode;
        m_draggingNodeId  = hit.nodeId;
        const BPNode* n = findNode(hit.nodeId);
        if (n) m_dragOffset = screenToCanvas(e->position()) - QPointF(n->x, n->y);
        update();
        return;
    }

    // 点击空白
    m_selectedNodeId.clear();
    update();
}

void BlueprintEditor::mouseMoveEvent(QMouseEvent* e) {
    if (m_panning) {
        QPoint delta = e->pos() - m_lastMouse;
        m_offset   += delta;
        m_lastMouse = e->pos();
        update();
        return;
    }
    if (m_dragState == DragState::DraggingNode && (m_doc || m_bpClass)) {
        QPointF canvasPos = screenToCanvas(e->position()) - m_dragOffset;
        // 直接修改节点坐标（临时，不标脏）
        if (m_bpClass) {
            for (BPNode& n : m_bpClass->nodes) {
                if (n.id == m_draggingNodeId) {
                    n.x = (float)canvasPos.x();
                    n.y = (float)canvasPos.y();
                    break;
                }
            }
        } else if (m_doc) {
            for (BPNode& n : const_cast<QList<BPNode>&>(m_doc->bpNodes())) {
                if (n.id == m_draggingNodeId) {
                    n.x = (float)canvasPos.x();
                    n.y = (float)canvasPos.y();
                    break;
                }
            }
        }
        update();
        return;
    }
    if (m_dragState == DragState::DraggingWire) {
        m_wireCursorPos = screenToCanvas(e->position());
        update();
    }
}

void BlueprintEditor::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::MiddleButton) {
        m_panning = false;
        setCursor(Qt::ArrowCursor);
        return;
    }
    if (e->button() != Qt::LeftButton) return;

    if (m_dragState == DragState::DraggingNode && (m_doc || m_bpClass)) {
        // 正式提交节点位置
        const BPNode* n = findNode(m_draggingNodeId);
        if (n) {
            updateNodeInActive(m_bpClass, m_doc, *n);
            notifyModified();
        }
        m_dragState = DragState::None;
        m_draggingNodeId.clear();
        update();
        return;
    }

    if (m_dragState == DragState::DraggingWire && (m_doc || m_bpClass)) {
        Hit hit = hitTest(e->position());
        if (hit.type == Hit::Pin && hit.nodeId != m_wireFromNode
            && hit.pinIsOutput != m_wireFromIsOutput)
        {
            // 确定方向：from=输出端，to=输入端
            QString fromNode = m_wireFromIsOutput ? m_wireFromNode : hit.nodeId;
            QString fromPin  = m_wireFromIsOutput ? m_wireFromPin  : hit.pinName;
            QString toNode   = m_wireFromIsOutput ? hit.nodeId     : m_wireFromNode;
            QString toPin    = m_wireFromIsOutput ? hit.pinName    : m_wireFromPin;

            // 类型兼容校验（exec-exec 或 data-data）
            const BPNode* fromNodePtr = findNode(fromNode);
            const BPNode* toNodePtr   = findNode(toNode);
            bool typeOk = false;
            if (fromNodePtr && toNodePtr) {
                bool fromExec = isPinExec(fromNodePtr->type, fromPin, true);
                bool toExec   = isPinExec(toNodePtr->type,   toPin,   false);
                typeOk = (fromExec == toExec);
            }

            if (typeOk) {
                // 若目标输入 pin 已有连接，先删除
                for (const BPConnection& c : activeConns()) {
                    if (c.toNode == toNode && c.toPin == toPin) {
                        removeConnFromActive(m_bpClass, m_doc, c.id);
                        break;
                    }
                }
                BPConnection conn;
                conn.id       = QUuid::createUuid().toString(QUuid::WithoutBraces);
                conn.fromNode = fromNode;
                conn.fromPin  = fromPin;
                conn.toNode   = toNode;
                conn.toPin    = toPin;
                addConnToActive(m_bpClass, m_doc, conn);
                notifyModified();
            }
            m_dragState = DragState::None;
            update();
            return;
        }

        // 落到空白处 → 显示节点创建弹窗
        if (hit.type == Hit::None) {
            showWireDropPopup(e->pos());
            return;
        }

        m_dragState = DragState::None;
        update();
        return;
    }

    m_dragState = DragState::None;
}

// ── 键盘事件 ──────────────────────────────────────────────────────────

void BlueprintEditor::keyPressEvent(QKeyEvent* e) {
    if ((e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace)
        && !m_selectedNodeId.isEmpty() && (m_doc || m_bpClass)) {
        // 先删除所有关联连接
        QStringList toRemove;
        for (const BPConnection& c : activeConns())
            if (c.fromNode == m_selectedNodeId || c.toNode == m_selectedNodeId)
                toRemove.append(c.id);
        for (const QString& id : toRemove)
            removeConnFromActive(m_bpClass, m_doc, id);
        removeNodeFromActive(m_bpClass, m_doc, m_selectedNodeId);
        m_selectedNodeId.clear();
        notifyModified();
        update();
    }
}

// ── 右键菜单 ──────────────────────────────────────────────────────────

void BlueprintEditor::contextMenuEvent(QContextMenuEvent* e) {
    if (!m_doc && !m_bpClass) return;
    QPointF canvasPos = screenToCanvas(e->pos());

    QMenu menu(this);
    auto* eventMenu  = menu.addMenu("事件");
    auto* actionMenu = menu.addMenu("动作");
    auto* flowMenu   = menu.addMenu("流程控制");
    auto* varMenu    = menu.addMenu("变量");
    auto* selfMenu   = menu.addMenu("Self");
    auto* uiMenu     = menu.addMenu("UI");

    for (const NodeDef& def : nodeDefs()) {
        if (!isSelfNodeVisible(def.typeId)) continue;
        QMenu* target = def.typeId.startsWith("Event.")  ? eventMenu  :
                        def.typeId.startsWith("Action.") ? actionMenu :
                        def.typeId.startsWith("Flow.")   ? flowMenu   :
                        def.typeId.startsWith("UI.")     ? uiMenu     :
                        def.typeId.startsWith("Self.")   ? selfMenu   : varMenu;
        const QString typeId = def.typeId;
        target->addAction(def.displayName, [this, typeId, canvasPos]() {
            if (!m_doc && !m_bpClass) return;
            BPNode node;
            node.id   = QUuid::createUuid().toString(QUuid::WithoutBraces);
            node.type = typeId;
            node.x    = (float)canvasPos.x();
            node.y    = (float)canvasPos.y();
            addNodeToActive(m_bpClass, m_doc, node);
            m_selectedNodeId = node.id;
            notifyModified();
            update();
        });
    }
    menu.exec(e->globalPos());
}

// ── eventFilter（捕获弹窗内 Escape）────────────────────────────────────

bool BlueprintEditor::eventFilter(QObject* obj, QEvent* e) {
    if (e->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(e);
        if (ke->key() == Qt::Key_Escape) {
            if (m_inlineEdit && m_inlineEdit->isVisible()) {
                cancelInlineEdit();
                return true;
            }
            if (m_wireDropPopup && m_wireDropPopup->isVisible()) {
                hideWireDropPopup();
                m_wireFromNode.clear();
                m_wireFromPin.clear();
                return true;
            }
            if (m_paramEditPopup && m_paramEditPopup->isVisible()) {
                hideParamEditPopup();
                return true;
            }
        }
    }
    // 内联编辑框失焦时提交
    if (obj == m_inlineEdit && e->type() == QEvent::FocusOut)
        QTimer::singleShot(0, this, &BlueprintEditor::commitInlineEdit);
    return QWidget::eventFilter(obj, e);
}

// ── 拖线松开弹窗 ──────────────────────────────────────────────────────

void BlueprintEditor::showWireDropPopup(QPoint screenPos) {
    hideWireDropPopup();

    m_wireDropCanvasPos = m_wireCursorPos;

    const BPNode* fromNodePtr = findNode(m_wireFromNode);
    if (!fromNodePtr) {
        m_dragState = DragState::None;
        update();
        return;
    }
    const bool isExecWire   = isPinExec(fromNodePtr->type, m_wireFromPin, m_wireFromIsOutput);
    const bool needInputPin = m_wireFromIsOutput; // 从输出拖，新节点需要输入 pin

    m_wireDropPopup = new QFrame(this);
    m_wireDropPopup->setObjectName("wireDropPopup");
    m_wireDropPopup->setStyleSheet(
        "QFrame#wireDropPopup {"
        "  background:#252526; border:1px solid #454545; border-radius:6px; }"
        "QLineEdit {"
        "  background:#3c3c3c; color:#ccc; border:1px solid #555;"
        "  border-radius:3px; padding:4px 6px; }"
        "QTreeWidget {"
        "  background:transparent; color:#ccc; border:none; outline:0; }"
        "QTreeWidget::item { padding:3px 4px; }"
        "QTreeWidget::item:hover { background:#2a2d2e; }"
        "QTreeWidget::item:selected { background:#094771; color:#fff; }"
        "QScrollBar:vertical { background:#252526; width:8px; border:none; }"
        "QScrollBar::handle:vertical { background:#454545; border-radius:4px; min-height:20px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }"
    );

    auto* vlay = new QVBoxLayout(m_wireDropPopup);
    vlay->setContentsMargins(0, 0, 0, 6);
    vlay->setSpacing(0);

    // 标题栏
    auto* titleBar = new QWidget(m_wireDropPopup);
    titleBar->setStyleSheet("background:#1e1e1e;");
    auto* titleLay = new QHBoxLayout(titleBar);
    titleLay->setContentsMargins(10, 7, 8, 7);
    auto* titleLbl = new QLabel("添加节点", titleBar);
    titleLbl->setStyleSheet("color:#aaa; font-size:11px; font-weight:bold; background:transparent;");
    auto* pinTypeLbl = new QLabel(isExecWire ? "[执行]" : "[数据]", titleBar);
    pinTypeLbl->setStyleSheet(
        QString("color:%1; font-size:10px; background:transparent;")
            .arg(isExecWire ? "#e07a30" : "#cccccc"));
    titleLay->addWidget(titleLbl);
    titleLay->addStretch();
    titleLay->addWidget(pinTypeLbl);
    vlay->addWidget(titleBar);

    // 搜索框
    auto* searchEdit = new QLineEdit(m_wireDropPopup);
    searchEdit->setPlaceholderText("搜索...");
    searchEdit->setClearButtonEnabled(true);
    QFont sf; sf.setPointSize(11); searchEdit->setFont(sf);
    QWidget* searchWrap = new QWidget(m_wireDropPopup);
    searchWrap->setStyleSheet("background:transparent;");
    auto* searchLay = new QHBoxLayout(searchWrap);
    searchLay->setContentsMargins(6, 6, 6, 4);
    searchLay->addWidget(searchEdit);
    vlay->addWidget(searchWrap);

    // 树形列表
    auto* tree = new QTreeWidget(m_wireDropPopup);
    tree->setHeaderHidden(true);
    tree->setRootIsDecorated(true);
    tree->setIndentation(16);
    tree->setAnimated(false);
    QFont tf; tf.setPointSize(10); tree->setFont(tf);
    vlay->addWidget(tree, 1);

    // 收集兼容节点并按分类插入
    QMap<QString, QTreeWidgetItem*> catItems;
    auto getCat = [&](const QString& name) -> QTreeWidgetItem* {
        if (!catItems.contains(name)) {
            auto* ci = new QTreeWidgetItem(tree, {name});
            ci->setFlags(ci->flags() & ~Qt::ItemIsSelectable);
            QFont cf; cf.setBold(true); cf.setPointSize(9); ci->setFont(0, cf);
            ci->setForeground(0, QColor("#888888"));
            catItems[name] = ci;
        }
        return catItems[name];
    };

    for (const NodeDef& def : nodeDefs()) {
        // 过滤不适用于当前上下文的 Self 节点
        if (!isSelfNodeVisible(def.typeId)) continue;

        QString compatPin;
        for (const PinDef& pd : def.pins) {
            // 兼容条件：pin 方向相对于新节点；类型相同
            bool wantOutput = !needInputPin;
            if (pd.isOutput == wantOutput && pd.isExec == isExecWire) {
                compatPin = pd.key;
                break;
            }
        }
        if (compatPin.isEmpty()) continue;

        QString catName = def.typeId.startsWith("Event.")  ? "事件"    :
                          def.typeId.startsWith("Action.") ? "动作"    :
                          def.typeId.startsWith("Flow.")   ? "流程控制" :
                          def.typeId.startsWith("UI.")     ? "UI"      :
                          def.typeId.startsWith("Self.")   ? "Self"    : "变量";
        auto* ci  = getCat(catName);
        auto* ni  = new QTreeWidgetItem(ci, {def.displayName});
        ni->setData(0, Qt::UserRole,     def.typeId);
        ni->setData(0, Qt::UserRole + 1, compatPin);
    }
    tree->expandAll();

    // 搜索过滤
    connect(searchEdit, &QLineEdit::textChanged, tree, [tree](const QString& text) {
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            auto* cat = tree->topLevelItem(i);
            bool anyVis = false;
            for (int j = 0; j < cat->childCount(); ++j) {
                auto* item = cat->child(j);
                bool vis = text.isEmpty() || item->text(0).contains(text, Qt::CaseInsensitive);
                item->setHidden(!vis);
                if (vis) anyVis = true;
            }
            cat->setHidden(!text.isEmpty() && !anyVis);
            if (!text.isEmpty() && anyVis) cat->setExpanded(true);
        }
    });

    // 选择节点（Enter 或单击，itemActivated 已覆盖双击，不重复连接 itemDoubleClicked 以防重入）
    auto onSelect = [this](QTreeWidgetItem* item) {
        if (!item || !item->parent()) return;
        onWireDropSelected(item->data(0, Qt::UserRole).toString(),
                           item->data(0, Qt::UserRole + 1).toString());
    };
    connect(tree, &QTreeWidget::itemActivated, m_wireDropPopup, [onSelect](QTreeWidgetItem* i, int) { onSelect(i); });
    connect(tree, &QTreeWidget::itemClicked,   m_wireDropPopup, [onSelect](QTreeWidgetItem* i, int) { onSelect(i); });

    // 安装 Escape 过滤器
    searchEdit->installEventFilter(this);
    tree->installEventFilter(this);

    // 位置与大小
    const int popW = 240, popH = 300;
    QPoint pos = screenPos;
    if (pos.x() + popW > width())  pos.setX(width()  - popW);
    if (pos.y() + popH > height()) pos.setY(height() - popH);
    if (pos.x() < 0) pos.setX(0);
    if (pos.y() < 0) pos.setY(0);
    m_wireDropPopup->setGeometry(pos.x(), pos.y(), popW, popH);
    m_wireDropPopup->show();
    m_wireDropPopup->raise();
    searchEdit->setFocus();

    m_dragState = DragState::None;
    update();
}

void BlueprintEditor::hideWireDropPopup() {
    if (m_wireDropPopup) {
        m_wireDropPopup->hide();
        m_wireDropPopup->deleteLater();
        m_wireDropPopup = nullptr;
    }
}

void BlueprintEditor::onWireDropSelected(const QString& typeId, const QString& compatPin) {
    // 防止 itemActivated + itemClicked 在同一次操作中重复触发
    if ((!m_doc && !m_bpClass) || m_wireFromNode.isEmpty()) { hideWireDropPopup(); return; }

    // 立即占住 wire 来源，防止任何重入再次进入此函数
    const QString fromNode        = m_wireFromNode;
    const QString fromPin         = m_wireFromPin;
    const bool    fromIsOutput    = m_wireFromIsOutput;
    m_wireFromNode.clear();
    m_wireFromPin.clear();

    BPNode node;
    node.id   = QUuid::createUuid().toString(QUuid::WithoutBraces);
    node.type = typeId;
    node.x    = (float)m_wireDropCanvasPos.x();
    node.y    = (float)m_wireDropCanvasPos.y();
    addNodeToActive(m_bpClass, m_doc, node);

    BPConnection conn;
    conn.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (fromIsOutput) {
        conn.fromNode = fromNode;
        conn.fromPin  = fromPin;
        conn.toNode   = node.id;
        conn.toPin    = compatPin;
    } else {
        conn.fromNode = node.id;
        conn.fromPin  = compatPin;
        conn.toNode   = fromNode;
        conn.toPin    = fromPin;
    }
    addConnToActive(m_bpClass, m_doc, conn);

    m_selectedNodeId = node.id;
    notifyModified();

    hideWireDropPopup();
    update();
}

// ── 引脚值内联编辑 ────────────────────────────────────────────────────

void BlueprintEditor::showInlineEdit(const QString& nodeId, const QString& pinKey) {
    commitInlineEdit();
    if (!m_doc && !m_bpClass) return;

    const BPNode* node = findNode(nodeId);
    if (!node) return;
    const NodeDef* def = findNodeDef(node->type);
    if (!def) return;

    // 计算该 pin 所在行
    int extraRows = (node->type == "Var.ActorRef") ? 1 : 0;
    int row = 0;
    for (const PinDef& pd : def->pins) {
        if (pd.key == pinKey) break;
        ++row;
    }

    QPointF tl  = canvasToScreen({node->x, node->y});
    float   nw  = kNodeW * (float)m_zoom;
    float   rowY = (float)(tl.y() + (kHeaderH + (row + extraRows) * kRowH) * m_zoom);
    float   rowH = kRowH * (float)m_zoom;
    QPointF pc  = pinCenter(*node, pinKey, false);
    float   x0  = (float)(pc.x() + 12.0 * m_zoom);
    float   x1  = (float)(tl.x() + nw - 6.0);

    m_inlineEditNodeId = nodeId;
    m_inlineEditPinKey = pinKey;

    m_inlineEdit = new QLineEdit(this);
    m_inlineEdit->setText(node->params.value(pinKey));
    m_inlineEdit->setGeometry(QRect((int)x0, (int)(rowY + 2),
                                    (int)(x1 - x0), (int)(rowH - 4)));
    m_inlineEdit->setFrame(false);
    m_inlineEdit->setStyleSheet(
        "QLineEdit { background: #1c2d3e; color: #5a9fd4;"
        "  border: 1px solid #2a5070; border-radius: 2px;"
        "  font-size: 8pt; padding: 0 2px; }");
    m_inlineEdit->show();
    m_inlineEdit->setFocus();
    m_inlineEdit->selectAll();
    m_inlineEdit->installEventFilter(this);

    connect(m_inlineEdit, &QLineEdit::returnPressed, this, &BlueprintEditor::commitInlineEdit);
}

void BlueprintEditor::commitInlineEdit() {
    if (!m_inlineEdit || (!m_doc && !m_bpClass)) return;
    QString val = m_inlineEdit->text();
    cancelInlineEdit();     // 先移除控件
    if (m_inlineEditNodeId.isEmpty()) return;
    // 写回
    for (BPNode node : activeNodes()) {   // 拷贝查找，再 update
        if (node.id == m_inlineEditNodeId) {
            node.params[m_inlineEditPinKey] = val;
            updateNodeInActive(m_bpClass, m_doc, node);
            notifyModified();
            break;
        }
    }
    m_inlineEditNodeId.clear();
    m_inlineEditPinKey.clear();
    update();
}

void BlueprintEditor::cancelInlineEdit() {
    if (!m_inlineEdit) return;
    m_inlineEdit->hide();
    m_inlineEdit->deleteLater();
    m_inlineEdit = nullptr;
    update();
}

// ── 引脚值编辑弹窗（actorId 专用）────────────────────────────────────

void BlueprintEditor::showParamEditPopup(QPoint screenPos, const QString& nodeId, const QString& pinKey) {
    hideParamEditPopup();

    const BPNode* node = findNode(nodeId);
    if (!node || (!m_doc && !m_bpClass)) return;
    const NodeDef* def = findNodeDef(node->type);
    if (!def) return;

    // 找到引脚的 label
    QString pinLabel = pinKey;
    for (const PinDef& pd : def->pins)
        if (pd.key == pinKey) { pinLabel = pd.label; break; }

    m_paramEditNodeId = nodeId;
    m_paramEditPinKey = pinKey;

    m_paramEditPopup = new QFrame(this);
    m_paramEditPopup->setObjectName("paramEditPopup");
    m_paramEditPopup->setStyleSheet(
        "QFrame#paramEditPopup {"
        "  background:#252526; border:1px solid #454545; border-radius:6px; }"
        "QLineEdit {"
        "  background:#3c3c3c; color:#ccc; border:1px solid #555;"
        "  border-radius:3px; padding:4px 6px; }"
        "QListWidget {"
        "  background:transparent; color:#ccc; border:none; outline:0; }"
        "QListWidget::item { padding:4px 8px; }"
        "QListWidget::item:hover { background:#2a2d2e; }"
        "QListWidget::item:selected { background:#094771; color:#fff; }"
        "QScrollBar:vertical { background:#252526; width:8px; border:none; }"
        "QScrollBar::handle:vertical { background:#454545; border-radius:4px; min-height:20px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }"
    );

    auto* vlay = new QVBoxLayout(m_paramEditPopup);
    vlay->setContentsMargins(0, 0, 0, 6);
    vlay->setSpacing(0);

    // 标题栏
    auto* titleBar = new QWidget(m_paramEditPopup);
    titleBar->setStyleSheet("background:#1e1e1e;");
    auto* titleLay = new QHBoxLayout(titleBar);
    titleLay->setContentsMargins(10, 7, 8, 7);
    bool isActorId = (pinKey == "actorId");
    auto* titleLbl = new QLabel(isActorId ? "选择对象" : ("设置：" + pinLabel), titleBar);
    titleLbl->setStyleSheet("color:#aaa; font-size:11px; font-weight:bold; background:transparent;");
    titleLay->addWidget(titleLbl);
    titleLay->addStretch();
    vlay->addWidget(titleBar);

    int popW = 220, popH = 0;

    if (isActorId) {
        // Actor 选择列表
        auto* list = new QListWidget(m_paramEditPopup);
        QFont lf; lf.setPointSize(10); list->setFont(lf);

        auto* clearItem = new QListWidgetItem("（清空）", list);
        clearItem->setForeground(QColor("#888"));
        clearItem->setData(Qt::UserRole, QString(""));

        QString currentId = node->params.value(pinKey);
        if (m_doc) {
            for (const ActorData& a : m_doc->actors()) {
                auto* item = new QListWidgetItem(a.name, list);
                item->setData(Qt::UserRole, a.id);
                if (a.id == currentId)
                    list->setCurrentItem(item);
            }
        }

        connect(list, &QListWidget::itemClicked, m_paramEditPopup, [this](QListWidgetItem* item) {
            onParamValueConfirmed(item->data(Qt::UserRole).toString());
        });
        list->installEventFilter(this);

        vlay->addWidget(list, 1);
        int itemCount = qMin(list->count(), 8);
        popH = 42 + itemCount * 28 + 12;
    } else {
        // 文本输入
        auto* wrap = new QWidget(m_paramEditPopup);
        wrap->setStyleSheet("background:transparent;");
        auto* wl = new QHBoxLayout(wrap);
        wl->setContentsMargins(8, 6, 8, 4);
        auto* edit = new QLineEdit(wrap);
        edit->setText(node->params.value(pinKey));
        edit->selectAll();
        QFont ef; ef.setPointSize(11); edit->setFont(ef);
        wl->addWidget(edit);
        vlay->addWidget(wrap);

        connect(edit, &QLineEdit::returnPressed, m_paramEditPopup, [this, edit]() {
            onParamValueConfirmed(edit->text());
        });
        edit->installEventFilter(this);
        QTimer::singleShot(0, edit, [edit]() { edit->setFocus(); });

        popH = 42 + 48;
    }

    // 位置与大小
    QPoint pos = screenPos;
    if (pos.x() + popW > width())  pos.setX(width()  - popW);
    if (pos.y() + popH > height()) pos.setY(height() - popH);
    if (pos.x() < 0) pos.setX(0);
    if (pos.y() < 0) pos.setY(0);
    m_paramEditPopup->setGeometry(pos.x(), pos.y(), popW, popH);
    m_paramEditPopup->show();
    m_paramEditPopup->raise();
}

void BlueprintEditor::hideParamEditPopup() {
    if (m_paramEditPopup) {
        m_paramEditPopup->hide();
        m_paramEditPopup->deleteLater();
        m_paramEditPopup = nullptr;
    }
}

void BlueprintEditor::onParamValueConfirmed(const QString& value) {
    if ((!m_doc && !m_bpClass) || m_paramEditNodeId.isEmpty()) { hideParamEditPopup(); return; }
    const QString nodeId  = m_paramEditNodeId;
    const QString pinKey  = m_paramEditPinKey;
    m_paramEditNodeId.clear();
    m_paramEditPinKey.clear();
    for (BPNode node : activeNodes()) {   // 拷贝查找，再 update
        if (node.id == nodeId) {
            if (value.isEmpty()) node.params.remove(pinKey);
            else                  node.params[pinKey] = value;
            updateNodeInActive(m_bpClass, m_doc, node);
            notifyModified();
            break;
        }
    }
    hideParamEditPopup();
    update();
}

// ── bpClass 模式 ──────────────────────────────────────────────────────

void BlueprintEditor::loadBpClass(BPClass* bpClass) {
    m_bpClass = bpClass;
    m_doc     = nullptr;
    m_selectedNodeId.clear();
    m_dragState = DragState::None;
    // Clear wire state
    m_wireFromNode.clear();
    m_wireFromPin.clear();
    m_wireFromIsOutput = false;
    // Clear inline edit state
    m_inlineEditNodeId.clear();
    m_inlineEditPinKey.clear();
    // Clear param edit state
    m_paramEditNodeId.clear();
    m_paramEditPinKey.clear();
    hideWireDropPopup();
    cancelInlineEdit();
    update();
}

void BlueprintEditor::saveBpClass() {
    if (m_bpClass) m_bpClass->save();
}

const QList<BPNode>& BlueprintEditor::activeNodes() const {
    if (m_bpClass) return m_bpClass->nodes;
    static QList<BPNode> empty;
    return m_doc ? m_doc->bpNodes() : empty;
}

const QList<BPConnection>& BlueprintEditor::activeConns() const {
    if (m_bpClass) return m_bpClass->connections;
    static QList<BPConnection> empty;
    return m_doc ? m_doc->bpConnections() : empty;
}

void BlueprintEditor::notifyModified() {
    if (m_bpClass) emit bpClassModified();
    else           emit documentModified();
}

bool BlueprintEditor::isSelfNodeVisible(const QString& typeId) const {
    if (!typeId.startsWith("Self.")) return true;
    if (!m_bpClass) return false;   // level mode: hide Self nodes
    if (typeId.startsWith("Self.Sprite.") && !m_bpClass->components.contains("精灵渲染器"))
        return false;
    if (typeId.startsWith("Self.Camera.") && !m_bpClass->components.contains("摄像机组件"))
        return false;
    return true;
}
