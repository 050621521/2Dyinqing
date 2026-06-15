#include "DocTabBar.h"
#include <QMouseEvent>

const QString DocTabBar::kBlueprintTabData = QStringLiteral("::blueprint::");
const QString DocTabBar::kGameViewTabData = QStringLiteral("::gameview::");

DocTabBar::DocTabBar(QWidget* parent) : QTabBar(parent) {}

void DocTabBar::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        const int idx = tabAt(e->pos());
        if (idx >= 0) {
            const QString data = tabData(idx).toString();
            // 关卡蓝图（前缀）或 Actor .bp 蓝图均可拖出浮动
            if (data.startsWith(kBlueprintTabData) || data.endsWith(".bp")) {
                m_bpDragActive = true;
                m_bpDragStart  = e->globalPosition().toPoint();
                m_bpDragTabId  = data;
            }
        }
    }
    QTabBar::mousePressEvent(e);
}

void DocTabBar::mouseMoveEvent(QMouseEvent* e) {
    if (m_bpDragActive) {
        // 仅当向 Tab 栏外（上/下）拖出时才浮动；栏内水平拖动交给基类做重排序
        const int y = e->position().toPoint().y();
        const int margin = 24;
        if (y < -margin || y > height() + margin) {
            m_bpDragActive = false;
            emit blueprintDraggedOut(m_bpDragTabId);
            return;
        }
    }
    QTabBar::mouseMoveEvent(e);
}

void DocTabBar::mouseReleaseEvent(QMouseEvent* e) {
    m_bpDragActive = false;
    QTabBar::mouseReleaseEvent(e);
}
