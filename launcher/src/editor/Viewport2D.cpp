#include "Viewport2D.h"
#include "models/ActorTypeUtils.h"
#include <QPainter>
#include <QPaintEvent>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QFont>
#include <QUuid>
#include <cmath>


Viewport2D::Viewport2D(QWidget* parent) : QWidget(parent) {
    setObjectName("viewport2D");
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

// ── 外部接口 ──────────────────────────────────────────────────────────

void Viewport2D::loadLevel(LevelDocument* doc) {
    m_doc = doc;
    m_selectedId.clear();
    m_pixmapCache.clear();
    update();
}

void Viewport2D::setSelectedId(const QString& id) {
    m_selectedId = id;
    update();
}

void Viewport2D::setToolMode(ToolMode mode) {
    m_toolMode = mode;
    applyToolCursor();
    update();
}

void Viewport2D::setPixelsPerUnit(float ppu) {
    m_ppu = qMax(1.0f, ppu);
    update();
}

void Viewport2D::setRuntimeMode(bool on, const QList<ActorData>& actors) {
    m_runtimeMode = on;
    if (on) {
        m_runtimeActors = actors;
        m_selectedId.clear();
        setCursor(Qt::ArrowCursor);
    } else {
        m_runtimeActors.clear();
    }
    update();
}

void Viewport2D::updateRuntimeActors(const QList<ActorData>& actors) {
    m_runtimeActors = actors;
    update();
}

void Viewport2D::syncPrintLog(const QStringList& log) {
    // 只保留最近 8 条
    m_printLog = log;
    while (m_printLog.size() > 8)
        m_printLog.removeFirst();
    update();
}

void Viewport2D::clearPrintLog() {
    m_printLog.clear();
    update();
}

void Viewport2D::applyToolCursor() {
    switch (m_toolMode) {
        case ToolMode::Select: setCursor(Qt::ArrowCursor);   break;
        case ToolMode::Move:   setCursor(Qt::SizeAllCursor); break;
        case ToolMode::Rotate: setCursor(Qt::CrossCursor);   break;
        case ToolMode::Scale:  setCursor(Qt::SizeAllCursor); break;
    }
}

// ── 坐标变换 ──────────────────────────────────────────────────────────

QPointF Viewport2D::worldToScreen(QPointF world) const {
    QPointF center(width() / 2.0 + m_offset.x(), height() / 2.0 + m_offset.y());
    return center + world * m_zoom;
}

QPointF Viewport2D::screenToWorld(QPointF screen) const {
    QPointF center(width() / 2.0 + m_offset.x(), height() / 2.0 + m_offset.y());
    return (screen - center) / m_zoom;
}

// ── 绘制 ──────────────────────────────────────────────────────────────

void Viewport2D::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    p.fillRect(rect(), QColor(0x1a, 0x1a, 0x1a));

    drawGrid(p);
    drawAxes(p);
    drawOriginMark(p);
    drawActors(p);

    if (m_runtimeMode) {
        // 运行时：红色边框提示
        p.setPen(QPen(QColor(220, 60, 60, 160), 3));
        p.setBrush(Qt::NoBrush);
        p.drawRect(rect().adjusted(1, 1, -1, -1));
        drawPrintLog(p);
    } else if (m_doc) {
        p.setPen(QColor(70, 70, 70));
        p.setFont(QFont("PingFang SC", 10));
        p.drawText(rect().adjusted(12, 12, -12, -12),
                   Qt::AlignTop | Qt::AlignLeft, m_doc->name());
    }
}

void Viewport2D::drawGrid(QPainter& p) {
    const float baseStep = 50.0f;
    float step = baseStep * m_zoom;
    while (step < 20) step *= 5;
    while (step > 100) step /= 5;

    QPointF origin = worldToScreen({0, 0});
    p.setPen(QPen(QColor(40, 40, 40), 1));

    float startX = std::fmod(origin.x(), step);
    if (startX < 0) startX += step;
    for (float x = startX; x < width(); x += step)
        p.drawLine(QPointF(x, 0), QPointF(x, height()));

    float startY = std::fmod(origin.y(), step);
    if (startY < 0) startY += step;
    for (float y = startY; y < height(); y += step)
        p.drawLine(QPointF(0, y), QPointF(width(), y));
}

