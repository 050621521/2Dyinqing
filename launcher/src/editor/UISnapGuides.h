#pragma once
#include <QVector>
#include <QRectF>

struct SnapLine {
    enum Kind { Widget, Canvas, Scene, Camera, Guide };
    bool   vertical = true;   // true=竖线(吸附x)  false=横线(吸附y)
    double pos = 0;           // 世界坐标值
    Kind   kind = Widget;
    double spanLo = 0, spanHi = 0; // 另一轴覆盖范围（只画相关段）
};

struct SnapResult {
    double dx = 0, dy = 0;
    bool   snappedX = false, snappedY = false;
    QVector<SnapLine> activeLines;
};

// 纯逻辑吸附引擎：不依赖 QPainter，只产出几何数据。
class UISnapGuides {
public:
    void clear();
    void addLine(const SnapLine& line);
    // movingRect 为拖动后的世界矩形；worldThreshold = 屏幕阈值 / zoom。
    SnapResult snap(const QRectF& movingRect, double worldThreshold) const;
    const QVector<SnapLine>& candidates() const { return m_lines; }

private:
    QVector<SnapLine> m_lines;
};
