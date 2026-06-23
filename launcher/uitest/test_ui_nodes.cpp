// 无界面测试：直接驱动 BPRuntime + UIRuntime，逐个验证 UI 蓝图节点的真实效果
#include "models/LevelDocument.h"
#include "models/UIDocument.h"
#include "editor/BPRuntime.h"
#include "editor/UIRuntime.h"

#include <QGuiApplication>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>
#include <cstdio>

static int g_pass = 0, g_fail = 0;
static void check(const QString& label, bool ok) {
    if (ok) { g_pass++; printf("  \xE2\x9C\x85 %s\n", label.toUtf8().constData()); }
    else    { g_fail++; printf("  \xE2\x9D\x8C %s\n", label.toUtf8().constData()); }
}

// ── 在实例中按 name 找控件 ───────────────────────────────────────────────
static const UIWidget* findW(UIRuntime& ui, const QString& uiName, const QString& wname) {
    for (const UIInstance* inst : ui.shownInstances()) {
        if (inst->uiName != uiName) continue;
        for (const UIWidget& w : inst->docCopy.widgets())
            if (w.name == wname) return new UIWidget(w); // 拷贝返回，调用方负责释放（测试用，简化）
    }
    return nullptr;
}
static QString instId(UIRuntime& ui, const QString& uiName) {
    for (const UIInstance* inst : ui.shownInstances())
        if (inst->uiName == uiName) return inst->instanceId;
    return {};
}