void Viewport2D::drawAxes(QPainter& p) {
    QPointF origin = worldToScreen({0, 0});
    p.setPen(QPen(QColor(180, 50, 50), 1.5f));
    p.drawLine(QPointF(0, origin.y()), QPointF(width(), origin.y()));
    p.setPen(QPen(QColor(50, 180, 50), 1.5f));
    p.drawLine(QPointF(origin.x(), 0), QPointF(origin.x(), height()));
}

void Viewport2D::drawOriginMark(QPainter& p) {
    QPointF o = worldToScreen({0, 0});
    p.setPen(QPen(Qt::white, 1));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(o, 4.0, 4.0);
}

void Viewport2D::drawActors(QPainter& p) {
    if (!m_doc && !m_runtimeMode) return;

    const QList<ActorData>& sorted = m_runtimeMode ? m_runtimeActors : m_doc->sortedActors();

    for (const ActorData& a : sorted) {
        QPointF pos = worldToScreen({a.x, a.y});

        // 预加载贴图；精灵显示尺寸 = 像素尺寸 / m_ppu × zoom × scale
        if (!a.spritePath.isEmpty() && !m_pixmapCache.contains(a.spritePath))
            m_pixmapCache[a.spritePath] = QPixmap(a.spritePath);
        const bool hasPx = !a.spritePath.isEmpty() && !m_pixmapCache[a.spritePath].isNull();

        const float szBase = qMax(24.0f, 40.0f * m_zoom);
        const float szW = hasPx
            ? m_pixmapCache[a.spritePath].width()  / m_ppu * m_zoom * qMax(0.05f, qAbs(a.scaleX))
            : szBase * qMax(0.05f, qAbs(a.scaleX));
        const float szH = hasPx
            ? m_pixmapCache[a.spritePath].height() / m_ppu * m_zoom * qMax(0.05f, qAbs(a.scaleY))
            : szBase * qMax(0.05f, qAbs(a.scaleY));
        const float sz  = (szW + szH) * 0.5f;
        QRectF rect(pos.x() - szW / 2, pos.y() - szH / 2, szW, szH);
        const bool selected = (a.id == m_selectedId);

        QColor fill = actorTypeColor(a.type);

        QPen outline(selected ? QColor(255, 255, 255) : fill.darker(160),
                     selected ? 2.5 : 1.0);

        // 旋转变换（选中框也随之旋转）
        p.save();
        if (a.rotation != 0.0f) {
            p.translate(pos.x(), pos.y());
            p.rotate(a.rotation);
            p.translate(-pos.x(), -pos.y());
        }

        // 优先：有贴图则任何类型都渲染图片
        bool drewPixmap = false;
        if (!a.spritePath.isEmpty()) {
            if (!m_pixmapCache.contains(a.spritePath))
                m_pixmapCache[a.spritePath] = QPixmap(a.spritePath);
            const QPixmap& px = m_pixmapCache[a.spritePath];
            if (!px.isNull()) {
                p.save();
                if (a.flipX || a.flipY) {
                    QTransform t;
                    t.translate(pos.x(), pos.y());
                    t.scale(a.flipX ? -1.0 : 1.0, a.flipY ? -1.0 : 1.0);
                    t.translate(-pos.x(), -pos.y());
                    p.setTransform(t, true);
                }
                p.setOpacity(a.spriteColor.alphaF());
                if (a.drawMode == "平铺")
                    p.drawTiledPixmap(rect.toRect(), px);
                else
                    p.drawPixmap(rect.toRect(), px);
                if (a.spriteColor.red() != 255 || a.spriteColor.green() != 255
                        || a.spriteColor.blue() != 255) {
                    p.setCompositionMode(QPainter::CompositionMode_SourceAtop);
                    QColor tint = a.spriteColor; tint.setAlpha(100);
                    p.fillRect(rect, tint);
                    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
                }
                p.setOpacity(1.0);
                p.restore();
                drewPixmap = true;
            }
        }

        // 摄像机视口矩形（在旋转变换内，与摄像机同向）
        const bool hasCamera = (a.type == "Camera" || a.components.contains("摄像机组件"));
        if (hasCamera && !m_runtimeMode) {
            const float halfH = a.cameraSize * m_zoom;
            const float halfW = halfH * (a.cameraResH > 0 ? (float)a.cameraResW / a.cameraResH : 1.7778f);
            QRectF frustum(pos.x() - halfW, pos.y() - halfH, halfW * 2, halfH * 2);
            if (a.cameraIsMain)
                p.setPen(QPen(QColor(255, 255, 255, 220), 1.5f, Qt::SolidLine));
            else
                p.setPen(QPen(QColor(80, 160, 240, selected ? 200 : 70), 1.5f, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawRect(frustum);
        }

        // 无贴图时按类型绘制占位图形
        if (!drewPixmap) {
            if (a.type == "Empty") {
                const float cs = sz * 0.28f;
                p.setPen(QPen(fill, selected ? 2.0f : 1.5f));
                p.setBrush(Qt::NoBrush);
                p.drawLine(QPointF(pos.x() - cs, pos.y()), QPointF(pos.x() + cs, pos.y()));
                p.drawLine(QPointF(pos.x(), pos.y() - cs), QPointF(pos.x(), pos.y() + cs));
                p.drawEllipse(pos, cs * 0.4, cs * 0.4);
            } else if (a.type == "Trigger") {
                QPen dp = outline; dp.setStyle(Qt::DashLine);
                p.setPen(dp);
                p.setBrush(QColor(fill.red(), fill.green(), fill.blue(), 55));
                p.drawRect(rect);
            } else if (a.type == "Light") {
                p.setPen(outline);
                p.setBrush(fill);
                p.drawEllipse(rect);
                p.setPen(QPen(fill.lighter(160), 1));
                const float r2 = sz * 0.72f;
                for (int i = 0; i < 8; ++i) {
                    float angle = i * static_cast<float>(M_PI) / 4.0f;
                    float dx = std::cos(angle), dy = std::sin(angle);
                    p.drawLine(QPointF(pos.x() + dx * sz * 0.5f, pos.y() + dy * sz * 0.5f),
                               QPointF(pos.x() + dx * r2,         pos.y() + dy * r2));
                }
            } else {
                p.setPen(outline);
                p.setBrush(fill);
                p.drawRect(rect);
                if (a.type == "Camera") {
                    p.setBrush(Qt::white); p.setPen(Qt::NoPen);
                    QPolygonF tri;
                    tri << QPointF(pos.x() + sz * 0.28f, pos.y())
                        << QPointF(pos.x() + sz * 0.5f,  pos.y() - sz * 0.18f)
                        << QPointF(pos.x() + sz * 0.5f,  pos.y() + sz * 0.18f);
                    p.drawPolygon(tri);
                }
            }
        }

        // 禁用状态蒙版
        if (!a.active && (drewPixmap || a.type != "Empty")) {
            p.setBrush(QColor(0, 0, 0, 130));
            p.setPen(Qt::NoPen);
            p.drawRect(rect);
        }

        // 选中外框（随旋转）
        if (selected) {
            p.setPen(QPen(QColor(255, 200, 50), 1.5, Qt::SolidLine));
            p.setBrush(Qt::NoBrush);
            if (a.type == "Empty" && !drewPixmap) {
                const float cs = sz * 0.28f;
                p.drawEllipse(pos, cs * 0.7, cs * 0.7);
            } else {
                p.drawRect(rect.adjusted(-4, -4, 4, 4));
            }
        }

        p.restore(); // 结束旋转变换

        // 边界限制框（世界空间绝对坐标，不随摄像机旋转）
        if (hasCamera && !m_runtimeMode
                && a.components.contains("边界限制组件") && a.confinerEnabled) {
            QPointF tl = worldToScreen({a.confinerMinX, a.confinerMinY});
            float bW = (a.confinerMaxX - a.confinerMinX) * m_zoom;
            float bH = (a.confinerMaxY - a.confinerMinY) * m_zoom;
            p.setPen(QPen(QColor(220, 130, 50, 180), 1.5f, Qt::DashDotLine));
            p.setBrush(Qt::NoBrush);
            p.drawRect(QRectF(tl.x(), tl.y(), bW, bH));
        }

        // Gizmo 在屏幕坐标绘制（不受旋转影响）
        if (selected) drawGizmo(p, a, rect, pos);
    }
}

void Viewport2D::drawGizmo(QPainter& p, const ActorData&, const QRectF& rect, const QPointF& pos) {
    const float sz = rect.width();

    if (m_toolMode == ToolMode::Move) {
        const float len = 60.0f;
        const float hs  = 5.0f;
        // 中心灰色方块
        p.setBrush(QColor(200, 200, 200)); p.setPen(Qt::NoPen);
        p.drawRect(QRectF(pos.x() - hs, pos.y() - hs, hs * 2, hs * 2));
        // X 轴：红色线 + 红色箭头
        p.setPen(QPen(QColor(220, 60, 60), 1.5f)); p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(pos.x() + hs, pos.y()), QPointF(pos.x() + len, pos.y()));
        p.setBrush(QColor(220, 60, 60)); p.setPen(Qt::NoPen);
        QPolygonF arrowX;
        arrowX << QPointF(pos.x() + len + 9, pos.y())
               << QPointF(pos.x() + len - 1, pos.y() - 5)
               << QPointF(pos.x() + len - 1, pos.y() + 5);
        p.drawPolygon(arrowX);
        // Y 轴：绿色线 + 绿色箭头（向上）
        p.setPen(QPen(QColor(100, 220, 60), 1.5f)); p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(pos.x(), pos.y() - hs), QPointF(pos.x(), pos.y() - len));
        p.setBrush(QColor(100, 220, 60)); p.setPen(Qt::NoPen);
        QPolygonF arrowY;
        arrowY << QPointF(pos.x(), pos.y() - len - 9)
               << QPointF(pos.x() - 5, pos.y() - len + 1)
               << QPointF(pos.x() + 5, pos.y() - len + 1);
        p.drawPolygon(arrowY);

    } else if (m_toolMode == ToolMode::Rotate) {
        const float r = sz * 0.72f;
        p.setPen(QPen(QColor(255, 255, 255, 160), 1.5f));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(pos, r, r);
        // 顶部把手点
        p.setBrush(QColor(255, 255, 255));
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(pos.x(), pos.y() - r), 5.0, 5.0);

    } else if (m_toolMode == ToolMode::Scale) {
        const float len = 60.0f;
        const float hs  = 5.0f;
        // 中心灰色方块
        p.setBrush(QColor(200, 200, 200));
        p.setPen(Qt::NoPen);
        p.drawRect(QRectF(pos.x() - hs, pos.y() - hs, hs * 2, hs * 2));
        // X 轴：红色线 + 红色方块端点
        p.setPen(QPen(QColor(220, 60, 60), 1.5f));
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(pos.x() + hs, pos.y()), QPointF(pos.x() + len, pos.y()));
        p.setBrush(QColor(220, 60, 60)); p.setPen(Qt::NoPen);
        p.drawRect(QRectF(pos.x() + len - hs, pos.y() - hs, hs * 2, hs * 2));
        // Y 轴：绿色线 + 绿色方块端点（屏幕向上）
        p.setPen(QPen(QColor(100, 220, 60), 1.5f));
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(pos.x(), pos.y() - hs), QPointF(pos.x(), pos.y() - len));
        p.setBrush(QColor(100, 220, 60)); p.setPen(Qt::NoPen);
        p.drawRect(QRectF(pos.x() - hs, pos.y() - len - hs, hs * 2, hs * 2));
    }
    // Select 模式：不绘制额外 Gizmo
}

