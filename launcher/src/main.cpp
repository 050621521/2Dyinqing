#include "launcher/LauncherWindow.h"
#include "editor/EditorWindow.h"
#include <QApplication>

int main(int argc, char* argv[]) {
    // macOS 上强制菜单栏显示在窗口内，而非屏幕顶部
    QApplication::setAttribute(Qt::AA_DontUseNativeMenuBar);

    QApplication app(argc, argv);
    app.setApplicationName("2DYinqing");
    app.setOrganizationName("2DYinqing");
    app.setApplicationVersion("0.1.0");

    auto* launcher = new LauncherWindow;
    launcher->show();

    QObject::connect(launcher, &LauncherWindow::projectSelected,
                     [launcher](const ProjectInfo& info) {
        auto* editor = new EditorWindow(info);
        editor->show();
        launcher->hide();

        QObject::connect(editor, &EditorWindow::editorClosed, [launcher, editor]() {
            editor->deleteLater();
            launcher->show();
        });
    });

    return app.exec();
}