static BPNode mk(const QString& id, const QString& type,
                 QMap<QString,QString> params = {}) {
    BPNode n; n.id = id; n.type = type; n.params = params; return n;
}
static BPConnection conn(const QString& from, const QString& fp,
                         const QString& to, const QString& tp) {
    BPConnection c; c.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    c.fromNode = from; c.fromPin = fp; c.toNode = to; c.toPin = tp; return c;
}

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    // 1) 建临时工程目录
    QString proj = QDir::tempPath() + "/uinode_test_" +
                   QUuid::createUuid().toString(QUuid::WithoutBraces);
    QDir().mkpath(proj + "/UI");
    QDir().mkpath(proj + "/Levels");

    // 2) 写一个 UI 文件「血条」，含：root(面板) > label(文本)/bar(进度条)/btn(按钮)/dd(下拉菜单)
    auto makeW = [](const QString& id, const QString& name, const QString& type,
                    const QString& parent) {
        UIWidget w; w.id = id; w.name = name; w.type = type; w.parentId = parent;
        w.visible = true; return w;
    };
    QList<UIWidget> ws;
    ws << makeW("wr","root","UI.面板","");
    UIWidget label = makeW("wl","label","UI.文本","wr"); label.text = "初始";
    UIWidget bar   = makeW("wb","bar","UI.进度条","wr"); bar.value = 1.0f;
    UIWidget btn   = makeW("wt","btn","UI.按钮","wr");   btn.text = "确定";
    UIWidget dd    = makeW("wd","dd","UI.下拉菜单","wr"); dd.text = "A\nB\nC";
    ws << label << bar << btn << dd;
    {
        QJsonArray arr; for (const UIWidget& w : ws) arr << w.toJson();
        QJsonObject root; root["name"]="血条"; root["version"]="0.1"; root["widgets"]=arr;
        QFile f(proj + "/UI/血条.ui"); f.open(QIODevice::WriteOnly);
        f.write(QJsonDocument(root).toJson()); f.close();
    }

    // 3) 构建关卡蓝图
    QList<BPNode> nodes;
    QList<BPConnection> conns;

    // 链 A：开始运行 → 创建UI → 显示UI(整体) → 设置文本 → 设置进度值 → 设置控件可见(隐藏label)
    nodes << mk("begin","Event.BeginPlay");
    nodes << mk("create","UI.Create",   {{"widgetRef","血条"}});
    nodes << mk("show","UI.Show",        {{"widgetRef","血条"}});
    nodes << mk("settext","UI.SetText",  {{"widgetRef","血条::label"},{"text","HP:100"}});
    nodes << mk("setval","UI.SetValue",  {{"widgetRef","血条::bar"},{"value","0.5"}});
    nodes << mk("setvis","UI.SetVisible",{{"widgetRef","血条::label"},{"visible","false"}});
    conns << conn("begin","exec_out","create","exec_in");
    conns << conn("create","exec_out","show","exec_in");
    conns << conn("show","exec_out","settext","exec_in");
    conns << conn("settext","exec_out","setval","exec_in");
    conns << conn("setval","exec_out","setvis","exec_in");

    // 按钮点击 → 设置文本(label="被点击")
    nodes << mk("onclick","UI.OnButtonClick",{{"widgetRef","血条::btn"}});
    nodes << mk("clicktext","UI.SetText",    {{"widgetRef","血条::label"},{"text","被点击"}});
    conns << conn("onclick","exec_out","clicktext","exec_in");

    // 下拉改变 → 设置文本(label=index)，index 引脚连到 text 引脚
    nodes << mk("ondd","UI.OnDropdownChanged",{{"widgetRef","血条::dd"}});
    nodes << mk("ddtext","UI.SetText",        {{"widgetRef","血条::label"}});
    conns << conn("ondd","exec_out","ddtext","exec_in");
    conns << conn("ondd","index","ddtext","text");

    // 链 C：开始运行 → 设置UI位置（整体移到 200,150）
    nodes << mk("setpos","UI.SetPosition",{{"widgetRef","血条"},{"x","200"},{"y","150"}});
    conns << conn("setvis","exec_out","setpos","exec_in");

    // 链 D：用「UI引用」节点喂 widgetRef，再「设置文本」（验证 UI.Ref 输出引脚）
    // UI.Ref 输出引脚 = "uiName::pinKey"，pinKey 即控件名 → 连到 SetText.widgetRef
    {
        BPNode ref = mk("uiref","UI.Ref",{{"uiName","血条"}});
        nodes << ref;
    }
    nodes << mk("reftext","UI.SetText",{{"text","来自引用"}});
    conns << conn("setpos","exec_out","reftext","exec_in");
    conns << conn("uiref","btn","reftext","widgetRef"); // UI.Ref 的 btn 引脚 → widgetRef

    // 链 E（按键触发）：设置数值变量 hp=0.25 → 获取数值变量 → 连线喂给「设置进度值」
    // 这是血条最真实的用法：数值由连线驱动，而非写死参数
    nodes << mk("key","Event.Key.W");
    nodes << mk("setnum","Var.SetNumber",{{"name","hp"},{"value","0.25"}});
    nodes << mk("getnum","Var.GetNumber",{{"name","hp"}});
    nodes << mk("setvalconn","UI.SetValue",{{"widgetRef","血条::bar"}}); // value 不写死，靠连线
    conns << conn("key","pressed","setnum","exec_in");
    conns << conn("setnum","exec_out","setvalconn","exec_in");
    conns << conn("getnum","value","setvalconn","value");

    // 链 F（复刻用户确切场景）：按键节点 → 直接连「设置进度值」，内联填值 0.7
    nodes << mk("keyA","Event.Key.A");
    nodes << mk("setvaldirect","UI.SetValue",{{"widgetRef","血条::bar"},{"value","0.7"}});
    conns << conn("keyA","pressed","setvaldirect","exec_in");

    // 写关卡文件并真实 load
    {
        QJsonArray nArr, cArr;
        for (const BPNode& n : nodes) nArr << n.toJson();
        for (const BPConnection& c : conns) cArr << c.toJson();
        QJsonObject bp; bp["nodes"]=nArr; bp["connections"]=cArr;
        QJsonObject root; root["name"]="关卡1"; root["version"]="0.1";
        root["objects"]=QJsonArray(); root["blueprint"]=bp;
        QFile f(proj + "/Levels/关卡1.level"); f.open(QIODevice::WriteOnly);
        f.write(QJsonDocument(root).toJson()); f.close();
    }

    LevelDocument doc;
    if (!doc.load(proj + "/Levels/关卡1.level")) { printf("关卡加载失败\n"); return 2; }

    UIRuntime ui(proj);
    BPRuntime rt(&doc);
    rt.setUIRuntime(&ui);

    // ── 触发开始运行链 ───────────────────────────────────────────────────
    printf("\n[链 A] 开始运行 → 创建/显示/设置文本/设置进度值/设置可见\n");
    rt.triggerBeginPlay();

    check("创建UI：实例已存在", instId(ui,"血条").size() > 0);
    {
        const UIWidget* root = findW(ui,"血条","root");
        check("显示UI：root 可见", root && root->visible);
        delete root;
        const UIWidget* l = findW(ui,"血条","label");
        check("设置文本：label.text == 'HP:100'", l && l->text == "HP:100");
        check("设置控件可见：label 已被隐藏", l && !l->visible);
        delete l;
        const UIWidget* b = findW(ui,"血条","bar");
        check("设置进度值：bar.value == 0.5", b && qAbs(b->value - 0.5f) < 1e-4);
        delete b;
        // UI引用：UI.Ref 的 btn 引脚喂给设置文本 → btn.text == '来自引用'
        const UIWidget* bt = findW(ui,"血条","btn");
        check("UI引用：UI.Ref→设置文本生效，btn.text == '来自引用'", bt && bt->text == "来自引用");
        delete bt;
    }
    {
        // 设置UI位置：实例 screenX/Y == 200/150
        bool ok = false;
        for (const UIInstance* inst : ui.shownInstances())
            if (inst->uiName == "血条")
                ok = (qAbs(inst->screenX - 200.f) < 1e-4 && qAbs(inst->screenY - 150.f) < 1e-4);
        check("设置UI位置：实例坐标 == (200,150)", ok);
    }

    // ── 按钮点击 ─────────────────────────────────────────────────────────
    printf("\n[事件] 按钮点击时\n");
    ui.notifyButtonClicked(instId(ui,"血条"), "btn"); // 模拟运行时点击信号
    {
        const UIWidget* l = findW(ui,"血条","label");
        check("按钮点击：label.text == '被点击'", l && l->text == "被点击");
        delete l;
    }

    // ── 下拉改变（验证 index 引脚传递）──────────────────────────────────
    printf("\n[事件] 下拉选项改变时（index 引脚 → 设置文本）\n");
    ui.notifyDropdownChanged(instId(ui,"血条"), "dd", 2);
    {
        const UIWidget* l = findW(ui,"血条","label");
        check("下拉改变：label.text == '2'（index 引脚生效）", l && l->text == "2");
        delete l;
    }

    // ── 连线驱动的设置进度值（血条真实用法）────────────────────────────
    printf("\n[链 E] 按键 → 设置数值变量 → 获取 → 连线喂给设置进度值\n");
    rt.triggerKeyDown("W");
    {
        const UIWidget* b = findW(ui,"血条","bar");
        printf("    [debug] bar.value 实际 = %f (期望 0.25; 0.5=链E没跑, 0=连线值为空)\n",
               b ? b->value : -1.0f);
        check("设置进度值(连线驱动)：bar.value == 0.25", b && qAbs(b->value - 0.25f) < 1e-4);
        delete b;
    }

    // ── 复刻用户场景：按键直连设置进度值（内联值）──────────────────────
    printf("\n[链 F] 复刻用户场景：按键(A) 直连 设置进度值(内联0.7)\n");
    rt.triggerKeyDown("A");
    {
        const UIWidget* b = findW(ui,"血条","bar");
        printf("    [debug] bar.value 实际 = %f (期望 0.7)\n", b ? b->value : -1.0f);
        check("按键直连设进度值：bar.value == 0.7", b && qAbs(b->value - 0.7f) < 1e-4);
        delete b;
    }

    // ── 隐藏UI / 销毁UI ──────────────────────────────────────────────────
    printf("\n[链 B] 隐藏单控件 / 销毁整个UI\n");
    ui.hideWidgetByName("血条","bar");
    {
        const UIWidget* b = findW(ui,"血条","bar");
        check("隐藏UI：bar 不可见", b && !b->visible);
        delete b;
    }
    ui.destroyByName("血条");
    check("销毁UI：实例列表已清空", instId(ui,"血条").isEmpty());

    // ── 关键验证：没有「创建UI/显示UI」时，设置进度值是否静默失效 ──────
    printf("\n[关键] 不创建/显示UI，直接设置进度值 → 应静默失效（找不到实例）\n");
    {
        UIRuntime ui2(proj);                       // 全新运行时，无任何实例
        ui2.setValueByName("血条", "bar", 0.7f);   // 直接设值
        check("未创建UI时：实例列表为空(无东西可显示)", ui2.shownInstances().isEmpty());
        // 即使先创建但不显示，控件默认隐藏
        ui2.createInstance("血条");
        const UIWidget* b = nullptr;
        for (const UIInstance* inst : ui2.shownInstances())
            if (inst->uiName=="血条")
                for (const UIWidget& w : inst->docCopy.widgets())
                    if (w.name=="bar") b = new UIWidget(w);
        check("仅创建未显示UI：bar 默认隐藏(visible==false)", b && !b->visible);
        delete b;
    }

    // ── 复刻用户真实 bug：数值填 10，被钳制到 1.0（满），与默认满格无差异 ──
    printf("\n[复刻] 设置进度值=10 → 钳制到 1.0（默认也是 1.0，故看不出变化）\n");
    {
        UIRuntime ui3(proj);
        ui3.createInstance("血条");
        ui3.showWidgetByName("血条", "bar");
        // 默认值确认
        auto barVal = [&]() -> float {
            for (const UIInstance* inst : ui3.shownInstances())
                if (inst->uiName=="血条")
                    for (const UIWidget& w : inst->docCopy.widgets())
                        if (w.name=="bar") return w.value;
            return -1;
        };
        check("进度条默认值 == 1.0（满）", qAbs(barVal() - 1.0f) < 1e-4);
        ui3.setValueByName("血条","bar", 10.0f);   // 用户填的 10
        check("设置进度值=10 被钳制为 1.0（与默认相同→无变化）", qAbs(barVal() - 1.0f) < 1e-4);
        ui3.setValueByName("血条","bar", 0.5f);    // 正确用法
        check("设置进度值=0.5 → 实际变 0.5（半格，肉眼可见）", qAbs(barVal() - 0.5f) < 1e-4);
    }

    printf("\n========== 结果：通过 %d / 失败 %d ==========\n", g_pass, g_fail);
    QDir(proj).removeRecursively();
    return g_fail == 0 ? 0 : 1;
}
