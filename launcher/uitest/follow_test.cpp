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
#include <cstdio>

static const char* kProj = "/Users/kwy/Documents/2Dyinqing/MyGame";
static int g_pass = 0, g_fail = 0;
static void check(const char* label, bool ok) {
    if (ok) { g_pass++; printf("  OK  %s\n", label); }
    else    { g_fail++; printf("  XX  %s\n", label); }
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

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    test_runtime_setfollow();
    printf("\n通过 %d / 失败 %d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
