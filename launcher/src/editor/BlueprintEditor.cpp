#include "BlueprintEditor.h"
#include "UndoCommands.h"
#include "models/UIDocument.h"
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
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

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

// ── 分支控制（Flow.Switch）分支数据：存于 BPNode.params ────────────────
//   params["branches"]   = JSON 数组，每项 {"id":稳定id,"value":比较值}
//                          引脚 key = "case_" + id（用稳定 id，删中间分支不错位）
//   params["hasDefault"] = "true"/"false"，是否启用 default 兜底出口
namespace {
struct SwitchBranch { QString id; QString value; };

QList<SwitchBranch> parseSwitchBranches(const QString& json) {
    QList<SwitchBranch> out;
    if (json.isEmpty()) return out;
    QJsonParseError err{};
    const QJsonDocument d = QJsonDocument::fromJson(json.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !d.isArray()) return out;
    for (const QJsonValue& v : d.array()) {
        const QJsonObject o = v.toObject();
        const QString id = o.value("id").toString();
        if (id.isEmpty()) continue;
        out.append({id, o.value("value").toString()});
    }
    return out;
}

QString serializeSwitchBranches(const QList<SwitchBranch>& branches) {
    QJsonArray arr;
    for (const SwitchBranch& b : branches) {
        QJsonObject o;
        o["id"]    = b.id;
        o["value"] = b.value;
        arr.append(o);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

bool switchHasDefault(const BPNode& node) {
    const QString v = node.params.value("hasDefault").toLower();
    return v == "true" || v == "1";
}

// 新建 Flow.Switch 时给一组默认分支，避免空节点
void seedSwitchDefaults(BPNode& node) {
    if (node.type != "Flow.Switch") return;
    if (!node.params.value("branches").isEmpty()) return;
    QList<SwitchBranch> def;
    def.append({QUuid::createUuid().toString(QUuid::WithoutBraces), ""});
    def.append({QUuid::createUuid().toString(QUuid::WithoutBraces), ""});
    node.params["branches"]   = serializeSwitchBranches(def);
    node.params["hasDefault"] = "false";
}
} // namespace

// ── 节点类型注册 ──────────────────────────────────────────────────────

static bool nodeHasUIPicker(const QString& type) {
    return type == "UI.Ref"
        || type == "UI.Show"    || type == "UI.Hide"
        || type == "UI.Create"  || type == "UI.Destroy";
}

static bool nodeIsUIRef(const QString& type) {
    return type == "UI.Ref";
}

QStringList BlueprintEditor::loadWidgetNames(const QString& uiName) const {
    if (uiName.isEmpty()) return {};
    if (m_uiWidgetCache.contains(uiName)) return m_uiWidgetCache.value(uiName);
    if (m_projectRoot.isEmpty()) return {};
    UIDocument doc;
    if (!doc.load(m_projectRoot + "/UI/" + uiName + ".ui")) return {};
    QStringList names;
    for (const UIWidget& w : doc.widgets()) names << w.name;
    m_uiWidgetCache[uiName] = names;
    return names;
}

const QList<BlueprintEditor::NodeDef>& BlueprintEditor::nodeDefs() {
    static QList<NodeDef> defs = {
        {
            "Event.BeginPlay", "开始运行", QColor("#6a2a8a"),
            {
                {"exec_out", "exec", true, true}
            }
        },
        {"Event.Key.W",       "W 键",    QColor("#6a2a8a"), {{"pressed","按下",true,true},{"released","松开",true,true}}},
        {"Event.Key.A",       "A 键",    QColor("#6a2a8a"), {{"pressed","按下",true,true},{"released","松开",true,true}}},
        {"Event.Key.S",       "S 键",    QColor("#6a2a8a"), {{"pressed","按下",true,true},{"released","松开",true,true}}},
        {"Event.Key.D",       "D 键",    QColor("#6a2a8a"), {{"pressed","按下",true,true},{"released","松开",true,true}}},
        {"Event.Key.Up",      "↑ 键",    QColor("#6a2a8a"), {{"pressed","按下",true,true},{"released","松开",true,true}}},
        {"Event.Key.Down",    "↓ 键",    QColor("#6a2a8a"), {{"pressed","按下",true,true},{"released","松开",true,true}}},
        {"Event.Key.Left",    "← 键",    QColor("#6a2a8a"), {{"pressed","按下",true,true},{"released","松开",true,true}}},
        {"Event.Key.Right",   "→ 键",    QColor("#6a2a8a"), {{"pressed","按下",true,true},{"released","松开",true,true}}},
        {"Event.Key.Space",   "空格键",  QColor("#6a2a8a"), {{"pressed","按下",true,true},{"released","松开",true,true}}},
        {"Event.Key.Return",  "回车键",  QColor("#6a2a8a"), {{"pressed","按下",true,true},{"released","松开",true,true}}},
        {"Event.Key.Escape",  "Esc 键",  QColor("#6a2a8a"), {{"pressed","按下",true,true},{"released","松开",true,true}}},
        {"Event.Key.Shift",   "Shift 键",QColor("#6a2a8a"), {{"pressed","按下",true,true},{"released","松开",true,true}}},
        {"Event.Key.Control", "Ctrl 键", QColor("#6a2a8a"), {{"pressed","按下",true,true},{"released","松开",true,true}}},
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
                {"actorId",  "对象ID",  false, false, ValueKind::ActorRef},
                {"dx",       "X偏移",   false, false},
                {"dy",       "Y偏移",   false, false}
            }
        },
        {
            "Action.SetActive", "设置激活", QColor("#1a5a2a"),
            {
                {"exec_in",  "exec",   true,  false},
                {"exec_out", "exec",   true,  true},
                {"actorId",  "对象ID", false, false, ValueKind::ActorRef},
                {"active",   "激活",   false, false, ValueKind::Bool}
            }
        },
        {
            "Action.LoadLevel", "跳转关卡", QColor("#5a3a1a"),
            {
                {"exec_in",   "exec",   true,  false},
                {"levelName", "关卡名", false, false, ValueKind::LevelRef}
            }
        },
        {
            "Action.BackLevel", "返回上一关", QColor("#5a3a1a"),
            {
                {"exec_in", "exec", true, false}
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
            // 分支控制（单值 Switch）：占位定义，实际引脚由 effectivePins 动态生成
            // （exec_in + value + 每分支 case_<id> 出口 + 可选 default）
            "Flow.Switch", "分支控制", QColor("#2a6a6a"),
            {
                {"exec_in", "exec", true,  false},
                {"value",   "值",   false, false}
            }
        },
        {
            "Var.GetActorPos", "获取位置", QColor("#2a4a1a"),
            {
                {"actorId", "对象ID", false, false, ValueKind::ActorRef},
                {"x",       "X",      false, true},
                {"y",       "Y",      false, true}
            }
        },
        {
            "Var.ActorRef", "Actor引用", QColor("#4a2a0a"),
            {
                {"actorId", "对象ID", false, true, ValueKind::ActorRef}
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
             {"active","激活",false,false,ValueKind::Bool}}
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
             {"flip","翻转",false,false,ValueKind::Bool}}
        },
        {
            "Self.Sprite.SetFlipY", "垂直翻转", QColor("#2a4a1a"),
            {{"exec_in","exec",true,false},{"exec_out","exec",true,true},
             {"flip","翻转",false,false,ValueKind::Bool}}
        },
        {
            "Self.Sprite.SetVisible", "设置精灵可见", QColor("#2a4a1a"),
            {{"exec_in","exec",true,false},{"exec_out","exec",true,true},
             {"visible","可见",false,false,ValueKind::Bool}}
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
        {
            "Self.Camera.SetFollow", "设置跟随目标", QColor("#1a2a4a"),
            {{"exec_in","exec",true,false},{"exec_out","exec",true,true},
             {"target","目标名称",false,false}}
        },
        {
            "Self.Camera.SetFollowOffset", "设置跟随偏移", QColor("#1a2a4a"),
            {{"exec_in","exec",true,false},{"exec_out","exec",true,true},
             {"x","偏移X",false,false},{"y","偏移Y",false,false}}
        },
        {
            "Self.Camera.SetSmooth", "设置跟随平滑度", QColor("#1a2a4a"),
            {{"exec_in","exec",true,false},{"exec_out","exec",true,true},
             {"speed","平滑速度",false,false}}
        },
        {
            "Self.Camera.SetBoundary", "设置摄像机边界", QColor("#1a2a4a"),
            {{"exec_in","exec",true,false},{"exec_out","exec",true,true},
             {"minX","左",false,false},{"maxX","右",false,false},
             {"minY","下",false,false},{"maxY","上",false,false}}
        },
        {
            "Self.Camera.ClearFollow", "清除跟随目标", QColor("#1a2a4a"),
            {{"exec_in","exec",true,false},{"exec_out","exec",true,true}}
        },
        {
            "Self.Camera.ClearBoundary", "清除摄像机边界", QColor("#1a2a4a"),
            {{"exec_in","exec",true,false},{"exec_out","exec",true,true}}
        },
        // ── UI 节点 ──────────────────────────────────────────────────────────
        {
            "UI.Ref", "UI引用", QColor("#4a1a6a"),
            {}  // 无静态引脚；动态控件引脚在渲染/命中检测时实时生成
        },
        {
            "UI.Create", "创建UI", QColor("#4a1a6a"),
            {
                {"exec_in",   "exec",     true,  false},
                {"exec_out",  "exec",     true,  true},
                {"widgetRef", "控件引用", false, false, ValueKind::WidgetRef},
            }
        },
        {
            "UI.Show", "显示UI", QColor("#1a4a6a"),
            {
                {"exec_in",   "exec",     true,  false},
                {"exec_out",  "exec",     true,  true},
                {"widgetRef", "控件引用", false, false, ValueKind::WidgetRef},
            }
        },
        {
            "UI.Hide", "隐藏UI", QColor("#1a4a6a"),
            {
                {"exec_in",   "exec",     true,  false},
                {"exec_out",  "exec",     true,  true},
                {"widgetRef", "控件引用", false, false, ValueKind::WidgetRef},
            }
        },
        {
            "UI.Destroy", "销毁UI", QColor("#6a1a1a"),
            {
                {"exec_in",   "exec",     true,  false},
                {"exec_out",  "exec",     true,  true},
                {"widgetRef", "控件引用", false, false, ValueKind::WidgetRef},
            }
        },
        {
            "UI.SetText", "设置文本", QColor("#1a4a6a"),
            {
                {"exec_in",   "exec",     true,  false},
                {"exec_out",  "exec",     true,  true},
                {"widgetRef", "控件引用", false, false, ValueKind::WidgetRef},
                {"text",      "文本内容", false, false},
            }
        },
        {
            "UI.SetValue", "设置进度值", QColor("#1a4a6a"),
            {
                {"exec_in",   "exec",     true,  false},
                {"exec_out",  "exec",     true,  true},
                {"widgetRef", "控件引用", false, false, ValueKind::WidgetRef},
                {"value",     "数值",     false, false},
            }
        },
        {
            "UI.SetPosition", "设置UI位置", QColor("#1a4a6a"),
            {
                {"exec_in",   "exec",     true,  false},
                {"exec_out",  "exec",     true,  true},
                {"widgetRef", "控件引用", false, false, ValueKind::WidgetRef},
                {"x",         "X",        false, false},
                {"y",         "Y",        false, false},
            }
        },
        {
            "UI.SetVisible", "设置控件可见", QColor("#1a4a6a"),
            {
                {"exec_in",   "exec",     true,  false},
                {"exec_out",  "exec",     true,  true},
                {"widgetRef", "控件引用", false, false, ValueKind::WidgetRef},
                {"visible",   "可见",     false, false, ValueKind::Bool},
            }
        },
        {
            "UI.OnButtonClick", "按钮点击时", QColor("#6a2a8a"),
            {
                {"widgetRef", "控件引用", false, false, ValueKind::WidgetRef},
                {"exec_out",  "exec",     true,  true},
            }
        },
        {
            "UI.OnDropdownChanged", "下拉选项改变时", QColor("#6a2a8a"),
            {
                {"widgetRef", "控件引用", false, false, ValueKind::WidgetRef},
                {"exec_out",  "exec",     true,  true},
                {"index",     "选中索引", false, true},
            }
        },
        // ── 运行时变量 ───────────────────────────────────────────────────
        {
            "Var.SetNumber", "设置数值变量", QColor("#2a4a2a"),
            {{"exec_in","exec",true,false},{"exec_out","exec",true,true},
             {"name","变量名",false,false},{"value","数值",false,false}}
        },
        {
            "Var.GetNumber", "获取数值变量", QColor("#2a4a2a"),
            {{"name","变量名",false,false},{"value","数值",false,true}}
        },
        {
            "Var.SetBool", "设置布尔变量", QColor("#2a4a2a"),
            {{"exec_in","exec",true,false},{"exec_out","exec",true,true},
             {"name","变量名",false,false},{"value","布尔值",false,false}}
        },
        {
            "Var.GetBool", "获取布尔变量", QColor("#2a4a2a"),
            {{"name","变量名",false,false},{"value","布尔值",false,true}}
        },
        {
            "Var.SetString", "设置字符串变量", QColor("#2a4a2a"),
            {{"exec_in","exec",true,false},{"exec_out","exec",true,true},
             {"name","变量名",false,false},{"value","字符串",false,false}}
        },
        {
            "Var.GetString", "获取字符串变量", QColor("#2a4a2a"),
            {{"name","变量名",false,false},{"value","字符串",false,true}}
        },
        {
            "Var.NumberToString", "数值转字符串", QColor("#3a3a4a"),
            {{"number","数值",false,false},{"text","文本",false,true}}
        },
        // ── 数学运算 ─────────────────────────────────────────────────────
        {
            "Math.Add", "加法", QColor("#1a2a5a"),
            {{"a","A",false,false},{"b","B",false,false},{"result","结果",false,true}}
        },
        {
            "Math.Sub", "减法", QColor("#1a2a5a"),
            {{"a","A",false,false},{"b","B",false,false},{"result","结果",false,true}}
        },
        {
            "Math.Mul", "乘法", QColor("#1a2a5a"),
            {{"a","A",false,false},{"b","B",false,false},{"result","结果",false,true}}
        },
        {
            "Math.Div", "除法", QColor("#1a2a5a"),
            {{"a","A",false,false},{"b","B",false,false},{"result","结果",false,true}}
        },
        {
            "Math.Clamp", "数值夹取", QColor("#1a2a5a"),
            {{"value","数值",false,false},{"min","最小",false,false},
             {"max","最大",false,false},{"result","结果",false,true}}
        },
        // ── 逻辑运算 ─────────────────────────────────────────────────────
        {
            "Logic.Compare", "数值比较", QColor("#3a2a5a"),
            {{"a","A",false,false},{"op","运算符",false,false},
             {"b","B",false,false},{"result","结果",false,true}}
        },
        {
            "Logic.Not", "逻辑非", QColor("#3a2a5a"),
            {{"value","布尔值",false,false},{"result","结果",false,true}}
        },
        {
            "Logic.And", "逻辑与", QColor("#3a2a5a"),
            {{"a","A",false,false},{"b","B",false,false},{"result","结果",false,true}}
        },
        {
            "Logic.Or", "逻辑或", QColor("#3a2a5a"),
            {{"a","A",false,false},{"b","B",false,false},{"result","结果",false,true}}
        },
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
    m_bpUndoStack = new QUndoStack(this);
}

void BlueprintEditor::loadLevel(LevelDocument* doc) {
    m_bpClass = nullptr;
    m_doc = doc;
    m_selectedNodeId.clear();
    m_selectedConnId.clear();
    m_dragState = DragState::None;
    hideWireDropPopup();
    cancelInlineEdit();
    m_bpUndoStack->clear();
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

// 节点实例的有效引脚列表。普通节点直接返回静态 def->pins；
// 动态节点（后续 Flow.Switch / Macro:: 调用节点）按实例配置计算。
// 所有"按实例枚举引脚"的几何/绘制/命中逻辑统一走此入口。
QList<BlueprintEditor::PinDef> BlueprintEditor::effectivePins(const BPNode& node) const {
    // 分支控制：exec_in + value（数据输入）+ 每个分支一个 exec 出口 + 可选 default
    if (node.type == "Flow.Switch") {
        QList<PinDef> pins;
        pins.append({"exec_in", "exec", true,  false});
        pins.append({"value",   "值",   false, false});
        for (const SwitchBranch& b : parseSwitchBranches(node.params.value("branches"))) {
            const QString label = b.value.isEmpty() ? "= ?" : ("= " + b.value);
            pins.append({"case_" + b.id, label, true, true});
        }
        if (switchHasDefault(node))
            pins.append({"default", "默认", true, true});
        return pins;
    }
    const NodeDef* def = findNodeDef(node.type);
    return def ? def->pins : QList<PinDef>{};
}

float BlueprintEditor::nodeHeight(const BPNode& node) const {
    if (node.type == "UI.Ref") {
        // 1行选择器按钮 + N行动态控件引脚
        int n = loadWidgetNames(node.params.value("uiName")).size();
        return kHeaderH + kRowH * (1 + n) + 4.0f;
    }
    int pinCount = effectivePins(node).size();
    // 分支控制：在引脚行下额外预留「默认开关」「＋加分支」两行
    if (node.type == "Flow.Switch")
        return kHeaderH + kRowH * (pinCount + 2) + 4.0f;
    int extraRows = (node.type == "Var.ActorRef" || nodeHasUIPicker(node.type)) ? 1 : 0;
    return kHeaderH + kRowH * qMax(1, pinCount + extraRows) + 4.0f;
}

QRectF BlueprintEditor::nodeRect(const BPNode& node) const {
    return QRectF(node.x, node.y, kNodeW, nodeHeight(node));
}

QPointF BlueprintEditor::pinCenter(const BPNode& node, const QString& pinKey, bool isOutput) const {
    QPointF tl = canvasToScreen({node.x, node.y});
    float rightX = (float)(tl.x() + kNodeW * m_zoom - 10.0f);
    float leftX  = (float)(tl.x() + 10.0f);

    if (node.type == "UI.Ref" && isOutput) {
        // rows 0..N-1: 动态控件引脚（紧接选择器按钮行）
        const QStringList widgets = loadWidgetNames(node.params.value("uiName"));
        for (int i = 0; i < widgets.size(); ++i) {
            if (widgets[i] == pinKey) {
                float cy = (float)(tl.y() + (kHeaderH + (1 + i) * kRowH + kRowH * 0.5f) * m_zoom);
                return {rightX, cy};
            }
        }
        return {};
    }

    int extraRows = (node.type == "Var.ActorRef" || nodeHasUIPicker(node.type)) ? 1 : 0;
    int row = 0;
    for (const PinDef& pd : effectivePins(node)) {
        if (pd.key == pinKey && pd.isOutput == isOutput) {
            float cy = (float)(tl.y() + (kHeaderH + (row + extraRows) * kRowH + kRowH * 0.5f) * m_zoom);
            return {isOutput ? rightX : leftX, cy};
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
    // 分支控制：value 是数据输入，其余（exec_in / case_* / default）都是 exec
    if (typeId == "Flow.Switch")
        return pinKey != "value";
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
        for (const PinDef& pd : effectivePins(node)) {
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
        // UI.Ref：检测动态控件输出引脚
        if (node.type == "UI.Ref") {
            const QStringList widgets = loadWidgetNames(node.params.value("uiName"));
            for (const QString& wName : widgets) {
                QPointF pc = pinCenter(node, wName, true);
                float hitR = kPinSq * m_zoom + 4.0f;
                QPointF delta = screenPos - pc;
                if (delta.x() * delta.x() + delta.y() * delta.y() <= hitR * hitR) {
                    Hit h;
                    h.type        = Hit::Pin;
                    h.nodeId      = node.id;
                    h.pinName     = wName;
                    h.pinIsOutput = true;
                    h.pinIsExec   = false;
                    return h;
                }
            }
        }
    }
    // 分支控制的可点区域：分支 × 删除 / 分支值编辑 / 默认开关 / ＋加分支
    // （置于 pin 圆点命中之后、普通 PinValue 之前）
    for (int i = nodes.size() - 1; i >= 0; --i) {
        const BPNode& node = nodes[i];
        if (node.type != "Flow.Switch") continue;
        QPointF tl = canvasToScreen({node.x, node.y});
        float nw = kNodeW * (float)m_zoom;
        const QList<PinDef> pins = effectivePins(node);
        const int P = pins.size();
        // 各 case 行：左侧 × 删除，中段值编辑
        for (int r = 0; r < P; ++r) {
            if (!pins[r].key.startsWith("case_")) continue;
            float rowY = tl.y() + (kHeaderH + r * kRowH) * (float)m_zoom;
            float rowH = kRowH * (float)m_zoom;
            if (screenPos.y() < rowY || screenPos.y() > rowY + rowH) continue;
            const QString branchId = pins[r].key.mid(5);
            if (screenPos.x() >= tl.x() && screenPos.x() <= tl.x() + 18.0f * m_zoom) {
                Hit h; h.type = Hit::SwitchDel; h.nodeId = node.id; h.pinName = branchId; return h;
            }
            if (screenPos.x() <= tl.x() + nw - 16.0f * m_zoom) {
                Hit h; h.type = Hit::SwitchValue; h.nodeId = node.id; h.pinName = branchId; return h;
            }
        }
        // 默认开关行 (row P)
        float defY = tl.y() + (kHeaderH + P * kRowH) * (float)m_zoom;
        float rowH = kRowH * (float)m_zoom;
        if (screenPos.y() >= defY && screenPos.y() <= defY + rowH &&
            screenPos.x() >= tl.x() && screenPos.x() <= tl.x() + nw) {
            Hit h; h.type = Hit::SwitchDefault; h.nodeId = node.id; return h;
        }
        // ＋加分支行 (row P+1)
        float addY = tl.y() + (kHeaderH + (P + 1) * kRowH) * (float)m_zoom;
        if (screenPos.y() >= addY && screenPos.y() <= addY + rowH &&
            screenPos.x() >= tl.x() && screenPos.x() <= tl.x() + nw) {
            Hit h; h.type = Hit::SwitchAdd; h.nodeId = node.id; return h;
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

        // UI picker 节点：选择器按钮行可点击
        if (nodeHasUIPicker(node.type)) {
            float rowY = tl.y() + kHeaderH * m_zoom;
            float rowH = kRowH * m_zoom;
            if (screenPos.x() >= tl.x() + 4*m_zoom && screenPos.x() <= tl.x() + nw - 4*m_zoom &&
                screenPos.y() >= rowY && screenPos.y() <= rowY + rowH) {
                Hit h;
                h.type    = Hit::PinValue;
                h.nodeId  = node.id;
                h.pinName = "uiName";
                return h;
            }
            if (nodeIsUIRef(node.type)) continue; // UI.Ref 无数据输入引脚，跳过后续值编辑检测
            // 其他 UI 节点：继续检测引脚值区域（带 extraRows=1 偏移）
        }

        // 其他节点：检测未连接的非 actorId 数据输入引脚
        int extraRows = (node.type == "Var.ActorRef" || nodeHasUIPicker(node.type)) ? 1 : 0;
        int row = 0;
        for (const PinDef& pd : effectivePins(node)) {
            if (!pd.isExec && !pd.isOutput && pd.key != "actorId"
                && !(nodeHasUIPicker(node.type) && pd.key == "widgetRef")
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

    // 连接线命中检测：采样贝塞尔曲线上的点
    for (const BPConnection& conn : activeConns()) {
        const BPNode* fromNode = findNode(conn.fromNode);
        const BPNode* toNode   = findNode(conn.toNode);
        if (!fromNode || !toNode) continue;
        QPointF from = pinCenter(*fromNode, conn.fromPin, true);
        QPointF to   = pinCenter(*toNode,   conn.toPin,   false);
        if (from.isNull() || to.isNull()) continue;

        float dx = qMax(80.0f * m_zoom, (float)std::fabs(to.x() - from.x()) * 0.5f);
        QPointF cp1 = from + QPointF(dx, 0);
        QPointF cp2 = to   - QPointF(dx, 0);
        constexpr int kSamples = 24;
        for (int s = 0; s <= kSamples; ++s) {
            float t  = (float)s / kSamples;
            float ti = 1.0f - t;
            QPointF pt = ti*ti*ti*from + 3*ti*ti*t*cp1 + 3*ti*t*t*cp2 + t*t*t*to;
            QPointF d  = screenPos - pt;
            if (d.x()*d.x() + d.y()*d.y() <= 64.0f) {  // 8px threshold
                Hit h;
                h.type   = Hit::Wire;
                h.connId = conn.id;
                return h;
            }
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
        if (from.isNull() || to.isNull()) continue;
        bool isExec  = isPinExec(fromNode->type, conn.fromPin, true);

        if (conn.id == m_selectedConnId) {
            // 选中高亮：先画宽白边再画本色
            p.setPen(QPen(QColor(0xff, 0xff, 0xff, 180), qMax(3.0, 4.0 * m_zoom), Qt::SolidLine, Qt::RoundCap));
            p.setBrush(Qt::NoBrush);
            float dx = qMax(80.0f * (float)m_zoom, (float)std::fabs(to.x() - from.x()) * 0.5f);
            QPainterPath path;
            path.moveTo(from);
            path.cubicTo(from + QPointF(dx, 0), to - QPointF(dx, 0), to);
            p.drawPath(path);
        }
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
    QString titleText = def->displayName;
    if (nodeHasUIPicker(node.type)) {
        const QString uiParam = node.params.value("uiName");
        if (!uiParam.isEmpty()) {
            // 标题只显示 UI 文件名部分（去掉 ::widget）
            const QString uiDisplayName = uiParam.contains("::")
                ? uiParam.left(uiParam.indexOf("::")) : uiParam;
            titleText += ": " + uiDisplayName;
        }
    }
    p.drawText(QRectF(textX, tl.y(), nw - (textX - tl.x()) - 4, hh),
               Qt::AlignVCenter | Qt::AlignLeft, titleText);

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

    // UI.Ref：选择器按钮 + 固定 uiRef 引脚 + 动态控件引脚
    if (nodeHasUIPicker(node.type)) {
        const QString uiParam = node.params.value("uiName");
        const bool hasName    = !uiParam.isEmpty();
        // 选择器按钮显示文字：整体→"游戏HUD ▾"，控件→"游戏HUD · 按钮 ▾"
        QString btnText;
        if (!hasName) {
            btnText = "(点击选择UI) ▾";
        } else if (uiParam.contains("::")) {
            const int sep = uiParam.indexOf("::");
            btnText = uiParam.left(sep) + " · " + uiParam.mid(sep + 2) + " ▾";
        } else {
            btnText = uiParam + " ▾";
        }
        const QString uiName = hasName
            ? (uiParam.contains("::") ? uiParam.left(uiParam.indexOf("::")) : uiParam)
            : QString();
        float btnRowY = tl.y() + kHeaderH * (float)m_zoom;
        float btnRowH = kRowH * (float)m_zoom;
        QRectF btnRc(tl.x() + 6.0f*(float)m_zoom, btnRowY + 3.0f*(float)m_zoom,
                     nw - 12.0f*(float)m_zoom, btnRowH - 6.0f*(float)m_zoom);
        p.setBrush(QColor(0x1a, 0x18, 0x2a));
        p.setPen(QPen(hasName ? QColor(0x8a, 0x6a, 0xd4) : QColor(0x44, 0x33, 0x66), 1.0));
        p.drawRoundedRect(btnRc, 3.0*(float)m_zoom, 3.0*(float)m_zoom);
        QFont bf; bf.setPointSizeF(qMax(7.0, 9.0 * m_zoom));
        p.setFont(bf);
        p.setPen(hasName ? QColor(0x8a, 0x6a, 0xd4) : QColor(0x66, 0x55, 0x88));
        p.drawText(btnRc, Qt::AlignCenter, btnText);

        // UI.Ref：动态控件输出引脚 + 提前 return
        if (nodeIsUIRef(node.type)) {
            const QStringList widgetNames = loadWidgetNames(uiName);
            for (int i = 0; i < widgetNames.size(); ++i) {
                const QString& wName = widgetNames[i];
                QPointF pc = pinCenter(node, wName, true);
                drawPin(p, pc, false, isPinConnected(node.id, wName, true));
                float rowY = tl.y() + (kHeaderH + (1 + i) * kRowH) * (float)m_zoom;
                p.setPen(QColor(0xcc, 0xcc, 0xcc));
                p.setFont(bf);
                QRectF labelRc(tl.x(), rowY, nw - 18.0f*(float)m_zoom, kRowH*(float)m_zoom);
                p.drawText(labelRc, Qt::AlignVCenter | Qt::AlignRight, wName);
            }
            if (node.id == m_selectedNodeId) {
                p.setPen(QPen(QColor(0xff, 0xff, 0xff), 2.0));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(nodeRc.adjusted(-3, -3, 3, 3), radius + 2, radius + 2);
            }
            return;
        }
        // 其他 UI 节点：选择器按钮已绘制，继续通用 pin 循环（extraRows = 1）
    }

    // Pin 行
    font.setBold(false);
    float pinFontSize = qMax(7.0, 9.0 * m_zoom);
    font.setPointSizeF(pinFontSize);
    p.setFont(font);

    int extraRows = (node.type == "Var.ActorRef" || nodeHasUIPicker(node.type)) ? 1 : 0;
    int row = 0;
    for (const PinDef& pd : effectivePins(node)) {
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

            // 未连接的非 actorId 数据输入引脚：右侧绘制常驻值编辑控件
            // UI picker 节点的 widgetRef 引脚由嵌入选择器负责，不再显示文本框
            if (!pd.isExec && pd.key != "actorId"
                && !(nodeHasUIPicker(node.type) && pd.key == "widgetRef")
                && !isPinConnected(node.id, pd.key, false)) {
                QString val = node.params.value(pd.key);
                if (pd.kind == ValueKind::Bool) {
                    // 勾选框
                    const bool on = (val.toLower() == "true" || val == "1");
                    float sz  = qMin(rowH - 6.0f, 14.0f * (float)m_zoom);
                    float bx1 = (float)(tl.x() + nw - 6.0f);
                    QRectF box(bx1 - sz, rowY + (rowH - sz) * 0.5f, sz, sz);
                    p.setBrush(on ? QColor(0x2a, 0x7a, 0x3a) : QColor(0x1c, 0x2d, 0x3e));
                    p.setPen(QPen(QColor(0x2a, 0x50, 0x70), 1.0));
                    p.drawRoundedRect(box, 2.0, 2.0);
                    p.setBrush(Qt::NoBrush);
                    if (on) {
                        p.setPen(QPen(QColor(0xe0, 0xff, 0xe0), qMax(1.2, 1.6 * m_zoom)));
                        QPointF a(box.left() + sz * 0.24, box.center().y() + sz * 0.02);
                        QPointF b(box.left() + sz * 0.42, box.bottom() - sz * 0.26);
                        QPointF c(box.right() - sz * 0.20, box.top() + sz * 0.26);
                        p.drawLine(a, b); p.drawLine(b, c);
                    }
                } else {
                    // 值框（下拉类型在右侧加 ▾ 提示可选）
                    const bool isDropdown = (pd.kind == ValueKind::LevelRef
                                          || pd.kind == ValueKind::ActorRef
                                          || pd.kind == ValueKind::WidgetRef);
                    float bx0 = (float)(tl.x() + nw * 0.5f);
                    float bx1 = (float)(tl.x() + nw - 6.0f);
                    QRectF boxRc(bx0, rowY + 2, bx1 - bx0, rowH - 4);
                    p.setBrush(QColor(0x1c, 0x2d, 0x3e));
                    p.setPen(QPen(QColor(0x2a, 0x50, 0x70), 1.0));
                    p.drawRoundedRect(boxRc, 2.0, 2.0);
                    p.setBrush(Qt::NoBrush);
                    QString display = val.isEmpty() ? (isDropdown ? "选择…" : "···") : val;
                    p.setPen(val.isEmpty() ? QColor(0x55, 0x55, 0x55) : QColor(0x5a, 0x9f, 0xd4));
                    QFont vf; vf.setPointSizeF(qMax(7.0, 8.5 * m_zoom));
                    p.setFont(vf);
                    p.drawText(boxRc.adjusted(3, 0, isDropdown ? -12 : -3, 0),
                               Qt::AlignVCenter | Qt::AlignRight, display);
                    if (isDropdown) {
                        p.setPen(QColor(0x88, 0xaa, 0xcc));
                        p.drawText(boxRc.adjusted(0, 0, -3, 0), Qt::AlignVCenter | Qt::AlignRight, "▾");
                    }
                    p.setFont(font);
                }
            }
        }
        ++row;
    }

    // 分支控制：在引脚行下绘制 分支×删除 / 默认开关 / ＋加分支
    if (node.type == "Flow.Switch") {
        const QList<PinDef> pins = effectivePins(node);
        const int P = pins.size();
        QFont sf; sf.setPointSizeF(qMax(7.0, 9.0 * m_zoom));
        p.setFont(sf);
        // 每个 case 行左侧画 ×
        for (int r = 0; r < P; ++r) {
            if (!pins[r].key.startsWith("case_")) continue;
            float rowY = tl.y() + (kHeaderH + r * kRowH) * (float)m_zoom;
            float rowH = kRowH * (float)m_zoom;
            p.setPen(QColor(0xc0, 0x60, 0x60));
            p.drawText(QRectF(tl.x() + 4.0f * (float)m_zoom, rowY, 14.0f * (float)m_zoom, rowH),
                       Qt::AlignCenter, "×");
        }
        // 默认开关行 (row P)
        float defY = tl.y() + (kHeaderH + P * kRowH) * (float)m_zoom;
        float rowH = kRowH * (float)m_zoom;
        bool on = switchHasDefault(node);
        float sz = qMin(rowH - 6.0f, 13.0f * (float)m_zoom);
        QRectF box(tl.x() + 8.0f * (float)m_zoom, defY + (rowH - sz) * 0.5f, sz, sz);
        p.setBrush(on ? QColor(0x2a, 0x6a, 0x6a) : QColor(0x1c, 0x2d, 0x3e));
        p.setPen(QPen(QColor(0x2a, 0x50, 0x70), 1.0));
        p.drawRoundedRect(box, 2.0, 2.0);
        if (on) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor(0xe0, 0xff, 0xe0), qMax(1.2, 1.5 * m_zoom)));
            QPointF a(box.left() + sz * 0.24, box.center().y() + sz * 0.02);
            QPointF b(box.left() + sz * 0.42, box.bottom() - sz * 0.26);
            QPointF c(box.right() - sz * 0.20, box.top() + sz * 0.26);
            p.drawLine(a, b); p.drawLine(b, c);
        }
        p.setBrush(Qt::NoBrush);
        p.setPen(QColor(0xaa, 0xaa, 0xaa));
        p.drawText(QRectF(box.right() + 6.0f * (float)m_zoom, defY,
                          nw - (box.right() - tl.x()) - 10.0f * (float)m_zoom, rowH),
                   Qt::AlignVCenter | Qt::AlignLeft, "默认出口");
        // ＋加分支行 (row P+1)
        float addY = tl.y() + (kHeaderH + (P + 1) * kRowH) * (float)m_zoom;
        QRectF rc(tl.x() + 6.0f * (float)m_zoom, addY + 2.0f, nw - 12.0f * (float)m_zoom, rowH - 4.0f);
        p.setBrush(QColor(0x20, 0x28, 0x1f));
        p.setPen(QPen(QColor(0x3a, 0x5a, 0x3a), 1.0));
        p.drawRoundedRect(rc, 3.0, 3.0);
        p.setBrush(Qt::NoBrush);
        p.setPen(QColor(0x7b, 0xbf, 0x7b));
        p.drawText(rc, Qt::AlignCenter, "＋ 加分支");
        p.setFont(font);
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
            m_dragState = DragState::None;
        }
        return;
    }
    if (m_paramEditPopup && m_paramEditPopup->isVisible()) {
        if (!m_paramEditPopup->geometry().contains(e->pos())) {
            hideParamEditPopup();
        }
        return;
    }
    if (m_uiAssetPopup && m_uiAssetPopup->isVisible()) {
        if (!m_uiAssetPopup->geometry().contains(e->pos()))
            hideUIAssetPicker();
    }
    setFocus();
    if (e->button() != Qt::LeftButton) return;

    Hit hit = hitTest(e->position());

    // 点中节点上的任意位置（含 pin/value 区域）都算选中该节点
    if (hit.type == Hit::Pin || hit.type == Hit::Node || hit.type == Hit::PinValue) {
        m_selectedNodeId = hit.nodeId;
        m_selectedConnId.clear();
    }

    // 点击连接线
    if (hit.type == Hit::Wire) {
        m_selectedConnId = hit.connId;
        m_selectedNodeId.clear();
        update();
        return;
    }

    // 点击引脚值区域：按引脚 kind 选择对应内联编辑器（学虚幻，类型驱动）
    if (hit.type == Hit::PinValue) {
        const BPNode* node     = findNode(hit.nodeId);
        const QString nodeType = node ? node->type : QString();
        const QString cur      = node ? node->params.value(hit.pinName) : QString();
        switch (pinKindOf(nodeType, hit.pinName)) {
        case ValueKind::ActorRef:
            showListPicker(e->pos(), hit.nodeId, hit.pinName, "选择对象", buildActorItems(), cur);
            break;
        case ValueKind::UIRef:
            showUIAssetPicker(e->pos(), hit.nodeId);   // 特殊：含控件展开
            break;
        case ValueKind::LevelRef:
            showListPicker(e->pos(), hit.nodeId, hit.pinName, "选择关卡", buildLevelItems(), cur);
            break;
        case ValueKind::WidgetRef:
            showListPicker(e->pos(), hit.nodeId, hit.pinName, "选择控件", buildWidgetItems(), cur);
            break;
        case ValueKind::Bool:
            toggleBoolParam(hit.nodeId, hit.pinName);  // 勾选框：点一下切换
            break;
        default:
            showInlineEdit(hit.nodeId, hit.pinName);   // 自由文本/数值 → 打字
            break;
        }
        update();
        return;
    }

    // 分支控制可点区域
    if (hit.type == Hit::SwitchAdd) {
        m_selectedNodeId = hit.nodeId; addSwitchBranch(hit.nodeId); return;
    }
    if (hit.type == Hit::SwitchDel) {
        m_selectedNodeId = hit.nodeId; removeSwitchBranch(hit.nodeId, hit.pinName); return;
    }
    if (hit.type == Hit::SwitchDefault) {
        m_selectedNodeId = hit.nodeId; toggleSwitchDefault(hit.nodeId); return;
    }
    if (hit.type == Hit::SwitchValue) {
        m_selectedNodeId = hit.nodeId;
        showInlineEdit(hit.nodeId, "case_" + hit.pinName);  // 写回分支比较值
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
        if (n) {
            m_nodeBeforeDrag = *n;
            m_dragOffset = screenToCanvas(e->position()) - QPointF(n->x, n->y);
        }
        update();
        return;
    }

    // 空白处：开始平移，松开时根据移动量决定是否清除选中
    m_panning     = true;
    m_lastMouse   = e->pos();
    m_panStartPos = e->pos();
    setCursor(Qt::ClosedHandCursor);
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
    if (e->button() != Qt::LeftButton) return;

    if (m_panning) {
        m_panning = false;
        setCursor(Qt::ArrowCursor);
        if ((e->pos() - m_panStartPos).manhattanLength() < 4) {
            m_selectedNodeId.clear();
            m_selectedConnId.clear();
            update();
        }
        return;
    }

    if (m_dragState == DragState::DraggingNode && (m_doc || m_bpClass)) {
        const BPNode* n = findNode(m_draggingNodeId);
        if (n) {
            // 判断是否有实际移动
            if (n->x != m_nodeBeforeDrag.x || n->y != m_nodeBeforeDrag.y) {
                BPNode after = *n;
                auto refresh = [this]() { notifyModified(); update(); };
                m_bpUndoStack->push(new BPNodeMoveCmd(m_bpClass, m_doc, m_nodeBeforeDrag, after, refresh));
            } else {
                updateNodeInActive(m_bpClass, m_doc, *n);
            }
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
                // 查找需被挤掉的旧连接（displaced）。
                // 虚幻规则：exec 输出只能连一根（按输出端去重，输入端允许扇入多连）；
                //          数据输入只能接一根（按输入端去重，输出端允许多连）。
                const bool isExecConn = isPinExec(fromNodePtr->type, fromPin, true);
                BPConnection displaced;
                bool hasDisplaced = false;
                for (const BPConnection& c : activeConns()) {
                    const bool conflict = isExecConn
                        ? (c.fromNode == fromNode && c.fromPin == fromPin)   // exec：同一输出端
                        : (c.toNode   == toNode   && c.toPin   == toPin);    // data：同一输入端
                    if (conflict) {
                        displaced = c;
                        hasDisplaced = true;
                        break;
                    }
                }
                BPConnection conn;
                conn.id       = QUuid::createUuid().toString(QUuid::WithoutBraces);
                conn.fromNode = fromNode;
                conn.fromPin  = fromPin;
                conn.toNode   = toNode;
                conn.toPin    = toPin;
                auto refresh = [this]() { notifyModified(); update(); };
                m_bpUndoStack->push(new BPConnectionAddCmd(
                    m_bpClass, m_doc, conn, displaced, hasDisplaced, refresh));
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

void BlueprintEditor::frameAll() {
    const auto& nodes = activeNodes();
    if (nodes.isEmpty()) { m_offset = {0, 0}; m_zoom = 1.0f; update(); return; }
    float minX = nodes[0].x, maxX = nodes[0].x + kNodeW;
    float minY = nodes[0].y, maxY = nodes[0].y + nodeHeight(nodes[0]);
    for (const BPNode& n : nodes) {
        minX = qMin(minX, n.x);
        maxX = qMax(maxX, n.x + kNodeW);
        minY = qMin(minY, n.y);
        maxY = qMax(maxY, n.y + nodeHeight(n));
    }
    float pad = 60.0f;
    float zoomX = (float)width()  / ((maxX - minX) + pad * 2);
    float zoomY = (float)height() / ((maxY - minY) + pad * 2);
    m_zoom = qBound(0.2f, qMin(zoomX, zoomY), 2.0f);
    float cx = (minX + maxX) * 0.5f, cy = (minY + maxY) * 0.5f;
    m_offset = QPointF(-cx * m_zoom, -cy * m_zoom);
    update();
}

void BlueprintEditor::duplicateSelectedNode() {
    if (m_selectedNodeId.isEmpty() || (!m_doc && !m_bpClass)) return;
    const BPNode* src = findNode(m_selectedNodeId);
    if (!src) return;
    BPNode copy = *src;
    copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    copy.x += 20.0f;
    copy.y += 20.0f;
    auto refresh = [this]() { notifyModified(); update(); };
    m_bpUndoStack->push(new BPNodeAddCmd(m_bpClass, m_doc, copy, refresh));
    m_selectedNodeId = copy.id;
    update();
}

void BlueprintEditor::deleteSelected() {
    if (!m_doc && !m_bpClass) return;
    auto refresh = [this]() { notifyModified(); update(); };
    if (!m_selectedConnId.isEmpty()) {
        BPConnection conn;
        for (const BPConnection& c : activeConns())
            if (c.id == m_selectedConnId) { conn = c; break; }
        m_bpUndoStack->push(new BPConnectionRemoveCmd(m_bpClass, m_doc, conn, refresh));
        m_selectedConnId.clear();
        return;
    }
    if (!m_selectedNodeId.isEmpty()) {
        const BPNode* n = findNode(m_selectedNodeId);
        if (n) {
            QList<BPConnection> related;
            for (const BPConnection& c : activeConns())
                if (c.fromNode == m_selectedNodeId || c.toNode == m_selectedNodeId)
                    related << c;
            m_bpUndoStack->push(new BPNodeRemoveCmd(m_bpClass, m_doc, *n, related, refresh));
            m_selectedNodeId.clear();
        }
    }
}

void BlueprintEditor::keyPressEvent(QKeyEvent* e) {
    QWidget::keyPressEvent(e);
}

// ── 右键菜单 ──────────────────────────────────────────────────────────

void BlueprintEditor::contextMenuEvent(QContextMenuEvent* e) {
    if (!m_doc && !m_bpClass) return;
    Hit hit = hitTest(QPointF(e->pos()));

    // 右键点在节点上：对象操作菜单
    if (hit.type == Hit::Node || hit.type == Hit::Pin || hit.type == Hit::PinValue) {
        const QString nodeId = hit.nodeId;
        const BPNode* n = findNode(nodeId);
        if (!n) return;
        const NodeDef* def = findNodeDef(n->type);
        QString label = def ? def->displayName : n->type;
        QMenu menu(this);
        menu.addAction("删除节点 "" + label + """, [this, nodeId, n]() {
            QList<BPConnection> related;
            for (const BPConnection& c : activeConns())
                if (c.fromNode == nodeId || c.toNode == nodeId)
                    related << c;
            auto refresh = [this]() { notifyModified(); update(); };
            m_bpUndoStack->push(new BPNodeRemoveCmd(m_bpClass, m_doc, *n, related, refresh));
            if (m_selectedNodeId == nodeId) m_selectedNodeId.clear();
        });
        menu.exec(e->globalPos());
        return;
    }

    // 右键点在连接线上：断开连接
    if (hit.type == Hit::Wire) {
        const QString connId = hit.connId;
        QMenu menu(this);
        menu.addAction("删除连接", [this, connId]() {
            BPConnection conn;
            for (const BPConnection& c : activeConns())
                if (c.id == connId) { conn = c; break; }
            auto refresh = [this]() { notifyModified(); update(); };
            m_bpUndoStack->push(new BPConnectionRemoveCmd(m_bpClass, m_doc, conn, refresh));
            if (m_selectedConnId == connId) m_selectedConnId.clear();
        });
        menu.exec(e->globalPos());
        return;
    }

    // 空白处：弹出节点创建菜单
    QPointF canvasPos = screenToCanvas(e->pos());

    QMenu menu(this);
    auto* eventMenu  = menu.addMenu("事件");
    auto* keyMenu    = eventMenu->addMenu("键盘事件");
    auto* actionMenu = menu.addMenu("动作");
    auto* flowMenu   = menu.addMenu("流程控制");
    auto* varMenu    = menu.addMenu("变量");
    auto* mathMenu   = menu.addMenu("数学");
    auto* logicMenu  = menu.addMenu("逻辑");
    auto* selfMenu   = menu.addMenu("Self");
    auto* uiMenu     = menu.addMenu("UI");

    for (const NodeDef& def : nodeDefs()) {
        if (!isSelfNodeVisible(def.typeId)) continue;
        QMenu* target = def.typeId.startsWith("Event.Key.")  ? keyMenu    :
                        def.typeId.startsWith("Event.")      ? eventMenu  :
                        def.typeId.startsWith("Action.")     ? actionMenu :
                        def.typeId.startsWith("Flow.")       ? flowMenu   :
                        def.typeId.startsWith("UI.")         ? uiMenu     :
                        def.typeId.startsWith("Self.")       ? selfMenu   :
                        def.typeId.startsWith("Math.")       ? mathMenu   :
                        def.typeId.startsWith("Logic.")      ? logicMenu  : varMenu;
        const QString typeId = def.typeId;
        target->addAction(def.displayName, [this, typeId, canvasPos]() {
            if (!m_doc && !m_bpClass) return;
            BPNode node;
            node.id   = QUuid::createUuid().toString(QUuid::WithoutBraces);
            node.type = typeId;
            node.x    = (float)canvasPos.x();
            node.y    = (float)canvasPos.y();
            seedSwitchDefaults(node);
            auto refresh = [this]() { notifyModified(); update(); };
            m_bpUndoStack->push(new BPNodeAddCmd(m_bpClass, m_doc, node, refresh));
            m_selectedNodeId = node.id;
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
    m_dragState = DragState::None;  // 弹窗显示后不再处于拖线状态

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
                          def.typeId.startsWith("Self.")   ? "Self"    :
                          def.typeId.startsWith("Math.")   ? "数学"    :
                          def.typeId.startsWith("Logic.")  ? "逻辑"    : "变量";
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
    seedSwitchDefaults(node);
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
    auto refresh = [this]() { notifyModified(); update(); };
    m_bpUndoStack->beginMacro("拖线创建节点");
    m_bpUndoStack->push(new BPNodeAddCmd(m_bpClass, m_doc, node, refresh));
    m_bpUndoStack->push(new BPConnectionAddCmd(m_bpClass, m_doc, conn, {}, false, refresh));
    m_bpUndoStack->endMacro();

    m_selectedNodeId = node.id;

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
    int extraRows = (node->type == "Var.ActorRef" || nodeHasUIPicker(node->type)) ? 1 : 0;
    int row = 0;
    for (const PinDef& pd : effectivePins(*node)) {
        if (pd.key == pinKey) break;
        ++row;
    }

    QPointF tl  = canvasToScreen({node->x, node->y});
    float   nw  = kNodeW * (float)m_zoom;
    float   rowY = (float)(tl.y() + (kHeaderH + (row + extraRows) * kRowH) * m_zoom);
    float   rowH = kRowH * (float)m_zoom;
    float   x0  = (float)(tl.x() + nw * 0.5f);
    float   x1  = (float)(tl.x() + nw - 6.0);

    m_inlineEditNodeId = nodeId;
    m_inlineEditPinKey = pinKey;

    // 分支控制的 case_<id> 引脚：编辑的是分支比较值，初值取自 branches
    m_inlineSwitchBranchId.clear();
    QString initText = node->params.value(pinKey);
    if (node->type == "Flow.Switch" && pinKey.startsWith("case_")) {
        m_inlineSwitchBranchId = pinKey.mid(5);
        for (const SwitchBranch& b : parseSwitchBranches(node->params.value("branches")))
            if (b.id == m_inlineSwitchBranchId) { initText = b.value; break; }
    }

    m_inlineEdit = new QLineEdit(this);
    m_inlineEdit->setText(initText);
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

    const BPNode* cur = findNode(m_inlineEditNodeId);
    if (cur) {
        if (!m_inlineSwitchBranchId.isEmpty()) {
            // 分支控制：写回某分支的比较值（走 Undo）
            BPNode before = *cur, after = *cur;
            auto branches = parseSwitchBranches(after.params.value("branches"));
            for (SwitchBranch& b : branches)
                if (b.id == m_inlineSwitchBranchId) { b.value = val; break; }
            after.params["branches"] = serializeSwitchBranches(branches);
            m_bpUndoStack->push(new BPNodeModifyCmd(m_bpClass, m_doc, before, after, "修改分支值",
                                [this]{ notifyModified(); update(); }));
        } else {
            // 普通引脚值：直接写回（沿用既有行为）
            BPNode node = *cur;
            node.params[m_inlineEditPinKey] = val;
            updateNodeInActive(m_bpClass, m_doc, node);
            notifyModified();
        }
    }
    m_inlineEditNodeId.clear();
    m_inlineEditPinKey.clear();
    m_inlineSwitchBranchId.clear();
    update();
}

void BlueprintEditor::cancelInlineEdit() {
    if (!m_inlineEdit) return;
    m_inlineEdit->hide();
    m_inlineEdit->deleteLater();
    m_inlineEdit = nullptr;
    update();
}

// ── 引脚值类型分发 ────────────────────────────────────────────────────

BlueprintEditor::ValueKind
BlueprintEditor::pinKindOf(const QString& typeId, const QString& key) const {
    // uiName 是合成参数（无对应 PinDef），按 key 名识别
    if (key == "uiName") return ValueKind::UIRef;
    const NodeDef* def = findNodeDef(typeId);
    if (def)
        for (const PinDef& pd : def->pins)
            if (pd.key == key) return pd.kind;
    return ValueKind::Text;
}

void BlueprintEditor::toggleBoolParam(const QString& nodeId, const QString& pinKey) {
    if (!m_doc && !m_bpClass) return;
    for (BPNode node : activeNodes()) {     // 拷贝查找，再 update
        if (node.id == nodeId) {
            const QString cur = node.params.value(pinKey).toLower();
            const bool now = (cur == "true" || cur == "1");
            node.params[pinKey] = now ? "false" : "true";
            updateNodeInActive(m_bpClass, m_doc, node);
            notifyModified();
            break;
        }
    }
    update();
}

// ── 分支控制编辑 ──────────────────────────────────────────────────────

void BlueprintEditor::addSwitchBranch(const QString& nodeId) {
    const BPNode* cur = findNode(nodeId);
    if (!cur) return;
    BPNode before = *cur, after = *cur;
    auto branches = parseSwitchBranches(after.params.value("branches"));
    branches.append({QUuid::createUuid().toString(QUuid::WithoutBraces), ""});
    after.params["branches"] = serializeSwitchBranches(branches);
    m_bpUndoStack->push(new BPNodeModifyCmd(m_bpClass, m_doc, before, after, "加分支",
                        [this]{ notifyModified(); update(); }));
    update();
}

void BlueprintEditor::removeSwitchBranch(const QString& nodeId, const QString& branchId) {
    const BPNode* cur = findNode(nodeId);
    if (!cur) return;
    BPNode before = *cur, after = *cur;
    auto branches = parseSwitchBranches(after.params.value("branches"));
    branches.removeIf([&](const SwitchBranch& b){ return b.id == branchId; });
    after.params["branches"] = serializeSwitchBranches(branches);
    // 连带删除连到该分支出口 case_<id> 的连线
    const QString pinKey = "case_" + branchId;
    QList<BPConnection> related;
    for (const BPConnection& c : activeConns())
        if (c.fromNode == nodeId && c.fromPin == pinKey) related << c;
    auto refresh = [this]{ notifyModified(); update(); };
    m_bpUndoStack->beginMacro("删分支");
    for (const BPConnection& c : related)
        m_bpUndoStack->push(new BPConnectionRemoveCmd(m_bpClass, m_doc, c, refresh));
    m_bpUndoStack->push(new BPNodeModifyCmd(m_bpClass, m_doc, before, after, "删分支", refresh));
    m_bpUndoStack->endMacro();
    update();
}

void BlueprintEditor::toggleSwitchDefault(const QString& nodeId) {
    const BPNode* cur = findNode(nodeId);
    if (!cur) return;
    BPNode before = *cur, after = *cur;
    const bool now = switchHasDefault(*cur);
    after.params["hasDefault"] = now ? "false" : "true";
    auto refresh = [this]{ notifyModified(); update(); };
    if (now) {
        // 关闭默认出口：清理连到 default 的连线
        QList<BPConnection> related;
        for (const BPConnection& c : activeConns())
            if (c.fromNode == nodeId && c.fromPin == "default") related << c;
        m_bpUndoStack->beginMacro("关闭默认出口");
        for (const BPConnection& c : related)
            m_bpUndoStack->push(new BPConnectionRemoveCmd(m_bpClass, m_doc, c, refresh));
        m_bpUndoStack->push(new BPNodeModifyCmd(m_bpClass, m_doc, before, after, "关闭默认出口", refresh));
        m_bpUndoStack->endMacro();
    } else {
        m_bpUndoStack->push(new BPNodeModifyCmd(m_bpClass, m_doc, before, after, "启用默认出口", refresh));
    }
    update();
}

// ── 下拉选项来源 ──────────────────────────────────────────────────────

QList<QPair<QString, QString>> BlueprintEditor::buildActorItems() const {
    QList<QPair<QString, QString>> items;
    if (m_doc)
        for (const ActorData& a : m_doc->actors())
            items.append({a.name, a.id});
    return items;
}

QList<QPair<QString, QString>> BlueprintEditor::buildLevelItems() const {
    QList<QPair<QString, QString>> items;
    if (m_projectRoot.isEmpty()) return items;
    QDir dir(m_projectRoot + "/Levels");
    for (const QString& fn : dir.entryList({"*.level"}, QDir::Files, QDir::Name)) {
        const QString name = QFileInfo(fn).baseName();
        items.append({name, name});   // 关卡名既是显示也是写回值
    }
    return items;
}

QList<QPair<QString, QString>> BlueprintEditor::buildWidgetItems() const {
    QList<QPair<QString, QString>> items;
    if (m_projectRoot.isEmpty()) return items;
    QDir uiDir(m_projectRoot + "/UI");
    for (const QString& fn : uiDir.entryList({"*.ui"}, QDir::Files, QDir::Name)) {
        const QString uiName = QFileInfo(fn).baseName();
        for (const QString& w : loadWidgetNames(uiName))
            items.append({uiName + "/" + w, uiName + "::" + w});  // 显示 UI名/控件名，写回 UI名::控件名
    }
    return items;
}

// ── 通用下拉列表选择器 ────────────────────────────────────────────────

void BlueprintEditor::showListPicker(QPoint screenPos, const QString& nodeId, const QString& pinKey,
                                     const QString& title,
                                     const QList<QPair<QString, QString>>& items,
                                     const QString& current) {
    hideParamEditPopup();
    if (!m_doc && !m_bpClass) return;

    m_paramEditNodeId = nodeId;
    m_paramEditPinKey = pinKey;

    m_paramEditPopup = new QFrame(this);
    m_paramEditPopup->setObjectName("paramEditPopup");
    m_paramEditPopup->setStyleSheet(
        "QFrame#paramEditPopup {"
        "  background:#252526; border:1px solid #454545; border-radius:6px; }"
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
    auto* titleLbl = new QLabel(title, titleBar);
    titleLbl->setStyleSheet("color:#aaa; font-size:11px; font-weight:bold; background:transparent;");
    titleLay->addWidget(titleLbl);
    titleLay->addStretch();
    vlay->addWidget(titleBar);

    auto* list = new QListWidget(m_paramEditPopup);
    QFont lf; lf.setPointSize(10); list->setFont(lf);

    auto* clearItem = new QListWidgetItem("（清空）", list);
    clearItem->setForeground(QColor("#888"));
    clearItem->setData(Qt::UserRole, QString(""));

    if (items.isEmpty()) {
        auto* none = new QListWidgetItem("（无可选项）", list);
        none->setForeground(QColor("#666"));
        none->setFlags(Qt::NoItemFlags);
    }
    for (const auto& it : items) {
        auto* item = new QListWidgetItem(it.first, list);
        item->setData(Qt::UserRole, it.second);
        if (it.second == current) list->setCurrentItem(item);
    }

    connect(list, &QListWidget::itemClicked, m_paramEditPopup, [this](QListWidgetItem* item) {
        if (!(item->flags() & Qt::ItemIsEnabled)) return;
        onParamValueConfirmed(item->data(Qt::UserRole).toString());
    });
    list->installEventFilter(this);
    vlay->addWidget(list, 1);

    int popW = 220;
    int itemCount = qMin(list->count(), 8);
    int popH = 42 + itemCount * 28 + 12;

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
    m_selectedConnId.clear();
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
    m_bpUndoStack->clear();
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

// ── UI 资产选择器 ──────────────────────────────────────────────────────

void BlueprintEditor::setProjectRoot(const QString& root) {
    m_projectRoot = root;
}

void BlueprintEditor::hideUIAssetPicker() {
    if (!m_uiAssetPopup) return;
    m_uiAssetPopup->hide();
    m_uiAssetPopup->deleteLater();
    m_uiAssetPopup = nullptr;
    m_uiAssetNodeId.clear();
}

void BlueprintEditor::showUIAssetPicker(QPoint screenPos, const QString& nodeId) {
    hideUIAssetPicker();
    if (m_projectRoot.isEmpty()) return;

    // 判断节点类型：UI.Show / UI.Hide 需要展开到控件级
    QString nodeType;
    for (const BPNode& n : activeNodes())
        if (n.id == nodeId) { nodeType = n.type; break; }
    const bool showWidgets = (nodeType == "UI.Show" || nodeType == "UI.Hide");

    // 扫描 UI/ 目录下的 .ui 文件
    QDir uiDir(m_projectRoot + "/UI");
    QStringList uiNames;
    for (const QString& fn : uiDir.entryList({"*.ui"}, QDir::Files))
        uiNames << QFileInfo(fn).baseName();

    // 构建条目列表：value = 存储值（"uiName" 或 "uiName::widget"），display = 显示文字
    struct Entry { QString value; QString display; };
    QList<Entry> entries;
    for (const QString& uiName : uiNames) {
        if (showWidgets) {
            entries.append({uiName, uiName + "（整体）"});
            for (const QString& w : loadWidgetNames(uiName))
                entries.append({uiName + "::" + w, "  " + w});
        } else {
            entries.append({uiName, uiName});
        }
    }

    m_uiAssetNodeId = nodeId;
    m_uiAssetPopup  = new QFrame(this);
    m_uiAssetPopup->setObjectName("uiAssetPopup");
    m_uiAssetPopup->setStyleSheet(
        "QFrame#uiAssetPopup {"
        "  background:#252526; border:1px solid #454545; border-radius:6px; }"
        "QListWidget {"
        "  background:transparent; color:#ccc; border:none; outline:0; }"
        "QListWidget::item { padding:4px 10px; }"
        "QListWidget::item:hover { background:#3a3a5a; color:#fff; }");

    auto* layout = new QVBoxLayout(m_uiAssetPopup);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);

    if (entries.isEmpty()) {
        auto* emptyLbl = new QLabel("（无 UI 文件）", m_uiAssetPopup);
        emptyLbl->setStyleSheet("color:#666; padding:6px;");
        layout->addWidget(emptyLbl);
    } else {
        auto* list = new QListWidget(m_uiAssetPopup);
        for (const Entry& e : entries) {
            auto* item = new QListWidgetItem(e.display, list);
            item->setData(Qt::UserRole, e.value);
            // UI文件行（整体）加粗
            if (!e.value.contains("::")) {
                QFont f = item->font(); f.setBold(true);
                item->setFont(f);
            }
        }
        list->setFixedHeight(qMin(240, list->count() * 24 + 8));
        layout->addWidget(list);

        connect(list, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
            const QString value = item->data(Qt::UserRole).toString();
            const QString nid   = m_uiAssetNodeId;
            hideUIAssetPicker();
            if (nid.isEmpty()) return;
            // 断开 widgetRef 输入连线（嵌入选择器优先）
            for (const BPConnection& c : activeConns()) {
                if (c.toNode == nid && c.toPin == "widgetRef") {
                    removeConnFromActive(m_bpClass, m_doc, c.id);
                    break;
                }
            }
            for (BPNode node : activeNodes()) {
                if (node.id == nid) {
                    const QString uiName = value.contains("::") ? value.left(value.indexOf("::")) : value;
                    m_uiWidgetCache.remove(uiName);
                    node.params["uiName"] = value;
                    updateNodeInActive(m_bpClass, m_doc, node);
                    notifyModified();
                    break;
                }
            }
            update();
        });
    }

    m_uiAssetPopup->adjustSize();
    // 弹窗位置：不超出边界
    QPoint pos = screenPos + QPoint(0, 4);
    if (pos.x() + m_uiAssetPopup->width()  > width())
        pos.setX(width() - m_uiAssetPopup->width() - 4);
    if (pos.y() + m_uiAssetPopup->height() > height())
        pos.setY(screenPos.y() - m_uiAssetPopup->height() - 4);
    m_uiAssetPopup->move(pos);
    m_uiAssetPopup->show();
    m_uiAssetPopup->raise();
}

QString BlueprintEditor::currentBpClassPath() const {
    return m_bpClass ? m_bpClass->filePath : QString();
}
