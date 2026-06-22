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
#include <QInputDialog>
#include <QMessageBox>

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

// 分支比较值字面量：优先 params["caseval_<id>"]；兼容旧数据（曾存于 branches JSON 的 value）
QString switchCaseValue(const BPNode& node, const QString& id) {
    const QString v = node.params.value("caseval_" + id);
    if (!v.isEmpty()) return v;
    for (const SwitchBranch& b : parseSwitchBranches(node.params.value("branches")))
        if (b.id == id) return b.value;
    return QString();
}

// 无静态 NodeDef 的动态节点（绘制/命中需特殊容忍）：
// 宏调用 Macro:: / 入口出口 Macro.Entry|Exit / 全局变量 Global.Get|Set
bool isMacroNodeType(const QString& t) {
    return t.startsWith("Macro::") || t == "Macro.Entry" || t == "Macro.Exit"
        || t == "Global.Get" || t == "Global.Set";
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

// 已弃用节点：保留求值与渲染（旧蓝图可正常打开运行），但从创建菜单隐藏。
// 这些已被新版下拉运算节点取代。
static bool isDeprecatedNodeType(const QString& typeId) {
    static const QSet<QString> dep = {
        "Logic.Compare"   // 旧「数值比较」(op 输入式)，已被 Cmp.* 系列取代
    };
    return dep.contains(typeId);
}

// 数组变量操作节点：varName 由「全局变量」菜单按变量烤入，因此从通用创建菜单隐藏。
static bool isVarBoundArrayOp(const QString& typeId) {
    static const QSet<QString> ops = {
        "Array.Add", "Array.RemoveAt", "Array.RemoveValue", "Array.SetAt", "Array.Clear"
    };
    return ops.contains(typeId);
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
        // ── 数学运算（虚幻式：一符一节点，标题即符号）───────────────────
        {
            "Math.Add", "+", QColor("#1a2a5a"),
            {{"a","A",false,false,ValueKind::Number},{"b","B",false,false,ValueKind::Number},
             {"result","",false,true,ValueKind::Number}}
        },
        {
            "Math.Sub", "-", QColor("#1a2a5a"),
            {{"a","A",false,false,ValueKind::Number},{"b","B",false,false,ValueKind::Number},
             {"result","",false,true,ValueKind::Number}}
        },
        {
            "Math.Mul", "×", QColor("#1a2a5a"),
            {{"a","A",false,false,ValueKind::Number},{"b","B",false,false,ValueKind::Number},
             {"result","",false,true,ValueKind::Number}}
        },
        {
            "Math.Div", "÷", QColor("#1a2a5a"),
            {{"a","A",false,false,ValueKind::Number},{"b","B",false,false,ValueKind::Number},
             {"result","",false,true,ValueKind::Number}}
        },
        {
            "Math.Mod", "%", QColor("#1a2a5a"),
            {{"a","A",false,false,ValueKind::Number},{"b","B",false,false,ValueKind::Number},
             {"result","",false,true,ValueKind::Number}}
        },
        {
            "Math.Clamp", "数值夹取", QColor("#1a2a5a"),
            {{"value","数值",false,false,ValueKind::Number},{"min","最小",false,false,ValueKind::Number},
             {"max","最大",false,false,ValueKind::Number},{"result","结果",false,true,ValueKind::Number}}
        },
        // ── 比较运算（虚幻式：一符一节点）─────────────────────────────────
        {
            "Cmp.GT", ">", QColor("#3a2a5a"),
            {{"a","A",false,false,ValueKind::Any},{"b","B",false,false,ValueKind::Any},
             {"result","",false,true,ValueKind::Bool}}
        },
        {
            "Cmp.GE", "≥", QColor("#3a2a5a"),
            {{"a","A",false,false,ValueKind::Any},{"b","B",false,false,ValueKind::Any},
             {"result","",false,true,ValueKind::Bool}}
        },
        {
            "Cmp.LT", "<", QColor("#3a2a5a"),
            {{"a","A",false,false,ValueKind::Any},{"b","B",false,false,ValueKind::Any},
             {"result","",false,true,ValueKind::Bool}}
        },
        {
            "Cmp.LE", "≤", QColor("#3a2a5a"),
            {{"a","A",false,false,ValueKind::Any},{"b","B",false,false,ValueKind::Any},
             {"result","",false,true,ValueKind::Bool}}
        },
        {
            "Cmp.EQ", "=", QColor("#3a2a5a"),
            {{"a","A",false,false,ValueKind::Any},{"b","B",false,false,ValueKind::Any},
             {"result","",false,true,ValueKind::Bool}}
        },
        {
            "Cmp.NE", "≠", QColor("#3a2a5a"),
            {{"a","A",false,false,ValueKind::Any},{"b","B",false,false,ValueKind::Any},
             {"result","",false,true,ValueKind::Bool}}
        },
        // ── 逻辑运算（虚幻式：一符一节点）─────────────────────────────────
        {
            "Logic.And", "与", QColor("#3a2a5a"),
            {{"a","A",false,false,ValueKind::Bool},{"b","B",false,false,ValueKind::Bool},
             {"result","",false,true,ValueKind::Bool}}
        },
        {
            "Logic.Or", "或", QColor("#3a2a5a"),
            {{"a","A",false,false,ValueKind::Bool},{"b","B",false,false,ValueKind::Bool},
             {"result","",false,true,ValueKind::Bool}}
        },
        {
            "Logic.Not", "非", QColor("#3a2a5a"),
            {{"value","",false,false,ValueKind::Bool},{"result","",false,true,ValueKind::Bool}}
        },
        // 旧「数值比较」（op 输入引脚式）：保留定义以渲染旧蓝图，已从菜单隐藏
        {
            "Logic.Compare", "数值比较", QColor("#3a2a5a"),
            {{"a","A",false,false},{"op","运算符",false,false},
             {"b","B",false,false},{"result","结果",false,true}}
        },
        // ── 数组数据节点（纯数据，作用于流入的数组值）─────────────────────
        {
            "Array.Make", "创建空数组", QColor("#2a4a4a"),
            {{"result","",false,true,ValueKind::Array}}
        },
        {
            "Array.Length", "数组长度", QColor("#2a4a4a"),
            {{"array","数组",false,false,ValueKind::Array},
             {"result","长度",false,true,ValueKind::Number}}
        },
        {
            "Array.Get", "获取元素", QColor("#2a4a4a"),
            {{"array","数组",false,false,ValueKind::Array},
             {"index","索引",false,false,ValueKind::Number},
             {"result","元素",false,true,ValueKind::Any}}
        },
        {
            "Array.Contains", "包含", QColor("#2a4a4a"),
            {{"array","数组",false,false,ValueKind::Array},
             {"value","值",false,false,ValueKind::Any},
             {"result","结果",false,true,ValueKind::Bool}}
        },
        // ── 数组变量操作节点（exec；varName 由「全局变量」菜单烤入，菜单中隐藏）──
        {
            "Array.Add", "添加元素", QColor("#2a4a4a"),
            {{"exec_in","exec",true,false},{"exec_out","exec",true,true},
             {"value","值",false,false,ValueKind::Any}}
        },
        {
            "Array.RemoveAt", "按索引移除", QColor("#2a4a4a"),
            {{"exec_in","exec",true,false},{"exec_out","exec",true,true},
             {"index","索引",false,false,ValueKind::Number}}
        },
        {
            "Array.RemoveValue", "按值移除", QColor("#2a4a4a"),
            {{"exec_in","exec",true,false},{"exec_out","exec",true,true},
             {"value","值",false,false,ValueKind::Any}}
        },
        {
            "Array.SetAt", "设置元素", QColor("#2a4a4a"),
            {{"exec_in","exec",true,false},{"exec_out","exec",true,true},
             {"index","索引",false,false,ValueKind::Number},
             {"value","值",false,false,ValueKind::Any}}
        },
        {
            "Array.Clear", "清空数组", QColor("#2a4a4a"),
            {{"exec_in","exec",true,false},{"exec_out","exec",true,true}}
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
    // 禁用自动右键菜单：右键拖拽=平移，右键单击=手动弹菜单
    setContextMenuPolicy(Qt::PreventContextMenu);
    m_bpUndoStack = new QUndoStack(this);
}

void BlueprintEditor::loadLevel(LevelDocument* doc) {
    m_bpClass = nullptr;
    m_doc = doc;
    clearNodeSelection();
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
BlueprintEditor::ValueKind BlueprintEditor::kindFromString(const QString& s) {
    if (s == "Bool")      return ValueKind::Bool;
    if (s == "LevelRef")  return ValueKind::LevelRef;
    if (s == "ActorRef")  return ValueKind::ActorRef;
    if (s == "UIRef")     return ValueKind::UIRef;
    if (s == "WidgetRef") return ValueKind::WidgetRef;
    if (s == "Number")    return ValueKind::Number;
    if (s == "Array")     return ValueKind::Array;
    if (s == "Any")       return ValueKind::Any;
    return ValueKind::Text;
}

QString BlueprintEditor::kindToString(ValueKind k) {
    switch (k) {
        case ValueKind::Bool:      return "Bool";
        case ValueKind::LevelRef:  return "LevelRef";
        case ValueKind::ActorRef:  return "ActorRef";
        case ValueKind::UIRef:     return "UIRef";
        case ValueKind::WidgetRef: return "WidgetRef";
        case ValueKind::Number:    return "Number";
        case ValueKind::Array:     return "Array";
        case ValueKind::Any:       return "Any";
        default:                   return "Text";
    }
}

BlueprintEditor::ValueKind BlueprintEditor::pinKindOf(
    const BPNode& node, const QString& pinKey, bool isOutput) const {
    for (const PinDef& pd : effectivePins(node))
        if (pd.key == pinKey && pd.isOutput == isOutput) return pd.kind;
    return ValueKind::Text;
}

bool BlueprintEditor::dataKindsCompatible(ValueKind from, ValueKind to) {
    if (from == ValueKind::Any || to == ValueKind::Any) return true;     // 通配
    const bool fromArr = (from == ValueKind::Array);
    const bool toArr   = (to   == ValueKind::Array);
    return fromArr == toArr;   // 同为数组或同为标量才允许；数组↔标量禁止
}

QColor BlueprintEditor::kindColor(ValueKind k) {
    switch (k) {
        case ValueKind::Number: return QColor(0x6c, 0xc6, 0x6c);  // 绿
        case ValueKind::Bool:   return QColor(0xd0, 0x6c, 0x6c);  // 红
        case ValueKind::Array:  return QColor(0x6c, 0x9c, 0xd6);  // 蓝
        case ValueKind::Any:    return QColor(0xdd, 0xdd, 0xdd);  // 中性浅灰
        default:                return QColor(0xcc, 0xcc, 0xcc);  // 文本/引用：原灰
    }
}

const BPMacro* BlueprintEditor::findMacro(const QString& id) const {
    auto it = m_macroCache.find(id);
    if (it != m_macroCache.end()) return &it.value();
    if (!m_projectRoot.isEmpty())
        for (const BPMacro& m : BPMacro::listAll(m_projectRoot))
            m_macroCache.insert(m.id, m);
    it = m_macroCache.find(id);
    return it != m_macroCache.end() ? &it.value() : nullptr;
}

bool BlueprintEditor::macroInterface(const BPNode& node, QList<MacroPin>& ins,
                                     QList<MacroPin>& outs) const {
    if (!node.type.startsWith("Macro::")) return false;
    if (node.type == "Macro::local") {
        const QString sub = node.params.value("subgraph");
        if (sub.isEmpty()) return false;
        const BPMacro m = BPMacro::fromJson(QJsonDocument::fromJson(sub.toUtf8()).object());
        ins = m.inputPins; outs = m.outputPins;
        return true;
    }
    const BPMacro* m = findMacro(node.type.mid(7));   // 去掉 "Macro::"
    if (!m) return false;
    ins = m->inputPins; outs = m->outputPins;
    return true;
}

QString BlueprintEditor::globalVarType(const QString& name) const {
    for (const GlobalVarDef& d : m_globalVarDefs)
        if (d.name == name) return d.type;
    return QString();
}

BlueprintEditor::ValueKind BlueprintEditor::kindFromGlobalType(const QString& type) const {
    if (type == "bool")   return ValueKind::Bool;
    if (type == "number") return ValueKind::Number;
    if (type.startsWith("array:")) return ValueKind::Array;
    if (type.startsWith("enum:"))  return ValueKind::EnumRef;
    return ValueKind::Text;   // string 打字
}

QList<QPair<QString, QString>>
BlueprintEditor::enumValuesForPin(const BPNode& node, const QString& key) const {
    QString enumName;
    if ((node.type == "Global.Get" || node.type == "Global.Set") && key == "value") {
        const QString t = globalVarType(node.params.value("varName"));
        if (t.startsWith("enum:")) enumName = t.mid(5);
    } else if (node.type == "Flow.Switch" && key.startsWith("caseval_")) {
        enumName = node.params.value("enum");
    }
    QList<QPair<QString, QString>> out;
    if (enumName.isEmpty()) return out;
    for (const EnumDef& e : m_enumDefs) {
        if (e.name != enumName) continue;
        for (int i = 0; i < e.values.size(); ++i) {
            const QString disp = (i < e.displays.size() && !e.displays[i].isEmpty())
                                 ? e.displays[i] : e.values[i];
            out.append({disp, e.values[i]});   // (显示名, 键值)
        }
        break;
    }
    return out;
}

BlueprintEditor::ValueKind
BlueprintEditor::pinKindForNode(const BPNode& node, const QString& key) const {
    for (const PinDef& pd : effectivePins(node))
        if (pd.key == key) return pd.kind;
    return pinKindOf(node.type, key);   // 兜底（如 uiName 合成参数）
}

QList<BlueprintEditor::PinDef> BlueprintEditor::effectivePins(const BPNode& node) const {
    // 全局变量节点：值引脚类型由变量声明决定
    if (node.type == "Global.Get") {
        return { {"value", QString(), false, true,
                  kindFromGlobalType(globalVarType(node.params.value("varName")))} };
    }
    if (node.type == "Global.Set") {
        return { {"exec_in",  "exec", true, false},
                 {"exec_out", "exec", true, true},
                 {"value", QString(), false, false,
                  kindFromGlobalType(globalVarType(node.params.value("varName")))} };
    }
    // 宏调用节点：引脚来自所引用宏的接口（输入在前、输出在后）
    if (node.type.startsWith("Macro::")) {
        QList<PinDef> pins;
        QList<MacroPin> ins, outs;
        if (macroInterface(node, ins, outs)) {
            for (const MacroPin& p : ins)
                pins.append({p.key, p.label, p.isExec, false, kindFromString(p.kind)});
            for (const MacroPin& p : outs)
                pins.append({p.key, p.label, p.isExec, true,  kindFromString(p.kind)});
        }
        return pins;
    }
    // 入口节点：输出引脚 = 正在编辑的宏的对外输入
    if (node.type == "Macro.Entry") {
        QList<PinDef> pins;
        if (m_editingMacro)
            for (const MacroPin& p : m_editingMacro->inputPins)
                pins.append({p.key, p.label, p.isExec, true, kindFromString(p.kind)});
        return pins;
    }
    // 出口节点：输入引脚 = 正在编辑的宏的对外输出
    if (node.type == "Macro.Exit") {
        QList<PinDef> pins;
        if (m_editingMacro)
            for (const MacroPin& p : m_editingMacro->outputPins)
                pins.append({p.key, p.label, p.isExec, false, kindFromString(p.kind)});
        return pins;
    }

    // 分支控制：exec_in + value（数据输入）；每个分支一行 = 比较值数据输入(caseval) + exec 出口(case)
    if (node.type == "Flow.Switch") {
        QList<PinDef> pins;
        pins.append({"exec_in", "exec", true,  false});
        pins.append({"value",   "值",   false, false});
        const bool enumMode = !node.params.value("enum").isEmpty();
        for (const SwitchBranch& b : parseSwitchBranches(node.params.value("branches"))) {
            pins.append({"caseval_" + b.id, QString(), false, false,
                         enumMode ? ValueKind::EnumRef : ValueKind::Text});  // 比较值
            pins.append({"case_" + b.id,    QString(), true,  true});        // exec 出口
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
    // 分支控制：可视行 = exec_in + value + 每分支一行 + [默认]，再加「默认开关」「＋加分支」两行
    if (node.type == "Flow.Switch") {
        const int nb = parseSwitchBranches(node.params.value("branches")).size();
        const int rows = 2 + nb + (switchHasDefault(node) ? 1 : 0);
        return kHeaderH + kRowH * (rows + 2) + 4.0f;
    }
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

    // 分支控制：每分支一行（caseval 左、case 右同一行），自定义行映射
    if (node.type == "Flow.Switch") {
        const auto branches = parseSwitchBranches(node.params.value("branches"));
        int row = -1;
        if (pinKey == "exec_in") row = 0;
        else if (pinKey == "value") row = 1;
        else if (pinKey == "default") row = 2 + branches.size();
        else {
            const QString id = pinKey.startsWith("caseval_") ? pinKey.mid(8)
                             : pinKey.startsWith("case_")    ? pinKey.mid(5) : QString();
            for (int i = 0; i < branches.size(); ++i)
                if (branches[i].id == id) { row = 2 + i; break; }
        }
        if (row < 0) return {};
        float cy = (float)(tl.y() + (kHeaderH + row * kRowH + kRowH * 0.5f) * m_zoom);
        return {isOutput ? rightX : leftX, cy};
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
    // 分支控制：value 和 caseval_* 是数据输入（可接任意数据源），其余（exec_in / case_* / default）才是 exec
    if (typeId == "Flow.Switch")
        return pinKey != "value" && !pinKey.startsWith("caseval_");
    if (typeId == "Global.Set") return pinKey == "exec_in" || pinKey == "exec_out";
    if (typeId == "Global.Get") return false;
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
        if (!def && !isMacroNodeType(node.type)) continue;

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
    // 分支控制的可点区域：值框编辑 / 分支 × 删除 / 默认开关 / ＋加分支
    // （置于 pin 圆点命中之后、普通 PinValue 之前；引脚本身由上面的 pin 圆点循环处理）
    for (int i = nodes.size() - 1; i >= 0; --i) {
        const BPNode& node = nodes[i];
        if (node.type != "Flow.Switch") continue;
        QPointF tl = canvasToScreen({node.x, node.y});
        const float nw = kNodeW * (float)m_zoom;
        const float rowH = kRowH * (float)m_zoom;
        auto rowYOf = [&](int r){ return (float)(tl.y() + (kHeaderH + r * kRowH) * m_zoom); };
        const auto branches = parseSwitchBranches(node.params.value("branches"));
        const float boxX0 = tl.x() + nw * 0.34f;
        const float boxX1 = tl.x() + nw - 30.0f * m_zoom;

        // value 行（row 1）：未连接时可编辑值框
        {
            const float rowY = rowYOf(1);
            if (screenPos.y() >= rowY && screenPos.y() <= rowY + rowH &&
                screenPos.x() >= boxX0 && screenPos.x() <= boxX1 &&
                !isPinConnected(node.id, "value", false)) {
                Hit h; h.type = Hit::SwitchValue; h.nodeId = node.id; h.pinName = "value"; return h;
            }
        }
        // 各分支行
        for (int b = 0; b < branches.size(); ++b) {
            const float rowY = rowYOf(2 + b);
            if (screenPos.y() < rowY || screenPos.y() > rowY + rowH) continue;
            const QString id = branches[b].id;
            // × 删除（case 出口左侧）
            if (screenPos.x() >= tl.x() + nw - 30.0f * m_zoom &&
                screenPos.x() <= tl.x() + nw - 14.0f * m_zoom) {
                Hit h; h.type = Hit::SwitchDel; h.nodeId = node.id; h.pinName = id; return h;
            }
            // 比较值框（未连接 caseval 时）
            if (screenPos.x() >= boxX0 && screenPos.x() <= boxX1 &&
                !isPinConnected(node.id, "caseval_" + id, false)) {
                Hit h; h.type = Hit::SwitchValue; h.nodeId = node.id;
                h.pinName = "caseval_" + id; return h;
            }
        }
        // 默认开关行 / ＋加分支行
        const int toggleRow = 2 + branches.size() + (switchHasDefault(node) ? 1 : 0);
        const float defY = rowYOf(toggleRow);
        if (screenPos.y() >= defY && screenPos.y() <= defY + rowH &&
            screenPos.x() >= tl.x() && screenPos.x() <= tl.x() + nw) {
            Hit h; h.type = Hit::SwitchDefault; h.nodeId = node.id; return h;
        }
        const float addY = rowYOf(toggleRow + 1);
        if (screenPos.y() >= addY && screenPos.y() <= addY + rowH &&
            screenPos.x() >= tl.x() && screenPos.x() <= tl.x() + nw) {
            Hit h; h.type = Hit::SwitchAdd; h.nodeId = node.id; return h;
        }
    }
    // PinValue 命中：ActorRef 选择器 + 普通数据输入引脚值区域
    const auto& nodes2 = activeNodes();
    for (int i = nodes2.size() - 1; i >= 0; --i) {
        const BPNode& node = nodes2[i];
        if (node.type == "Flow.Switch") continue;   // 分支控制值框由上面专门处理
        const NodeDef* def = findNodeDef(node.type);
        if (!def && !isMacroNodeType(node.type)) continue;
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

    // 框选矩形
    if (m_marquee) {
        const QRect r = QRect(m_marqueeStart, m_marqueeCur).normalized();
        p.setPen(QPen(QColor(0x5a, 0x9f, 0xd4), 1.0, Qt::DashLine));
        p.setBrush(QColor(0x5a, 0x9f, 0xd4, 40));
        p.drawRect(r);
    }

    // 宏内部编辑：左上角"返回"按钮 + 标题
    if (m_inMacroEdit) {
        const QRect btn(10, 10, 200, 30);
        p.setBrush(QColor(0x2a, 0x24, 0x3a));
        p.setPen(QPen(QColor(0x8a, 0x6a, 0xd4), 1.5));
        p.drawRoundedRect(btn, 4, 4);
        p.setPen(QColor(0xff, 0xff, 0xff));
        QFont f; f.setPointSizeF(11); p.setFont(f);
        p.drawText(btn, Qt::AlignCenter, "← 返回（编辑：" + m_macroEditInfo.name + "）");
    }
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

void BlueprintEditor::drawBezier(QPainter& p, QPointF from, QPointF to, bool isExec,
                                 ValueKind kind) {
    const QColor color = isExec ? QColor(0xe0, 0x7a, 0x30) : kindColor(kind);
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
        ValueKind kind = pinKindOf(*fromNode, conn.fromPin, true);

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
        drawBezier(p, from, to, isExec, kind);
    }
}

void BlueprintEditor::drawPin(QPainter& p, QPointF center, bool isExec, bool connected,
                              ValueKind kind) {
    const QColor execColor(0xe0, 0x7a, 0x30);
    const QColor dataColor = kindColor(kind);
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
    if (!def && !isMacroNodeType(node.type)) return;

    // 表头名称/颜色：普通节点取静态定义；宏节点动态推导
    QColor  headerColor;
    QString displayName;
    if (def) {
        headerColor = def->headerColor;
        displayName = def->displayName;
    } else if (node.type == "Macro.Entry") {
        headerColor = QColor("#6a6a3a"); displayName = "入口节点";
    } else if (node.type == "Macro.Exit") {
        headerColor = QColor("#6a6a3a"); displayName = "出口节点";
    } else if (node.type == "Global.Get" || node.type == "Global.Set") {
        headerColor = QColor("#1a3a4a");
        const QString vn = node.params.value("varName");
        const bool undefined = globalVarType(vn).isEmpty();
        displayName = (node.type == "Global.Get" ? "获取 " : "设置 ")
                    + (vn.isEmpty() ? "全局变量" : vn) + (undefined && !vn.isEmpty() ? " (未定义)" : "");
    } else {  // Macro:: 调用节点
        headerColor = QColor("#3a2a5a");
        QString nm = node.params.value("macroName");
        if (nm.isEmpty() && node.type != "Macro::local")
            if (const BPMacro* m = findMacro(node.type.mid(7))) nm = m->name;
        displayName = nm.isEmpty() ? "自定义节点" : nm;
    }

    // 数组变量操作节点：标题追加目标变量名
    if (node.type.startsWith("Array.")) {
        const QString vn = node.params.value("varName");
        if (!vn.isEmpty()) displayName += "：" + vn;
    }

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
    p.setBrush(headerColor);
    p.drawPath(headerPath);

    // 色块图标
    float iconSz = 8.0f * (float)m_zoom;
    float iconX  = tl.x() + 6.0f * (float)m_zoom;
    float iconY  = tl.y() + (hh - iconSz) * 0.5f;
    QColor iconColor = headerColor.lighter(150);
    p.setBrush(iconColor);
    p.drawRect(QRectF(iconX, iconY, iconSz, iconSz));

    // 节点标题文字
    float fontSize = qMax(8.0, 10.0 * m_zoom);
    QFont font; font.setPointSizeF(fontSize); font.setBold(false);
    p.setFont(font);
    p.setPen(QColor(0xff, 0xff, 0xff));
    float textX = iconX + iconSz + 5.0f * (float)m_zoom;
    QString titleText = displayName;
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
            if (m_selectedNodeIds.contains(node.id)) {
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
    if (node.type != "Flow.Switch")      // 分支控制自定义绘制，跳过通用引脚循环
    for (const PinDef& pd : effectivePins(node)) {
        QPointF pc = pinCenter(node, pd.key, pd.isOutput);
        bool connected = isPinConnected(node.id, pd.key, pd.isOutput);
        drawPin(p, pc, pd.isExec, connected, pd.kind);

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
                                          || pd.kind == ValueKind::WidgetRef
                                          || pd.kind == ValueKind::EnumRef);
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

    // 分支控制：完整自定义绘制（每分支：左值输入引脚 + 比较值框 + × + 右 exec 出口）
    if (node.type == "Flow.Switch") {
        const auto branches = parseSwitchBranches(node.params.value("branches"));
        QFont sf; sf.setPointSizeF(qMax(7.0, 9.0 * m_zoom)); p.setFont(sf);
        const float rowH = kRowH * (float)m_zoom;
        auto rowYOf = [&](int r){ return (float)(tl.y() + (kHeaderH + r * kRowH) * m_zoom); };

        const bool enumMode = !node.params.value("enum").isEmpty();
        auto drawValBox = [&](float rowY, const QString& text, bool connected, bool dropdown) {
            const float bx0 = (float)(tl.x() + nw * 0.34f);
            const float bx1 = (float)(tl.x() + nw - 30.0f * (float)m_zoom);
            QRectF boxRc(bx0, rowY + 2, bx1 - bx0, rowH - 4);
            if (connected) {
                p.setPen(QColor(0x88, 0x88, 0x88)); p.setBrush(Qt::NoBrush);
                p.drawText(boxRc, Qt::AlignVCenter | Qt::AlignRight, "(变量)");
                return;
            }
            p.setBrush(QColor(0x1c, 0x2d, 0x3e));
            p.setPen(QPen(QColor(0x2a, 0x50, 0x70), 1.0));
            p.drawRoundedRect(boxRc, 2.0, 2.0);
            p.setBrush(Qt::NoBrush);
            const QString disp = text.isEmpty() ? (dropdown ? "选择…" : "···") : text;
            p.setPen(text.isEmpty() ? QColor(0x55, 0x55, 0x55) : QColor(0x5a, 0x9f, 0xd4));
            p.drawText(boxRc.adjusted(3, 0, dropdown ? -12 : -3, 0), Qt::AlignVCenter | Qt::AlignRight, disp);
            if (dropdown) {
                p.setPen(QColor(0x88, 0xaa, 0xcc));
                p.drawText(boxRc.adjusted(0, 0, -3, 0), Qt::AlignVCenter | Qt::AlignRight, "▾");
            }
        };

        // exec_in
        drawPin(p, pinCenter(node, "exec_in", false), true, isPinConnected(node.id, "exec_in", false));
        p.setPen(QColor(0xaa, 0xaa, 0xaa));
        p.drawText(QRectF(tl.x() + 18 * (float)m_zoom, rowYOf(0), nw, rowH),
                   Qt::AlignVCenter | Qt::AlignLeft, "exec");
        // value（被判断的值）
        {
            const bool conn = isPinConnected(node.id, "value", false);
            drawPin(p, pinCenter(node, "value", false), false, conn);
            p.setPen(QColor(0xaa, 0xaa, 0xaa));
            p.drawText(QRectF(tl.x() + 18 * (float)m_zoom, rowYOf(1), nw * 0.3f, rowH),
                       Qt::AlignVCenter | Qt::AlignLeft, "值");
            drawValBox(rowYOf(1), node.params.value("value"), conn, false);
        }
        // 各分支
        for (int i = 0; i < branches.size(); ++i) {
            const QString id = branches[i].id;
            const float rowY = rowYOf(2 + i);
            const bool cvConn = isPinConnected(node.id, "caseval_" + id, false);
            drawPin(p, pinCenter(node, "caseval_" + id, false), false, cvConn);
            drawPin(p, pinCenter(node, "case_" + id, true), true, isPinConnected(node.id, "case_" + id, true));
            p.setPen(QColor(0xaa, 0xaa, 0xaa));
            p.drawText(QRectF(tl.x() + 18 * (float)m_zoom, rowY, 16 * (float)m_zoom, rowH),
                       Qt::AlignVCenter | Qt::AlignLeft, "=");
            drawValBox(rowY, switchCaseValue(node, id), cvConn, enumMode);
            p.setPen(QColor(0xc0, 0x60, 0x60));
            p.drawText(QRectF(tl.x() + nw - 28 * (float)m_zoom, rowY, 14 * (float)m_zoom, rowH),
                       Qt::AlignCenter, "×");
        }
        // default 出口
        if (switchHasDefault(node)) {
            const int row = 2 + branches.size();
            drawPin(p, pinCenter(node, "default", true), true, isPinConnected(node.id, "default", true));
            p.setPen(QColor(0xaa, 0xaa, 0xaa));
            p.drawText(QRectF(tl.x(), rowYOf(row), nw - 18 * (float)m_zoom, rowH),
                       Qt::AlignVCenter | Qt::AlignRight, "默认");
        }
        // 默认开关行 + 加分支行
        const int toggleRow = 2 + branches.size() + (switchHasDefault(node) ? 1 : 0);
        {
            const float defY = rowYOf(toggleRow);
            const bool on = switchHasDefault(node);
            const float sz = qMin(rowH - 6.0f, 13.0f * (float)m_zoom);
            QRectF box(tl.x() + 8 * (float)m_zoom, defY + (rowH - sz) * 0.5f, sz, sz);
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
            p.setBrush(Qt::NoBrush); p.setPen(QColor(0xaa, 0xaa, 0xaa));
            p.drawText(QRectF(box.right() + 6 * (float)m_zoom, defY, nw, rowH),
                       Qt::AlignVCenter | Qt::AlignLeft, "默认出口");
        }
        {
            const float addY = rowYOf(toggleRow + 1);
            QRectF rc(tl.x() + 6 * (float)m_zoom, addY + 2.0f, nw - 12 * (float)m_zoom, rowH - 4.0f);
            p.setBrush(QColor(0x20, 0x28, 0x1f));
            p.setPen(QPen(QColor(0x3a, 0x5a, 0x3a), 1.0));
            p.drawRoundedRect(rc, 3.0, 3.0);
            p.setBrush(Qt::NoBrush); p.setPen(QColor(0x7b, 0xbf, 0x7b));
            p.drawText(rc, Qt::AlignCenter, "＋ 加分支");
        }
        p.setFont(font);
    }

    // 选中高亮（最后绘制，保证在所有内容上方）
    if (m_selectedNodeIds.contains(node.id)) {
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
    ValueKind kind = pinKindOf(*fromNode, m_wireFromPin, m_wireFromIsOutput);

    if (!m_wireFromIsOutput) qSwap(from, to);
    drawBezier(p, from, to, isExec, kind);
}

// ── 滚轮缩放 ──────────────────────────────────────────────────────────

void BlueprintEditor::wheelEvent(QWheelEvent* e) {
    // 弹出列表（选择控件 / UI 资源 / 连线落点菜单）内滚动时，
    // 列表滚到顶/底边界会把滚轮事件冒泡到这里，不应缩放画布——直接放行给列表。
    const QPoint wp = e->position().toPoint();
    auto overPopup = [&](QWidget* p) {
        return p && p->isVisible() && p->geometry().contains(wp);
    };
    if (overPopup(m_paramEditPopup) || overPopup(m_uiAssetPopup) || overPopup(m_wireDropPopup)) {
        e->ignore();
        return;
    }
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

// ── 选中辅助 ──────────────────────────────────────────────────────────

void BlueprintEditor::selectSingleNode(const QString& id) {
    m_selectedNodeId = id;
    m_selectedNodeIds.clear();
    if (!id.isEmpty()) m_selectedNodeIds.insert(id);
    m_selectedConnId.clear();
}

void BlueprintEditor::clearNodeSelection() {
    m_selectedNodeId.clear();
    m_selectedNodeIds.clear();
}

// 折叠选中节点为一个本地自定义节点（宏）。
// 穿越选区边界的连线 → 自动推成入口/出口引脚；选区替换为一个 Macro::local 调用节点。
void BlueprintEditor::foldSelectionToMacro() {
    if (m_selectedNodeIds.isEmpty() || (!m_doc && !m_bpClass)) return;
    const QSet<QString> sel = m_selectedNodeIds;

    bool ok = false;
    const QString name = QInputDialog::getText(this, "折叠成节点", "节点名称：",
                             QLineEdit::Normal, "自定义节点", &ok).trimmed();
    if (!ok || name.isEmpty()) return;

    auto uuid = [] { return QUuid::createUuid().toString(QUuid::WithoutBraces); };

    // 选中节点快照 + 重心
    QList<BPNode> selNodes;
    QPointF centroid(0, 0);
    for (const QString& id : sel)
        if (const BPNode* n = findNode(id)) { selNodes << *n; centroid += QPointF(n->x, n->y); }
    if (selNodes.isEmpty()) return;
    centroid /= selNodes.size();

    const QList<BPConnection> connSnap = activeConns();

    // 某引脚是否连到"选区内的另一个节点"（连内部=隐藏；否则暴露为边界引脚）
    auto pinHasInternal = [&](const QString& nodeId, const QString& key, bool isOutput) -> bool {
        for (const BPConnection& c : connSnap) {
            if (isOutput) { if (c.fromNode == nodeId && c.fromPin == key && sel.contains(c.toNode))   return true; }
            else          { if (c.toNode   == nodeId && c.toPin   == key && sel.contains(c.fromNode)) return true; }
        }
        return false;
    };

    // 边界引脚 = 选区内每个节点上"未连到选区内其他节点"的引脚（含未连接的空引脚）
    struct Boundary { QString node, pin; bool isOutputPin, isExec; QString kind, label, pinKey; };
    QList<Boundary> boundaries;
    for (const BPNode& n : selNodes)
        for (const PinDef& pd : effectivePins(n)) {
            if (pinHasInternal(n.id, pd.key, pd.isOutput)) continue;  // 内部连接，隐藏
            boundaries.append({n.id, pd.key, pd.isOutput, pd.isExec, kindToString(pd.kind), pd.label,
                               (pd.isOutput ? "out_" : "in_") + uuid()});
        }

    if (boundaries.isEmpty()) {
        const auto ret = QMessageBox::question(this, "折叠成节点",
            "选区内节点没有任何对外引脚，折叠后将是一个无引脚的节点。\n仍要折叠吗？",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (ret != QMessageBox::Yes) return;
    }

    // 组装宏资产
    BPMacro macro;
    macro.id   = uuid();
    macro.name = name;
    macro.nodes = selNodes;
    BPNode entry; entry.id = uuid(); entry.type = "Macro.Entry";
    entry.x = centroid.x() - 240; entry.y = centroid.y();
    BPNode exitN; exitN.id = uuid(); exitN.type = "Macro.Exit";
    exitN.x = centroid.x() + 240; exitN.y = centroid.y();
    macro.nodes << entry << exitN;
    // 内部连线（两端都在选区）整体保留
    for (const BPConnection& c : connSnap)
        if (sel.contains(c.fromNode) && sel.contains(c.toNode)) macro.connections.append(c);
    // 边界引脚 → 宏接口 + 入口/出口接线
    for (const Boundary& b : boundaries) {
        const MacroPin mp{b.pinKey, b.label, b.isExec, b.kind};
        if (b.isOutputPin) {
            macro.outputPins.append(mp);
            macro.connections.append({uuid(), b.node, b.pin, exitN.id, b.pinKey});
        } else {
            macro.inputPins.append(mp);
            macro.connections.append({uuid(), entry.id, b.pinKey, b.node, b.pin});
        }
    }

    // 调用节点（本地折叠：子图存自身 params）
    BPNode call;
    call.id   = uuid();
    call.type = "Macro::local";
    call.x = centroid.x(); call.y = centroid.y();
    call.params["macroName"] = name;
    call.params["subgraph"]  =
        QString::fromUtf8(QJsonDocument(macro.toJson()).toJson(QJsonDocument::Compact));

    // 外部连线改接到调用节点（连到边界引脚的那些线）
    QList<BPConnection> newExternal;
    for (const Boundary& b : boundaries) {
        for (const BPConnection& c : connSnap) {
            if (b.isOutputPin) {
                if (c.fromNode == b.node && c.fromPin == b.pin && !sel.contains(c.toNode))
                    newExternal.append({uuid(), call.id, b.pinKey, c.toNode, c.toPin});
            } else {
                if (c.toNode == b.node && c.toPin == b.pin && !sel.contains(c.fromNode))
                    newExternal.append({uuid(), c.fromNode, c.fromPin, call.id, b.pinKey});
            }
        }
    }

    // 应用（整组 Undo）：删边界/内部连线 → 删选中节点 → 加调用节点 → 加外部新连线
    auto refresh = [this] { notifyModified(); update(); };
    m_bpUndoStack->beginMacro("折叠成节点");
    for (const BPConnection& c : connSnap)
        if (sel.contains(c.fromNode) || sel.contains(c.toNode))
            m_bpUndoStack->push(new BPConnectionRemoveCmd(m_bpClass, m_doc, c, refresh));
    for (const BPNode& n : selNodes)
        m_bpUndoStack->push(new BPNodeRemoveCmd(m_bpClass, m_doc, n, {}, refresh));
    m_bpUndoStack->push(new BPNodeAddCmd(m_bpClass, m_doc, call, refresh));
    for (const BPConnection& c : newExternal)
        m_bpUndoStack->push(new BPConnectionAddCmd(m_bpClass, m_doc, c, {}, false, refresh));
    m_bpUndoStack->endMacro();

    selectSingleNode(call.id);
    update();
}

// 解开折叠：把一个 Macro:: 调用节点展开回其内部节点，外部连线接回内部端点。
void BlueprintEditor::unfoldMacroNode(const QString& nodeId) {
    const BPNode* callPtr = findNode(nodeId);
    if (!callPtr || !callPtr->type.startsWith("Macro::") || (!m_doc && !m_bpClass)) return;
    const BPNode call = *callPtr;

    BPMacro macro; bool ok = false;
    if (call.type == "Macro::local") {
        const QString sub = call.params.value("subgraph");
        if (!sub.isEmpty()) {
            macro = BPMacro::fromJson(QJsonDocument::fromJson(sub.toUtf8()).object());
            ok = true;
        }
    } else if (const BPMacro* m = findMacro(call.type.mid(7))) {
        macro = *m; ok = true;
    }
    if (!ok) return;

    auto uuid = [] { return QUuid::createUuid().toString(QUuid::WithoutBraces); };

    QString entryId, exitId;
    for (const BPNode& n : macro.nodes) {
        if (n.type == "Macro.Entry") entryId = n.id;
        else if (n.type == "Macro.Exit") exitId = n.id;
    }
    QMap<QString, QPair<QString,QString>> inMap, outMap;
    for (const BPConnection& c : macro.connections) {
        if (c.fromNode == entryId) inMap[c.fromPin] = {c.toNode, c.toPin};
        if (c.toNode   == exitId)  outMap[c.toPin]  = {c.fromNode, c.fromPin};
    }

    QMap<QString,QString> idMap;
    for (const BPNode& n : macro.nodes)
        if (n.id != entryId && n.id != exitId) idMap[n.id] = uuid();
    auto mapId = [&](const QString& id){ return idMap.value(id, id); };

    QList<BPNode> newNodes;
    for (const BPNode& n : macro.nodes) {
        if (n.id == entryId || n.id == exitId) continue;
        BPNode nn = n; nn.id = idMap[n.id]; newNodes << nn;
    }
    QList<BPConnection> newConns;
    for (const BPConnection& c : macro.connections) {
        if (c.fromNode == entryId || c.toNode == exitId) continue;
        if (!idMap.contains(c.fromNode) || !idMap.contains(c.toNode)) continue;
        newConns << BPConnection{uuid(), mapId(c.fromNode), c.fromPin, mapId(c.toNode), c.toPin};
    }

    const QList<BPConnection> extConns = activeConns();
    QList<BPConnection> rewired;
    for (const BPConnection& c : extConns) {
        if (c.toNode == nodeId) {
            auto it = inMap.find(c.toPin);
            if (it != inMap.end())
                rewired << BPConnection{uuid(), c.fromNode, c.fromPin,
                                        mapId(it.value().first), it.value().second};
        }
        if (c.fromNode == nodeId) {
            auto it = outMap.find(c.fromPin);
            if (it != outMap.end())
                rewired << BPConnection{uuid(), mapId(it.value().first), it.value().second,
                                        c.toNode, c.toPin};
        }
    }

    auto refresh = [this] { notifyModified(); update(); };
    m_bpUndoStack->beginMacro("解开折叠");
    for (const BPConnection& c : extConns)
        if (c.fromNode == nodeId || c.toNode == nodeId)
            m_bpUndoStack->push(new BPConnectionRemoveCmd(m_bpClass, m_doc, c, refresh));
    m_bpUndoStack->push(new BPNodeRemoveCmd(m_bpClass, m_doc, call, {}, refresh));
    for (const BPNode& n : newNodes)
        m_bpUndoStack->push(new BPNodeAddCmd(m_bpClass, m_doc, n, refresh));
    for (const BPConnection& c : newConns)
        m_bpUndoStack->push(new BPConnectionAddCmd(m_bpClass, m_doc, c, {}, false, refresh));
    for (const BPConnection& c : rewired)
        m_bpUndoStack->push(new BPConnectionAddCmd(m_bpClass, m_doc, c, {}, false, refresh));
    m_bpUndoStack->endMacro();

    m_selectedNodeIds.clear();
    for (const BPNode& n : newNodes) m_selectedNodeIds.insert(n.id);
    m_selectedNodeId = newNodes.isEmpty() ? QString() : newNodes.first().id;
    m_selectedConnId.clear();
    update();
}

// 提升为宏库资产：本地折叠子图写成 .bpmacro，节点改为 Macro::<id> 库引用。
void BlueprintEditor::promoteMacroToLibrary(const QString& nodeId) {
    if (m_projectRoot.isEmpty()) {
        QMessageBox::warning(this, "提升为宏库资产", "当前没有工程路径，无法保存。");
        return;
    }
    const BPNode* callPtr = findNode(nodeId);
    if (!callPtr || callPtr->type != "Macro::local") return;
    const BPNode call = *callPtr;
    const QString sub = call.params.value("subgraph");
    if (sub.isEmpty()) return;

    BPMacro macro = BPMacro::fromJson(QJsonDocument::fromJson(sub.toUtf8()).object());
    if (macro.id.isEmpty()) macro.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    macro.name     = call.params.value("macroName", macro.name);
    macro.filePath = BPMacro::macrosDir(m_projectRoot) + "/" + macro.name + ".bpmacro";
    if (!macro.save()) {
        QMessageBox::warning(this, "提升为宏库资产", "保存宏库文件失败。");
        return;
    }
    m_macroCache.insert(macro.id, macro);

    BPNode before = call, after = call;
    after.type = "Macro::" + macro.id;
    after.params.remove("subgraph");
    m_bpUndoStack->push(new BPNodeModifyCmd(m_bpClass, m_doc, before, after, "提升为宏库资产",
                        [this]{ notifyModified(); update(); }));
    update();
}

// ── 进/出宏内部编辑 ───────────────────────────────────────────────────

void BlueprintEditor::enterMacroEdit(const QString& nodeId) {
    if (m_inMacroEdit) return;   // 暂不支持嵌套进入
    const BPNode* callPtr = findNode(nodeId);
    if (!callPtr || !callPtr->type.startsWith("Macro::")) return;

    BPMacro macro;
    if (callPtr->type == "Macro::local") {
        const QString sub = callPtr->params.value("subgraph");
        if (sub.isEmpty()) return;
        macro = BPMacro::fromJson(QJsonDocument::fromJson(sub.toUtf8()).object());
    } else if (const BPMacro* m = findMacro(callPtr->type.mid(7))) {
        macro = *m;
    } else return;

    // 记录返回上下文
    m_returnDoc       = m_doc;
    m_returnBpClass   = m_bpClass;
    m_macroCallNodeId = nodeId;
    m_macroEditInfo   = macro;

    // 子图装进临时 BPClass，复用 bpClass 编辑路径
    m_macroEditClass = BPClass{};
    m_macroEditClass.name        = macro.name;
    m_macroEditClass.nodes       = macro.nodes;
    m_macroEditClass.connections = macro.connections;

    loadBpClass(&m_macroEditClass);    // 会清状态/撤销栈
    m_inMacroEdit  = true;
    m_editingMacro = &m_macroEditInfo; // 入口/出口引脚来源
    frameAll();
    update();
}

void BlueprintEditor::exitMacroEdit() {
    if (!m_inMacroEdit) return;

    // 收回编辑结果
    BPMacro macro      = m_macroEditInfo;
    macro.nodes        = m_macroEditClass.nodes;
    macro.connections  = m_macroEditClass.connections;

    // 回写：本地折叠 → 更新调用节点 subgraph；库宏 → 存文件 + 刷新缓存
    if (macro.filePath.isEmpty()) {
        const QString subJson =
            QString::fromUtf8(QJsonDocument(macro.toJson()).toJson(QJsonDocument::Compact));
        auto writeBack = [&](const QString& id) {
            if (m_returnBpClass) {
                for (BPNode& x : m_returnBpClass->nodes)
                    if (x.id == id) { x.params["subgraph"] = subJson; return; }
            } else if (m_returnDoc) {
                for (const BPNode& n : m_returnDoc->bpNodes())
                    if (n.id == id) { BPNode nn = n; nn.params["subgraph"] = subJson;
                                      m_returnDoc->updateBPNode(nn); return; }
            }
        };
        writeBack(m_macroCallNodeId);
    } else {
        macro.save();
        m_macroCache.insert(macro.id, macro);
    }

    LevelDocument* rdoc = m_returnDoc;
    BPClass*       rbc  = m_returnBpClass;
    m_inMacroEdit     = false;
    m_editingMacro    = nullptr;
    m_returnDoc       = nullptr;
    m_returnBpClass   = nullptr;
    m_macroCallNodeId.clear();

    if (rbc)      loadBpClass(rbc);
    else          loadLevel(rdoc);
    notifyModified();
    update();
}

// ── 鼠标事件 ──────────────────────────────────────────────────────────

void BlueprintEditor::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) { QWidget::mouseDoubleClickEvent(e); return; }
    Hit hit = hitTest(e->position());
    if (hit.type == Hit::Node && findNode(hit.nodeId)
        && findNode(hit.nodeId)->type.startsWith("Macro::")) {
        enterMacroEdit(hit.nodeId);
        return;
    }
    QWidget::mouseDoubleClickEvent(e);
}

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
    // 宏编辑：点"返回"按钮
    if (m_inMacroEdit && e->button() == Qt::LeftButton
        && QRect(10, 10, 200, 30).contains(e->pos())) {
        exitMacroEdit();
        return;
    }
    // 右键/中键拖拽 = 平移画布（右键未拖动则在松开时弹菜单；左键空白拖拽=框选）
    if (e->button() == Qt::RightButton || e->button() == Qt::MiddleButton) {
        m_panning     = true;
        m_panIsRight  = (e->button() == Qt::RightButton);
        m_lastMouse   = e->pos();
        m_panStartPos = e->pos();
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (e->button() != Qt::LeftButton) return;

    Hit hit = hitTest(e->position());

    // 点中引脚/值区域：单选该节点（节点本体的选中在下面 Hit::Node 处理，含整组拖拽）
    if (hit.type == Hit::Pin || hit.type == Hit::PinValue) {
        selectSingleNode(hit.nodeId);
    }

    // 点击连接线
    if (hit.type == Hit::Wire) {
        m_selectedConnId = hit.connId;
        clearNodeSelection();
        update();
        return;
    }

    // 点击引脚值区域：按引脚 kind 选择对应内联编辑器（学虚幻，类型驱动）
    if (hit.type == Hit::PinValue) {
        const BPNode* node     = findNode(hit.nodeId);
        const QString cur      = node ? node->params.value(hit.pinName) : QString();
        const ValueKind kind   = node ? pinKindForNode(*node, hit.pinName)
                                      : (hit.pinName == "uiName" ? ValueKind::UIRef : ValueKind::Text);
        switch (kind) {
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
        case ValueKind::EnumRef: {
            showListPicker(e->pos(), hit.nodeId, hit.pinName, "选择枚举值",
                           enumValuesForPin(*node, hit.pinName), cur);
            break;
        }
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
        selectSingleNode(hit.nodeId); addSwitchBranch(hit.nodeId); return;
    }
    if (hit.type == Hit::SwitchDel) {
        selectSingleNode(hit.nodeId); removeSwitchBranch(hit.nodeId, hit.pinName); return;
    }
    if (hit.type == Hit::SwitchDefault) {
        selectSingleNode(hit.nodeId); toggleSwitchDefault(hit.nodeId); return;
    }
    if (hit.type == Hit::SwitchValue) {
        selectSingleNode(hit.nodeId);
        const BPNode* sn = findNode(hit.nodeId);
        const QList<QPair<QString, QString>> items =
            sn ? enumValuesForPin(*sn, hit.pinName) : QList<QPair<QString, QString>>{};
        if (!items.isEmpty()) {   // 枚举模式的比较值 → 下拉
            const QString cur = sn ? switchCaseValue(*sn, hit.pinName.mid(8)) : QString();
            showListPicker(e->pos(), hit.nodeId, hit.pinName, "选择枚举值", items, cur);
        } else {
            showInlineEdit(hit.nodeId, hit.pinName);  // "value" 或自由值 caseval
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
        const bool addMod = (e->modifiers()
            & (Qt::ShiftModifier | Qt::ControlModifier | Qt::MetaModifier));
        if (addMod) {
            // Shift/Cmd 点击：增减选中，不拖拽
            if (m_selectedNodeIds.contains(hit.nodeId)) {
                m_selectedNodeIds.remove(hit.nodeId);
                if (m_selectedNodeId == hit.nodeId)
                    m_selectedNodeId = m_selectedNodeIds.isEmpty()
                                       ? QString() : *m_selectedNodeIds.begin();
            } else {
                m_selectedNodeIds.insert(hit.nodeId);
                m_selectedNodeId = hit.nodeId;
            }
            m_selectedConnId.clear();
            update();
            return;
        }
        // 无修饰键：点的不在当前多选里 → 单选它；在多选里 → 保持，准备整组拖
        if (!m_selectedNodeIds.contains(hit.nodeId))
            selectSingleNode(hit.nodeId);
        else
            m_selectedNodeId = hit.nodeId;
        // 开始拖拽（可能整组），记录所有选中节点起始快照
        m_dragState      = DragState::DraggingNode;
        m_draggingNodeId = hit.nodeId;
        m_groupDragLast  = screenToCanvas(e->position());
        m_groupBefore.clear();
        for (const QString& id : m_selectedNodeIds)
            if (const BPNode* n = findNode(id)) m_groupBefore.append(*n);
        update();
        return;
    }

    // 空白处：开始框选（左键）。加修饰键则在原选中基础上增选。
    m_marquee         = true;
    m_marqueeStart    = e->pos();
    m_marqueeCur      = e->pos();
    m_marqueeAdditive = (e->modifiers()
        & (Qt::ShiftModifier | Qt::ControlModifier | Qt::MetaModifier));
}

void BlueprintEditor::mouseMoveEvent(QMouseEvent* e) {
    if (m_panning) {
        QPoint delta = e->pos() - m_lastMouse;
        m_offset   += delta;
        m_lastMouse = e->pos();
        update();
        return;
    }
    if (m_marquee) {
        m_marqueeCur = e->pos();
        update();
        return;
    }
    if (m_dragState == DragState::DraggingNode && (m_doc || m_bpClass)) {
        // 增量移动所有选中节点（保持相对位置），临时改坐标不标脏
        const QPointF cur   = screenToCanvas(e->position());
        const QPointF delta = cur - m_groupDragLast;
        m_groupDragLast = cur;
        auto moveById = [&](const QString& id) {
            if (m_bpClass) {
                for (BPNode& n : m_bpClass->nodes)
                    if (n.id == id) { n.x += (float)delta.x(); n.y += (float)delta.y(); return; }
            } else if (m_doc) {
                for (BPNode& n : const_cast<QList<BPNode>&>(m_doc->bpNodes()))
                    if (n.id == id) { n.x += (float)delta.x(); n.y += (float)delta.y(); return; }
            }
        };
        for (const QString& id : m_selectedNodeIds) moveById(id);
        update();
        return;
    }
    if (m_dragState == DragState::DraggingWire) {
        m_wireCursorPos = screenToCanvas(e->position());
        update();
    }
}

void BlueprintEditor::mouseReleaseEvent(QMouseEvent* e) {
    // 右键/中键平移结束
    if (m_panning && (e->button() == Qt::RightButton || e->button() == Qt::MiddleButton)) {
        m_panning = false;
        setCursor(Qt::ArrowCursor);
        // 右键未拖动 → 弹出上下文菜单
        if (m_panIsRight && (e->pos() - m_panStartPos).manhattanLength() < 4)
            showContextMenu(e->pos(), e->globalPosition().toPoint());
        m_panIsRight = false;
        return;
    }
    if (e->button() != Qt::LeftButton) return;

    // 框选结束
    if (m_marquee) {
        m_marquee = false;
        const QRect r = QRect(m_marqueeStart, m_marqueeCur).normalized();
        if (r.width() < 3 && r.height() < 3) {
            // 视为点击空白：清除选中（除非加修饰键）
            if (!m_marqueeAdditive) { clearNodeSelection(); m_selectedConnId.clear(); }
        } else {
            if (!m_marqueeAdditive) m_selectedNodeIds.clear();
            for (const BPNode& n : activeNodes()) {
                QPointF tl = canvasToScreen({n.x, n.y});
                QRectF  nr(tl.x(), tl.y(), kNodeW * m_zoom, nodeHeight(n) * m_zoom);
                if (r.intersects(nr.toRect())) m_selectedNodeIds.insert(n.id);
            }
            if (!m_selectedNodeIds.contains(m_selectedNodeId))
                m_selectedNodeId = m_selectedNodeIds.isEmpty()
                                   ? QString() : *m_selectedNodeIds.begin();
            m_selectedConnId.clear();
        }
        update();
        return;
    }

    if (m_dragState == DragState::DraggingNode && (m_doc || m_bpClass)) {
        // 收集实际发生移动的节点，整组走 Undo
        auto refresh = [this]() { notifyModified(); update(); };
        QList<QPair<BPNode, BPNode>> moved;
        for (const BPNode& before : m_groupBefore) {
            const BPNode* after = findNode(before.id);
            if (after && (after->x != before.x || after->y != before.y))
                moved.append({before, *after});
        }
        if (moved.size() == 1) {
            m_bpUndoStack->push(new BPNodeMoveCmd(m_bpClass, m_doc,
                                moved[0].first, moved[0].second, refresh));
        } else if (moved.size() > 1) {
            m_bpUndoStack->beginMacro("移动节点");
            for (const auto& pr : moved)
                m_bpUndoStack->push(new BPNodeMoveCmd(m_bpClass, m_doc,
                                    pr.first, pr.second, refresh));
            m_bpUndoStack->endMacro();
        }
        m_dragState = DragState::None;
        m_draggingNodeId.clear();
        m_groupBefore.clear();
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
                if (fromExec == toExec) {
                    // exec-exec 总允许；data-data 再按值类型兼容判定
                    typeOk = fromExec
                        || dataKindsCompatible(pinKindOf(*fromNodePtr, fromPin, true),
                                               pinKindOf(*toNodePtr,   toPin,   false));
                }
            }

            if (typeOk) {
                // 查找需被挤掉的旧连接（displaced）。
                // 规则：exec 既可扇出（一个输出连多个）也可扇入（多个连一个输入），
                //       只挡完全重复的同一根线；数据输入仍单连（按输入端去重）。
                const bool isExecConn = isPinExec(fromNodePtr->type, fromPin, true);
                BPConnection displaced;
                bool hasDisplaced = false;
                for (const BPConnection& c : activeConns()) {
                    const bool conflict = isExecConn
                        ? (c.fromNode == fromNode && c.fromPin == fromPin
                           && c.toNode == toNode && c.toPin == toPin)        // exec：仅挡重复线
                        : (c.toNode   == toNode   && c.toPin   == toPin);    // data：输入单连
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
    selectSingleNode(copy.id);
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
    if (m_selectedNodeIds.isEmpty()) return;
    // 批量删除所有选中节点（连带各自的连线），整组走 Undo
    const QStringList ids = QStringList(m_selectedNodeIds.begin(), m_selectedNodeIds.end());
    QList<QPair<BPNode, QList<BPConnection>>> toRemove;
    for (const QString& id : ids) {
        const BPNode* n = findNode(id);
        if (!n) continue;
        QList<BPConnection> related;
        for (const BPConnection& c : activeConns())
            if (c.fromNode == id || c.toNode == id) related << c;
        toRemove.append({*n, related});
    }
    if (toRemove.isEmpty()) return;
    if (toRemove.size() == 1) {
        m_bpUndoStack->push(new BPNodeRemoveCmd(m_bpClass, m_doc,
                            toRemove[0].first, toRemove[0].second, refresh));
    } else {
        m_bpUndoStack->beginMacro("删除节点");
        for (const auto& pr : toRemove)
            m_bpUndoStack->push(new BPNodeRemoveCmd(m_bpClass, m_doc,
                                pr.first, pr.second, refresh));
        m_bpUndoStack->endMacro();
    }
    clearNodeSelection();
}

void BlueprintEditor::keyPressEvent(QKeyEvent* e) {
    QWidget::keyPressEvent(e);
}

// ── 右键菜单 ──────────────────────────────────────────────────────────

void BlueprintEditor::showContextMenu(const QPoint& pos, const QPoint& globalPos) {
    if (!m_doc && !m_bpClass) return;
    Hit hit = hitTest(QPointF(pos));

    // 右键点在节点上：对象操作菜单
    if (hit.type == Hit::Node || hit.type == Hit::Pin || hit.type == Hit::PinValue) {
        const QString nodeId = hit.nodeId;
        const BPNode* n = findNode(nodeId);
        if (!n) return;
        const NodeDef* def = findNodeDef(n->type);
        QString label = def ? def->displayName : n->type;
        QMenu menu(this);
        if (n->type == "Flow.Switch") {
            auto* em = menu.addMenu("按枚举");
            for (const EnumDef& ed : m_enumDefs) {
                const QString en = ed.name;
                em->addAction(en, [this, nodeId, en]() { bindSwitchEnum(nodeId, en); });
            }
            if (!n->params.value("enum").isEmpty())
                em->addAction("（取消枚举·自由值）", [this, nodeId]() { bindSwitchEnum(nodeId, QString()); });
            menu.addSeparator();
        }
        if (n->type.startsWith("Macro::")) {
            menu.addAction("解开折叠", [this, nodeId]() { unfoldMacroNode(nodeId); });
            if (n->type == "Macro::local")
                menu.addAction("提升为宏库资产", [this, nodeId]() { promoteMacroToLibrary(nodeId); });
            menu.addSeparator();
        }
        if (m_selectedNodeIds.contains(nodeId)) {
            menu.addAction(QString("折叠成节点（%1）").arg(m_selectedNodeIds.size()),
                           [this]() { foldSelectionToMacro(); });
            menu.addSeparator();
        }
        menu.addAction("删除节点 "" + label + """, [this, nodeId, n]() {
            QList<BPConnection> related;
            for (const BPConnection& c : activeConns())
                if (c.fromNode == nodeId || c.toNode == nodeId)
                    related << c;
            auto refresh = [this]() { notifyModified(); update(); };
            m_bpUndoStack->push(new BPNodeRemoveCmd(m_bpClass, m_doc, *n, related, refresh));
            m_selectedNodeIds.remove(nodeId);
            if (m_selectedNodeId == nodeId) m_selectedNodeId.clear();
        });
        menu.exec(globalPos);
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
        menu.exec(globalPos);
        return;
    }

    // 空白处：弹出节点创建菜单
    QPointF canvasPos = screenToCanvas(pos);

    QMenu menu(this);
    // 有多选时，顶部提供"折叠成节点"
    if (m_selectedNodeIds.size() >= 2) {
        menu.addAction(QString("折叠成节点（%1）").arg(m_selectedNodeIds.size()),
                       [this]() { foldSelectionToMacro(); });
        menu.addSeparator();
    }
    // 全局变量：每个声明的变量提供 获取/设置
    if (!m_globalVarDefs.isEmpty()) {
        auto* gvMenu = menu.addMenu("全局变量");
        for (const GlobalVarDef& gv : m_globalVarDefs) {
            const QString vn = gv.name;
            auto makeGlobalNode = [this, vn, canvasPos](const QString& type) {
                if (!m_doc && !m_bpClass) return;
                BPNode node;
                node.id   = QUuid::createUuid().toString(QUuid::WithoutBraces);
                node.type = type;
                node.x = (float)canvasPos.x(); node.y = (float)canvasPos.y();
                node.params["varName"] = vn;
                auto refresh = [this]() { notifyModified(); update(); };
                m_bpUndoStack->push(new BPNodeAddCmd(m_bpClass, m_doc, node, refresh));
                selectSingleNode(node.id);
                update();
            };
            if (gv.type.startsWith("array:")) {
                auto* arrSub = gvMenu->addMenu(vn + "（数组）");
                arrSub->addAction("获取 " + vn,   [makeGlobalNode]() { makeGlobalNode("Global.Get"); });
                arrSub->addAction("设置 " + vn,   [makeGlobalNode]() { makeGlobalNode("Global.Set"); });
                arrSub->addSeparator();
                arrSub->addAction("添加元素",     [makeGlobalNode]() { makeGlobalNode("Array.Add"); });
                arrSub->addAction("按索引移除",   [makeGlobalNode]() { makeGlobalNode("Array.RemoveAt"); });
                arrSub->addAction("按值移除",     [makeGlobalNode]() { makeGlobalNode("Array.RemoveValue"); });
                arrSub->addAction("设置元素",     [makeGlobalNode]() { makeGlobalNode("Array.SetAt"); });
                arrSub->addAction("清空数组",     [makeGlobalNode]() { makeGlobalNode("Array.Clear"); });
            } else {
                gvMenu->addAction("获取 " + vn, [makeGlobalNode]() { makeGlobalNode("Global.Get"); });
                gvMenu->addAction("设置 " + vn, [makeGlobalNode]() { makeGlobalNode("Global.Set"); });
            }
        }
    }
    // 工程里的自定义节点（宏库资产）
    const QList<BPMacro> macros = m_projectRoot.isEmpty()
        ? QList<BPMacro>{} : BPMacro::listAll(m_projectRoot);
    if (!macros.isEmpty()) {
        auto* customMenu = menu.addMenu("自定义节点");
        for (const BPMacro& mac : macros) {
            const QString id = mac.id, nm = mac.name;
            customMenu->addAction(nm, [this, id, nm, canvasPos]() {
                if (!m_doc && !m_bpClass) return;
                BPNode node;
                node.id   = QUuid::createUuid().toString(QUuid::WithoutBraces);
                node.type = "Macro::" + id;
                node.x = (float)canvasPos.x(); node.y = (float)canvasPos.y();
                node.params["macroName"] = nm;
                auto refresh = [this]() { notifyModified(); update(); };
                m_bpUndoStack->push(new BPNodeAddCmd(m_bpClass, m_doc, node, refresh));
                selectSingleNode(node.id);
                update();
            });
        }
    }
    auto* eventMenu  = menu.addMenu("事件");
    auto* keyMenu    = eventMenu->addMenu("键盘事件");
    auto* actionMenu = menu.addMenu("动作");
    auto* flowMenu   = menu.addMenu("流程控制");
    auto* varMenu    = menu.addMenu("变量");
    auto* arrayMenu  = menu.addMenu("数组");
    auto* mathMenu   = menu.addMenu("数学");
    auto* logicMenu  = menu.addMenu("逻辑");
    auto* selfMenu   = menu.addMenu("Self");
    auto* uiMenu     = menu.addMenu("UI");

    for (const NodeDef& def : nodeDefs()) {
        if (!isSelfNodeVisible(def.typeId)) continue;
        if (isDeprecatedNodeType(def.typeId)) continue;
        if (isVarBoundArrayOp(def.typeId)) continue;   // varName 由全局变量菜单烤入
        QMenu* target = def.typeId.startsWith("Event.Key.")  ? keyMenu    :
                        def.typeId.startsWith("Event.")      ? eventMenu  :
                        def.typeId.startsWith("Action.")     ? actionMenu :
                        def.typeId.startsWith("Flow.")       ? flowMenu   :
                        def.typeId.startsWith("UI.")         ? uiMenu     :
                        def.typeId.startsWith("Self.")       ? selfMenu   :
                        def.typeId.startsWith("Math.")       ? mathMenu   :
                        def.typeId.startsWith("Logic.")      ? logicMenu  :
                        def.typeId.startsWith("Cmp.")        ? logicMenu  :
                        def.typeId.startsWith("Array.")      ? arrayMenu  : varMenu;
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
            selectSingleNode(node.id);
            update();
        });
    }
    menu.exec(globalPos);
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
        if (isDeprecatedNodeType(def.typeId)) continue;
        if (isVarBoundArrayOp(def.typeId)) continue;   // varName 由全局变量菜单烤入

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
                          def.typeId.startsWith("Logic.")  ? "逻辑"    :
                          def.typeId.startsWith("Cmp.")    ? "逻辑"    :
                          def.typeId.startsWith("Array.")  ? "数组"    : "变量";
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

    selectSingleNode(node.id);

    hideWireDropPopup();
    update();
}

// ── 引脚值内联编辑 ────────────────────────────────────────────────────

void BlueprintEditor::showInlineEdit(const QString& nodeId, const QString& pinKey) {
    commitInlineEdit();
    if (!m_doc && !m_bpClass) return;

    const BPNode* node = findNode(nodeId);
    if (!node) return;

    // 行 Y 用 pinCenter（兼容分支控制的自定义行布局）
    const QPointF pc = pinCenter(*node, pinKey, false);
    if (pc.x() == 0.0 && pc.y() == 0.0) return;
    QPointF tl  = canvasToScreen({node->x, node->y});
    float   nw  = kNodeW * (float)m_zoom;
    float   rowH = kRowH * (float)m_zoom;
    float   rowY = (float)pc.y() - rowH * 0.5f;
    float   x0, x1;
    if (node->type == "Flow.Switch") {
        x0 = (float)(tl.x() + nw * 0.34f);
        x1 = (float)(tl.x() + nw - 30.0f * (float)m_zoom);
    } else {
        x0 = (float)(tl.x() + nw * 0.5f);
        x1 = (float)(tl.x() + nw - 6.0);
    }

    m_inlineEditNodeId = nodeId;
    m_inlineEditPinKey = pinKey;
    m_inlineSwitchBranchId.clear();

    QString initText;
    if (node->type == "Flow.Switch" && pinKey.startsWith("caseval_"))
        initText = switchCaseValue(*node, pinKey.mid(8));   // 兼容旧数据
    else
        initText = node->params.value(pinKey);

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
        BPNode before = *cur, after = *cur;
        after.params[m_inlineEditPinKey] = val;
        if (after.type == "Flow.Switch") {
            // 分支控制的值/比较值：走 Undo
            m_bpUndoStack->push(new BPNodeModifyCmd(m_bpClass, m_doc, before, after, "修改分支值",
                                [this]{ notifyModified(); update(); }));
        } else {
            updateNodeInActive(m_bpClass, m_doc, after);
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

void BlueprintEditor::bindSwitchEnum(const QString& nodeId, const QString& enumName) {
    const BPNode* cur = findNode(nodeId);
    if (!cur) return;
    BPNode before = *cur, after = *cur;
    if (enumName.isEmpty()) {
        after.params.remove("enum");   // 解绑 → 比较值回到打字模式
    } else {
        after.params["enum"] = enumName;
        // 一键生成：为尚未覆盖的枚举值各加一个分支（已有分支保留）
        auto branches = parseSwitchBranches(after.params.value("branches"));
        QStringList covered;
        for (const SwitchBranch& b : branches) covered << switchCaseValue(after, b.id);
        for (const QString& v : Enums::valuesOf(m_enumDefs, enumName)) {
            if (covered.contains(v)) continue;
            const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            branches.append({id, QString()});
            after.params["caseval_" + id] = v;
        }
        after.params["branches"] = serializeSwitchBranches(branches);
    }
    m_bpUndoStack->push(new BPNodeModifyCmd(m_bpClass, m_doc, before, after,
                        enumName.isEmpty() ? "取消枚举" : "按枚举生成分支",
                        [this]{ notifyModified(); update(); }));
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
    clearNodeSelection();
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
    m_macroCache.clear();   // 切工程时丢弃旧宏缓存
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
