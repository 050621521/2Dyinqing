// 真实渲染测试：用 GameViewport 渲染进度条，数填充色像素，证明进度条随值变化
#include "models/UIDocument.h"
#include "editor/UIRuntime.h"
#include "editor/GameViewport.h"

#include <QApplication>
#include <QKeyEvent>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPixmap>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>
#include <cstdio>

// 统计图像里"红色填充"像素数（进度条填充色设为纯红）
static int countRed(const QImage& img) {
    int n = 0;
    for (int y = 0; y < img.height(); y += 2)
        for (int x = 0; x < img.width(); x += 2) {
            QColor c = img.pixelColor(x, y);
            if (c.red() > 200 && c.green() < 70 && c.blue() < 70) n++;
        }
    return n;
}

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    QString proj = QDir::tempPath() + "/render_test_" +
                   QUuid::createUuid().toString(QUuid::WithoutBraces);
    QDir().mkpath(proj + "/UI");

    // UI：root 面板(透明) + bar 进度条(纯红填充, 1000x200)
    UIWidget root; root.id="r"; root.name="root"; root.type="UI.面板";
    root.bgColor = QColor(0,0,0,0); root.x=0; root.y=0; root.width=1920; root.height=1080;
    UIWidget bar;  bar.id="b"; bar.name="bar"; bar.type="UI.进度条"; bar.parentId="r";
    bar.x=100; bar.y=100; bar.width=1000; bar.height=200; bar.anchor="左上";
    bar.bgColor = QColor(40,40,40,255);
    bar.fillColor = QColor(255,0,0,255);   // 纯红，便于数像素
    bar.value = 1.0f;
    {
        QJsonArray arr; arr << root.toJson() << bar.toJson();
        QJsonObject o; o["name"]="血条"; o["version"]="0.1"; o["widgets"]=arr;
        QFile f(proj + "/UI/血条.ui"); f.open(QIODevice::WriteOnly);
        f.write(QJsonDocument(o).toJson()); f.close();
    }

    UIRuntime ui(proj);
    ui.createInstance("血条");
    ui.showWidgetByName("血条", "");   // 显示全部控件

    GameViewport gv;
    gv.resize(1920, 1080);
    gv.setRuntimeMode(true);
    ActorData cam;
    cam.id="cam"; cam.bpClass="builtin/Camera"; cam.cameraIsMain=true;
    cam.cameraResW=1920; cam.cameraResH=1080; cam.cameraBackground=QColor(0,0,0,255);
    gv.setRuntimeActors({cam});
    gv.setUIRuntime(&ui);

    auto renderRed = [&](float value) -> int {
        ui.setValueByName("血条", "bar", value);
        QImage img = gv.grab().toImage();
        return countRed(img);
    };

    int full = renderRed(1.0f);
    int half = renderRed(0.5f);
    int zero = renderRed(0.0f);

    printf("进度条填充红色像素数：value=1.0 → %d, value=0.5 → %d, value=0.0 → %d\n",
           full, half, zero);

    int pass = 0, fail = 0;
    auto chk = [&](const char* msg, bool ok){
        if (ok){pass++;printf("  \xE2\x9C\x85 %s\n",msg);} else {fail++;printf("  \xE2\x9D\x8C %s\n",msg);}
    };
    chk("value=1.0 时进度条有大量填充像素（确实画出来了）", full > 1000);
    chk("value=0.0 时几乎没有填充像素（空条）", zero < full / 10);
    chk("value=0.5 的填充约为 1.0 的一半（随值变化）",
        half > full * 0.4 && half < full * 0.6);

    // ── 验证按键 → 信号 这一步（GameViewport 侧）─────────────────────────
    QString gotKey;
    QObject::connect(&gv, &GameViewport::keyPressed, [&](const QString& k){ gotKey = k; });
    QKeyEvent ev(QEvent::KeyPress, Qt::Key_W, Qt::NoModifier);
    QApplication::sendEvent(&gv, &ev);
    chk("按 W 键 → GameViewport 发出 keyPressed(\"W\") 信号", gotKey == "W");
    printf("    [debug] 收到的按键 = '%s'\n", gotKey.toUtf8().constData());

    printf("\n========== 渲染测试：通过 %d / 失败 %d ==========\n", pass, fail);
    QDir(proj).removeRecursively();
    return fail == 0 ? 0 : 1;
}
