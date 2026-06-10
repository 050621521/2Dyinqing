#include "BlueprintFloatWindow.h"
#include <QCloseEvent>

BlueprintFloatWindow::BlueprintFloatWindow(QWidget* parent)
    : QMainWindow(parent, Qt::Window)
{
    setWindowTitle("关卡蓝图");
    resize(900, 600);
}

void BlueprintFloatWindow::closeEvent(QCloseEvent* e) {
    emit closed();
    e->accept();
}
