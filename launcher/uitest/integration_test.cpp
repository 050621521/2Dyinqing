// 真实集成测试：构造真正的 EditorWindow，点运行，查焦点，投递 W 键，抓帧数进度条像素
#include "editor/EditorWindow.h"
#include "editor/GameViewport.h"
#include "models/ProjectInfo.h"
#include <QApplication>
#include <QToolButton>
#include <QShortcut>
#include <QKeyEvent>
#include <QImage>
#include <QTimer>
#include <QEventLoop>
#include <cstdio>

// 统计进度条填充色像素（fillColor #ffcc1b2d ≈ rgb(204,27,45)）
static int countBarRed(const QImage& img) {
    int n = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x) {
            QColor c = img.pixelColor(x, y);
            if (qAbs(c.red()-204) < 50 && qAbs(c.green()-27) < 50 && qAbs(c.blue()-45) < 50)
                n++;
        }
    return n;
}

static void pump(int ms) {  // 处理事件循环（让重绘/队列连接生效）
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    const QString proj = argv[1];   // 临时工程路径
    ProjectInfo info; info.name = "MyGame"; info.path = proj;

    EditorWindow win(info);
    win.resize(1600, 1000);
    win.show();
    pump(300);

    // 找运行按钮（QToolButton 文本 "▶"）并点击
    QToolButton* runBtn = nullptr;
    for (QToolButton* b : win.findChildren<QToolButton*>())
        if (b->text() == "▶") { runBtn = b; break; }
    printf("找到运行按钮: %s\n", runBtn ? "是" : "否");
    if (!runBtn) return 2;
    runBtn->click();
    pump(300);

    GameViewport* gv = win.findChild<GameViewport*>();
    printf("找到游戏视图: %s\n", gv ? "是" : "否");
    if (!gv) return 2;

    // 1) 焦点检查：运行后游戏视图是否拿到键盘焦点
    QWidget* fw = QApplication::focusWidget();
    printf("运行后键盘焦点控件: %s | 是否=游戏视图: %s\n",
           fw ? fw->metaObject()->className() : "(无)",
           fw == gv ? "✅是" : "❌否");

    // 直接监听游戏视图的 keyPressed 信号，确认它有没有发出
    bool keyFired = false; QString firedKey;
    QObject::connect(gv, &GameViewport::keyPressed,
                     [&](const QString& k){ keyFired = true; firedKey = k; });

    int before = countBarRed(gv->grab().toImage());
    printf("按键前 进度条红色像素: %d\n", before);

    // —— 验证修复：不再手动禁用任何快捷键，直接按 W ——
    // 修复后 W 快捷键已作用域绑定到场景视口，游戏视图聚焦时不应被消费

    // 2) 模拟通过焦点系统按 W（先确保游戏视图有焦点，再投递到焦点控件）
    gv->setFocus();
    pump(50);
    QWidget* target = QApplication::focusWidget();
    printf("投递目标(焦点控件): %s\n", target ? target->metaObject()->className() : "(无)");
    {
        QKeyEvent kp(QEvent::KeyPress, Qt::Key_W, Qt::NoModifier);
        QApplication::sendEvent(target ? target : (QWidget*)gv, &kp);
        QKeyEvent kr(QEvent::KeyRelease, Qt::Key_W, Qt::NoModifier);
        QApplication::sendEvent(target ? target : (QWidget*)gv, &kr);
    }
    pump(200);

    printf("游戏视图 keyPressed 信号是否发出: %s (key='%s')\n",
           keyFired ? "✅是" : "❌否", firedKey.toUtf8().constData());

    int after = countBarRed(gv->grab().toImage());
    printf("按 W 后 进度条红色像素: %d\n", after);

    printf("\n判定：进度条 %s（before=%d after=%d）\n",
           (after > 0 && after < before * 0.75) ? "✅变短了（设置进度值生效）"
                                                : "❌没变化",
           before, after);
    return 0;
}
