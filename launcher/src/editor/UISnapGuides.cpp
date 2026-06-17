#include "editor/UISnapGuides.h"
#include <cmath>

void UISnapGuides::clear() { m_lines.clear(); }
void UISnapGuides::addLine(const SnapLine& line) { m_lines.append(line); }

SnapResult UISnapGuides::snap(const QRectF& r, double thr) const {
    SnapResult res;
    // 候选锚点：矩形左/中/右(竖)、上/中/下(横)
    const double vx[3] = { r.left(), r.center().x(), r.right() };
    const double hy[3] = { r.top(),  r.center().y(), r.bottom() };

    double bestVX = thr; double bestHY = thr;
    for (const SnapLine& ln : m_lines) {
        if (ln.vertical) {
            for (double x : vx) {
                double d = std::abs(ln.pos - x);
                if (d < bestVX) {
                    bestVX = d; res.dx = ln.pos - x; res.snappedX = true;
                }
            }
        } else {
            for (double y : hy) {
                double d = std::abs(ln.pos - y);
                if (d < bestHY) {
                    bestHY = d; res.dy = ln.pos - y; res.snappedY = true;
                }
            }
        }
    }
    // 收集所有命中阈值的线用于高亮（用吸附后的最终坐标判定）
    if (res.snappedX || res.snappedY) {
        const QRectF snapped = r.translated(res.dx, res.dy);
        const double sx[3] = { snapped.left(), snapped.center().x(), snapped.right() };
        const double sy[3] = { snapped.top(),  snapped.center().y(), snapped.bottom() };
        for (const SnapLine& ln : m_lines) {
            bool hit = false;
            if (ln.vertical && res.snappedX)
                for (double x : sx) if (std::abs(ln.pos - x) < 0.5) hit = true;
            if (!ln.vertical && res.snappedY)
                for (double y : sy) if (std::abs(ln.pos - y) < 0.5) hit = true;
            if (hit) res.activeLines.append(ln);
        }
    }
    return res;
}
