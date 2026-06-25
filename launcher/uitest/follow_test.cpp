// 无界面测试：UIRuntime 跟随接口 + UI.Follow 蓝图节点
#include "models/LevelDocument.h"
#include "models/UIDocument.h"
#include "editor/BPRuntime.h"
#include "editor/UIRuntime.h"
#include <QGuiApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <cstdio>

static const char* kProj = "/Users/kwy/Documents/2Dyinqing/MyGame";
static int g_pass = 0, g_fail = 0;
static void check(const char* label, bool ok) {
    if (ok) { g_pass++; printf("  OK  %s\n", label); }
    else    { g_fail++; printf("  XX  %s\n", label); }
}

static BPNode mk(const QString& id, const QString& type, QMap<QString,QString> params = {}) {
    BPNode n; n.id = id; n.type = type; n.params = params; return n;
}
static BPConnection cn(const QString& from, const QString& fp, const QString& to, const QString& tp) {
    BPConnection c; c.id = from + "->" + to; c.fromNode = from; c.fromPin = fp;
    c.toNode = to; c.toPin = tp; return c;
}

static void test_runtime_setfollow() {
    printf("[setFollowActor / Ref / clear]\n");
    UIRuntime ui{QString(kProj)};
    const QString inst = ui.createInstance("头顶信息");   // 需 MyGame/UI/头顶信息.ui 存在
    check("createInstance 头顶信息成功", !inst.isEmpty());

    ui.setFollowActor(inst, "actor-123", 0.0f, -150.0f);
    const UIInstance* p = nullptr;
    for (const UIInstance* i : ui.shownInstances()) if (i->instanceId == inst) p = i;
    check("按实例id设置followActorId", p && p->followActorId == "actor-123");
    check("offsetY 记录正确",          p && p->followOffsetY == -150.0f);

    ui.setFollowActorRef(inst, "actor-999", 5.0f, -10.0f); // 用实例 id 当 ref
    check("setFollowActorRef(实例id) 生效", p && p->followActorId == "actor-999");

    ui.clearFollow(inst);
    check("clearFollow 清空", p && p->followActorId.isEmpty());
}

static void test_follow_node() {
    printf("[UI.Follow 蓝图节点]\n");
    QList<BPNode> nodes = {
        mk("begin",  "Event.BeginPlay"),
        mk("create", "UI.Create", {{"widgetRef", "头顶信息"}}),
        mk("follow", "UI.Follow",  {{"actorId", "unit-A"}, {"offsetUp", "150"}}),
    };
    QList<BPConnection> conns = {
        cn("begin",  "exec_out",  "create", "exec_in"),
        cn("create", "exec_out",  "follow", "exec_in"),
        cn("create", "widgetRef", "follow", "widgetRef"),
    };
    QJsonArray nArr, cArr;
    for (const BPNode& n : nodes) nArr << n.toJson();
    for (const BPConnection& c : conns) cArr << c.toJson();
    QJsonObject bp; bp["nodes"] = nArr; bp["connections"] = cArr;
    QJsonObject root; root["name"] = "__follow_test__"; root["version"] = "0.1";
    root["objects"] = QJsonArray(); root["blueprint"] = bp;
    const QString lvlPath = QDir::tempPath() + "/__follow_test__.level";
    { QFile f(lvlPath); f.open(QIODevice::WriteOnly);
      f.write(QJsonDocument(root).toJson()); f.close(); }

    LevelDocument doc;
    check("临时关卡加载成功", doc.load(lvlPath));

    UIRuntime ui{QString(kProj)};
    BPRuntime rt(&doc);
    rt.setUIRuntime(&ui);
    rt.triggerBeginPlay();

    const UIInstance* p = nullptr;
    for (const UIInstance* i : ui.shownInstances())
        if (i->uiName == "头顶信息") p = i;
    check("UI.Create 建出头顶信息实例", p != nullptr);
    check("UI.Follow 绑定到 unit-A",    p && p->followActorId == "unit-A");
    check("上移距离转为负offsetY",       p && p->followOffsetY == -150.0f);

    QFile::remove(lvlPath);
}

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    test_runtime_setfollow();
    test_follow_node();
    printf("\n通过 %d / 失败 %d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