// ── 运行时日志覆盖层 ──────────────────────────────────────────────────

void Viewport2D::drawPrintLog(QPainter& p) {
    // 运行中标记（右上角）
    {
        QFont f("PingFang SC", 11, QFont::Bold);
        p.setFont(f);
        QRect badge(width() - 90, 8, 82, 22);
        p.fillRect(badge, QColor(220, 60, 60, 200));
        p.setPen(Qt::white);
        p.drawText(badge, Qt::AlignCenter, "▶  运行中");
    }

    if (m_printLog.isEmpty()) return;

    const int lineH = 22;
    const int padX  = 10;
    const int rows  = m_printLog.size();

    QFont font("Menlo", 13);
    p.setFont(font);
    QFontMetrics fm(font);

    int maxW = 60; // 最小宽度
    QStringList display;
    for (const QString& s : m_printLog) {
        QString d = s.isEmpty() ? "(空)" : s;
        display << d;
        maxW = qMax(maxW, fm.horizontalAdvance(d));
    }

    const int bgH   = rows * lineH + 10;
    const int bgY   = height() - bgH - 10;
    QRect bg(padX - 4, bgY - 4, maxW + 20, bgH);
    p.fillRect(bg, QColor(0, 0, 0, 200));

    p.setPen(QColor(80, 230, 80, 230)); // 绿色，UE4 风格
    for (int i = 0; i < rows; ++i) {
        int y = bgY + i * lineH + lineH - 4;
        p.drawText(padX + 4, y, display[i]);
    }
}

