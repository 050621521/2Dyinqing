#pragma once
#include <QTabBar>

class DocTabBar : public QTabBar {
    Q_OBJECT
public:
    explicit DocTabBar(QWidget* parent = nullptr);

    static const QString kBlueprintTabData;  // 值为 "::blueprint::"
    static const QString kGameViewTabData;

signals:
    void blueprintDraggedOut(QPoint globalPos);

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    bool   m_bpDragActive = false;
    QPoint m_bpDragStart;
};
