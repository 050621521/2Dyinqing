// 用用户真实工程文件测试：加载磁盘上的关卡，触发开始运行+按W，看进度条
#include "models/LevelDocument.h"
#include "editor/BPRuntime.h"
#include "editor/UIRuntime.h"
#include <QGuiApplication>
#include <cstdio>

static float barValue(UIRuntime& ui) {
    for (const UIInstance* inst : ui.shownInstances())
        if (inst->uiName == "标准关卡界面")
            for (const UIWidget& w : inst->docCopy.widgets())
                if (w.name == "进度条") return w.value;
    return -999;
}

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    const QString proj = "/Users/kwy/Documents/New project/MyGame";
    const QString levelPath = proj + "/Levels/标准关卡界面.level";

    LevelDocument doc;
    if (!doc.load(levelPath)) { printf("关卡加载失败\n"); return 2; }

    printf("=== 保存在磁盘的蓝图节点 ===\n");
    for (const BPNode& n : doc.bpNodes())
        printf("  %s\n", n.type.toUtf8().constData());

    UIRuntime ui(proj);
    BPRuntime rt(&doc);
    rt.setUIRuntime(&ui);

    rt.triggerBeginPlay();
    printf("\n开始运行后：进度条出现? %s, 值=%.3f\n",
           barValue(ui) > -900 ? "是" : "否(UI未显示)", barValue(ui));

    rt.triggerKeyDown("W");
    printf("按 W 后：进度条值=%.3f （若蓝图无W节点则不会变）\n", barValue(ui));

    bool hasW = false, hasSet = false;
    for (const BPNode& n : doc.bpNodes()) {
        if (n.type.startsWith("Event.Key.")) hasW = true;
        if (n.type == "UI.SetValue")          hasSet = true;
    }
    printf("\n磁盘蓝图是否含【按键W节点】: %s\n", hasW ? "有" : "❌ 没有");
    printf("磁盘蓝图是否含【设置进度值节点】: %s\n", hasSet ? "有" : "❌ 没有");
    return 0;
}