// ── 交互 ──────────────────────────────────────────────────────────────

void Viewport2D::keyPressEvent(QKeyEvent* e) {
    if (m_runtimeMode) {
        QString key = e->text().toUpper();
        if (key.isEmpty()) {
            // 处理方向键等特殊键
            if (e->key() == Qt::Key_Up)    key = "UP";
            else if (e->key() == Qt::Key_Down)  key = "DOWN";
            else if (e->key() == Qt::Key_Left)  key = "LEFT";
            else if (e->key() == Qt::Key_Right) key = "RIGHT";
        }
        if (!key.isEmpty())
            emit keyPressed(key);
        return;
    }
    QWidget::keyPressEvent(e);
}

void Viewport2D::wheelEvent(QWheelEvent* e) {
    float factor = e->angleDelta().y() > 0 ? 1.15f : (1.0f / 1.15f);
    m_zoom = qBound(0.05f, m_zoom * factor, 50.0f);
    update();
}

void Viewport2D::mousePressEvent(QMouseEvent* e) {
    if (m_runtimeMode) return;
    if (e->button() == Qt::LeftButton) {
        if (m_doc) {
            // Move 模式：先检测已选 Actor 的箭头端点
            if (m_toolMode == ToolMode::Move && !m_selectedId.isEmpty()) {
                for (const ActorData& a : m_doc->actors()) {
                    if (a.id != m_selectedId) continue;
                    QPointF pos = worldToScreen({a.x, a.y});
                    const float len = 60.0f;
                    const float hs  = 12.0f;
                    QRectF xHandle(pos.x() + len - hs + 5, pos.y() - hs, hs * 2, hs * 2);
                    QRectF yHandle(pos.x() - hs, pos.y() - len - hs - 5, hs * 2, hs * 2);
                    ScaleHandle hit = ScaleHandle::None;
                    if      (xHandle.contains(e->pos())) hit = ScaleHandle::AxisX;
                    else if (yHandle.contains(e->pos())) hit = ScaleHandle::AxisY;
                    if (hit != ScaleHandle::None) {
                        m_scaleHandle = hit;
                        m_dragging    = true;
                        m_dragActorId = a.id;
                        m_dragStart   = screenToWorld(e->pos());
                        update();
                        return;
                    }
                    break;
                }
            }

            // Scale 模式：先检测已选 Actor 的 Gizmo 轴端点
            if (m_toolMode == ToolMode::Scale && !m_selectedId.isEmpty()) {
                for (const ActorData& a : m_doc->actors()) {
                    if (a.id != m_selectedId) continue;
                    QPointF pos = worldToScreen({a.x, a.y});
                    const float len = 60.0f;
                    const float hs  = 10.0f;
                    QRectF xHandle(pos.x() + len - hs, pos.y() - hs,       hs * 2, hs * 2);
                    QRectF yHandle(pos.x() - hs,       pos.y() - len - hs, hs * 2, hs * 2);
                    QRectF cHandle(pos.x() - hs,       pos.y() - hs,       hs * 2, hs * 2);
                    ScaleHandle hit = ScaleHandle::None;
                    if      (xHandle.contains(e->pos())) hit = ScaleHandle::AxisX;
                    else if (yHandle.contains(e->pos())) hit = ScaleHandle::AxisY;
                    else if (cHandle.contains(e->pos())) hit = ScaleHandle::Center;
                    if (hit != ScaleHandle::None) {
                        m_scaleHandle     = hit;
                        m_dragging        = true;
                        m_dragActorId     = a.id;
                        m_dragAnchor      = e->pos();
                        m_dragScaleStartX = a.scaleX;
                        m_dragScaleStartY = a.scaleY;
                        update();
                        return;
                    }
                    break;
                }
            }

            for (const ActorData& a : m_doc->actors()) {
                QPointF pos = worldToScreen({a.x, a.y});
                if (!a.spritePath.isEmpty() && !m_pixmapCache.contains(a.spritePath))
                    m_pixmapCache[a.spritePath] = QPixmap(a.spritePath);
                const bool hasPxH = !a.spritePath.isEmpty() && !m_pixmapCache[a.spritePath].isNull();
                const float szBaseH = qMax(24.0f, 40.0f * m_zoom);
                const float szW = hasPxH
                    ? m_pixmapCache[a.spritePath].width()  / m_ppu * m_zoom * qMax(0.05f, qAbs(a.scaleX))
                    : szBaseH * qMax(0.05f, qAbs(a.scaleX));
                const float szH = hasPxH
                    ? m_pixmapCache[a.spritePath].height() / m_ppu * m_zoom * qMax(0.05f, qAbs(a.scaleY))
                    : szBaseH * qMax(0.05f, qAbs(a.scaleY));
                QRectF hit(pos.x() - szW / 2, pos.y() - szH / 2, szW, szH);
                if (hit.contains(e->pos())) {
                    m_selectedId  = a.id;
                    m_dragging    = true;
                    m_dragActorId = a.id;
                    if (m_toolMode == ToolMode::Rotate) {
                        m_dragAnchor   = e->pos();
                        m_dragRotStart = a.rotation;
                    } else if (m_toolMode == ToolMode::Scale) {
                        m_scaleHandle     = ScaleHandle::Center;
                        m_dragAnchor      = e->pos();
                        m_dragScaleStartX = a.scaleX;
                        m_dragScaleStartY = a.scaleY;
                    } else {
                        m_scaleHandle = ScaleHandle::Center;
                        m_dragStart   = screenToWorld(e->pos());
                    }
                    emit actorSelected(a);
                    update();
                    return;
                }
            }
            m_selectedId.clear();
            m_dragging = false;
            update();
        }

    } else if (e->button() == Qt::RightButton) {
        if (!m_doc) return;

        // 判断是否点在某个 Actor 上
        for (const ActorData& a : m_doc->actors()) {
            QPointF pos = worldToScreen({a.x, a.y});
            if (!a.spritePath.isEmpty() && !m_pixmapCache.contains(a.spritePath))
                m_pixmapCache[a.spritePath] = QPixmap(a.spritePath);
            const bool hasPxR = !a.spritePath.isEmpty() && !m_pixmapCache[a.spritePath].isNull();
            const float szBaseR = qMax(24.0f, 40.0f * m_zoom);
            const float szW2 = hasPxR
                ? m_pixmapCache[a.spritePath].width()  / m_ppu * m_zoom * qMax(0.05f, qAbs(a.scaleX))
                : szBaseR * qMax(0.05f, qAbs(a.scaleX));
            const float szH2 = hasPxR
                ? m_pixmapCache[a.spritePath].height() / m_ppu * m_zoom * qMax(0.05f, qAbs(a.scaleY))
                : szBaseR * qMax(0.05f, qAbs(a.scaleY));
            QRectF hit(pos.x() - szW2 / 2, pos.y() - szH2 / 2, szW2, szH2);
            if (hit.contains(e->pos())) {
                // 点在 Actor 上：平移模式
                m_panning   = true;
                m_lastMouse = e->pos();
                setCursor(Qt::ClosedHandCursor);
                return;
            }
        }

        // 点在空白处：弹出创建菜单
        QMenu menu(this);
        auto* addMenu = menu.addMenu("在此处放置 Actor");
        const QPointF worldPos = screenToWorld(e->pos());
        for (const QString& type : kActorTypes) {
            addMenu->addAction(typeLabel(type), [this, type, worldPos]() {
                if (!m_doc) return;
                ActorData a;
                a.id         = QUuid::createUuid().toString(QUuid::WithoutBraces);
                a.name       = typeLabel(type);
                a.type       = type;
                a.components = defaultComponents(type);
                a.x    = (float)worldPos.x();
                a.y    = (float)worldPos.y();
                m_doc->addActor(a);
                m_selectedId = a.id;
                update();
                emit actorCreated(a);
            });
        }
        menu.exec(e->globalPosition().toPoint());

    } else if (e->button() == Qt::MiddleButton) {
        m_panning   = true;
        m_lastMouse = e->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void Viewport2D::mouseMoveEvent(QMouseEvent* e) {
    if (m_runtimeMode) return;
    if (m_dragging && m_doc) {
        if (m_toolMode == ToolMode::Rotate) {
            float newRot = m_dragRotStart + (e->pos().x() - m_dragAnchor.x()) * 0.4f;
            for (ActorData a : m_doc->actors()) {
                if (a.id == m_dragActorId) {
                    a.rotation = newRot;
                    m_doc->updateActor(a);
                    emit actorDragging(a);
                    break;
                }
            }
        } else if (m_toolMode == ToolMode::Scale) {
            float dx = (e->pos().x() - m_dragAnchor.x()) * 0.005f;
            float dy = (m_dragAnchor.y() - e->pos().y()) * 0.005f;
            for (ActorData a : m_doc->actors()) {
                if (a.id != m_dragActorId) continue;
                if (m_scaleHandle == ScaleHandle::AxisX) {
                    a.scaleX = qMax(0.05f, m_dragScaleStartX * (1.0f + dx));
                } else if (m_scaleHandle == ScaleHandle::AxisY) {
                    a.scaleY = qMax(0.05f, m_dragScaleStartY * (1.0f + dy));
                } else {
                    float factor = 1.0f + dy;
                    a.scaleX = qMax(0.05f, m_dragScaleStartX * factor);
                    a.scaleY = qMax(0.05f, m_dragScaleStartY * factor);
                }
                m_doc->updateActor(a);
                emit actorDragging(a);
                break;
            }
        } else {
            QPointF cur   = screenToWorld(e->pos());
            QPointF delta = cur - m_dragStart;
            for (ActorData a : m_doc->actors()) {
                if (a.id != m_dragActorId) continue;
                if (m_toolMode == ToolMode::Move && m_scaleHandle == ScaleHandle::AxisX) {
                    a.x += (float)delta.x();
                } else if (m_toolMode == ToolMode::Move && m_scaleHandle == ScaleHandle::AxisY) {
                    a.y += (float)delta.y();
                } else {
                    a.x += (float)delta.x();
                    a.y += (float)delta.y();
                }
                m_doc->updateActor(a);
                emit actorDragging(a);
                break;
            }
            m_dragStart = cur;
        }
        update();
    } else if (m_panning) {
        QPoint delta = e->pos() - m_lastMouse;
        m_offset    += delta;
        m_lastMouse  = e->pos();
        update();
    }
}

void Viewport2D::mouseReleaseEvent(QMouseEvent* e) {
    if (m_runtimeMode) return;
    if (e->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        if (m_doc) {
            for (const ActorData& a : m_doc->actors()) {
                if (a.id == m_dragActorId) {
                    emit actorTransformed(a);
                    break;
                }
            }
        }
        m_dragActorId.clear();
    }
    if (e->button() == Qt::MiddleButton || e->button() == Qt::RightButton) {
        m_panning = false;
        applyToolCursor();
    }
}
