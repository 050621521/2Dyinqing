#include "DocTabBar.h"
#include <QMouseEvent>

const QString DocTabBar::kBlueprintTabData = QStringLiteral("::blueprint::");

DocTabBar::DocTabBar(QWidget* parent) : QTabBar(parent) {}

void DocTabBar::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        const int idx = tabAt(e->pos());
        if (idx >= 0 && tabData(idx).toString() == kBlueprintTabData) {
            m_bpDragActive = true;
            m_bpDragStart  = e->globalPosition().toPoint();
        }
    }
    QTabBar::mousePressEvent(e);
}

void DocTabBar::mouseMoveEvent(QMouseEvent* e) {
    if (m_bpDragActive) {
        const QPoint delta = e->globalPosition().toPoint() - m_bpDragStart;
        if (delta.manhattanLength() > 40) {
            m_bpDragActive = false;
            emit blueprintDraggedOut(e->globalPosition().toPoint());
            return;
        }
    }
    QTabBar::mouseMoveEvent(e);
}

void DocTabBar::mouseReleaseEvent(QMouseEvent* e) {
    m_bpDragActive = false;
    QTabBar::mouseReleaseEvent(e);
}
