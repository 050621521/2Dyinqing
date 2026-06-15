#pragma once
#include <QTabBar>
#include <QString>

class DocTabBar : public QTabBar {
    Q_OBJECT
public:
    explicit DocTabBar(QWidget* parent = nullptr);

    static const QString kBlueprintTabData;  // 值为 "::blueprint::"
    static const QString kGameViewTabData;

signals:
    // 任意蓝图 tab（关卡蓝图或 .bp Actor 蓝图）被拖出 tab 栏，携带其 tabId
    void blueprintDraggedOut(const QString& tabId);

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    bool    m_bpDragActive = false;
    QPoint  m_bpDragStart;
    QString m_bpDragTabId;
};
