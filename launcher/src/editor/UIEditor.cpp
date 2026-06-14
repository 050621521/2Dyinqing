#include "UIEditor.h"
#include "models/ActorTypeUtils.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QScrollArea>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QComboBox>
#include <QColorDialog>
#include <QDialog>
#include <QListWidget>
#include <QListWidgetItem>
#include <QDirIterator>
#include <QMenu>
#include <QPainter>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QUuid>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QDataStream>
#include <QToolButton>
#include <algorithm>

// ── AnchorPicker ──────────────────────────────────────────────────────────

static const QStringList& anchorOrder() {
    static const QStringList s = {
        "左上","正上","右上",
        "左中","居中","右中",
        "左下","正下","右下"
    };
    return s;
}

AnchorPicker::AnchorPicker(QWidget* parent) : QWidget(parent) {
    setFixedSize(63, 63);
}

void AnchorPicker::setAnchor(const QString& anchor) {
    if (m_anchor != anchor) { m_anchor = anchor; update(); }
}

void AnchorPicker::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const int cell = 21;

    p.fillRect(rect(), QColor(40, 40, 50));
    p.setPen(QPen(QColor(70, 70, 90), 1));
    p.drawRect(0, 0, width() - 1, height() - 1);
    for (int i = 1; i < 3; ++i) {
        p.drawLine(i * cell, 0, i * cell, height());
        p.drawLine(0, i * cell, width(), i * cell);
    }

    const QStringList& order = anchorOrder();
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            const QString& name = order[r * 3 + c];
            const QPointF center(c * cell + cell * 0.5, r * cell + cell * 0.5);
            if (name == m_anchor) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor("#38bdf8"));
                p.drawEllipse(center, 5.0, 5.0);
            } else {
                p.setPen(QPen(QColor(100, 100, 130), 1));
                p.setBrush(QColor(55, 55, 70));
                p.drawEllipse(center, 3.0, 3.0);
            }
        }
    }
}

void AnchorPicker::mousePressEvent(QMouseEvent* e) {
    const int cell = 21;
    const int c = qBound(0, (int)(e->position().x() / cell), 2);
    const int r = qBound(0, (int)(e->position().y() / cell), 2);
    const QString newAnchor = anchorOrder()[r * 3 + c];
    if (newAnchor != m_anchor) {
        m_anchor = newAnchor;
        update();
        emit anchorChanged(m_anchor);
    }
}

// ── UIEditorCanvas ────────────────────────────────────────────────────────

UIEditorCanvas::UIEditorCanvas(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAcceptDrops(true);
}

void UIEditorCanvas::setDoc(UIDocument* doc) {
    m_doc = doc;
    m_selectedId.clear();
    m_selectedIds.clear();
    update();
}

UIEditorCanvas::ResizeHandle UIEditorCanvas::hitResizeHandle(QPointF pos, const UIWidget& w) const {
    const QRectF r = widgetScreenRect(w, getViewportRect());
    const float hs = 7.0f;
    // 四角（优先检测）
    if (QRectF(r.left()-hs,  r.top()-hs,    hs*2, hs*2).contains(pos)) return ResizeHandle::TL;
    if (QRectF(r.right()-hs, r.top()-hs,    hs*2, hs*2).contains(pos)) return ResizeHandle::TR;
    if (QRectF(r.left()-hs,  r.bottom()-hs, hs*2, hs*2).contains(pos)) return ResizeHandle::BL;
    if (QRectF(r.right()-hs, r.bottom()-hs, hs*2, hs*2).contains(pos)) return ResizeHandle::BR;
    // 四边
    if (QRectF(r.left()-hs,  r.top()+hs,    hs*2, r.height()-hs*2).contains(pos)) return ResizeHandle::L;
    if (QRectF(r.right()-hs, r.top()+hs,    hs*2, r.height()-hs*2).contains(pos)) return ResizeHandle::R;
    if (QRectF(r.left()+hs,  r.top()-hs,    r.width()-hs*2, hs*2).contains(pos))  return ResizeHandle::T;
    if (QRectF(r.left()+hs,  r.bottom()-hs, r.width()-hs*2, hs*2).contains(pos))  return ResizeHandle::B;
    return ResizeHandle::None;
}

void UIEditorCanvas::setPixelSnap(bool enabled, int grid) {
    m_pixelSnapEnabled = enabled;
    m_snapGrid = qMax(1, grid);
}

void UIEditorCanvas::alignSelected(int type) {
    if (!m_doc || m_selectedIds.size() < 2) return;
    QList<UIWidget> sel;
    for (const UIWidget& w : m_doc->widgets())
        if (m_selectedIds.contains(w.id)) sel << w;
    if (sel.isEmpty()) return;

    float minX = sel[0].x, maxX = sel[0].x + sel[0].width;
    float minY = sel[0].y, maxY = sel[0].y + sel[0].height;
    for (const UIWidget& w : sel) {
        minX = qMin(minX, w.x); maxX = qMax(maxX, w.x + w.width);
        minY = qMin(minY, w.y); maxY = qMax(maxY, w.y + w.height);
    }
    float cx = (minX + maxX) * 0.5f;
    float cy = (minY + maxY) * 0.5f;

    QHash<QString, QPointF> positions;
    for (UIWidget w : sel) {
        switch (type) {
            case 0: w.x = minX; break;
            case 1: w.x = maxX - w.width; break;
            case 2: w.x = cx - w.width * 0.5f; break;
            case 3: w.y = minY; break;
            case 4: w.y = maxY - w.height; break;
            case 5: w.y = cy - w.height * 0.5f; break;
        }
        m_doc->updateWidget(w);
        positions[w.id] = {w.x, w.y};
    }
    update();
    if (onWidgetsMoved) onWidgetsMoved(positions);
}

void UIEditorCanvas::distributeSelected(bool horizontal) {
    if (!m_doc || m_selectedIds.size() < 3) return;
    QList<UIWidget> sel;
    for (const UIWidget& w : m_doc->widgets())
        if (m_selectedIds.contains(w.id)) sel << w;

    QHash<QString, QPointF> positions;
    if (horizontal) {
        std::sort(sel.begin(), sel.end(), [](const UIWidget& a, const UIWidget& b){ return a.x < b.x; });
        float totalW = 0; for (const UIWidget& w : sel) totalW += w.width;
        float span = sel.last().x + sel.last().width - sel.first().x;
        float gap = (span - totalW) / (sel.size() - 1);
        float x = sel.first().x;
        for (UIWidget w : sel) {
            w.x = x; m_doc->updateWidget(w);
            positions[w.id] = {w.x, w.y};
            x += w.width + gap;
        }
    } else {
        std::sort(sel.begin(), sel.end(), [](const UIWidget& a, const UIWidget& b){ return a.y < b.y; });
        float totalH = 0; for (const UIWidget& w : sel) totalH += w.height;
        float span = sel.last().y + sel.last().height - sel.first().y;
        float gap = (span - totalH) / (sel.size() - 1);
        float y = sel.first().y;
        for (UIWidget w : sel) {
            w.y = y; m_doc->updateWidget(w);
            positions[w.id] = {w.x, w.y};
            y += w.height + gap;
        }
    }
    update();
    if (onWidgetsMoved) onWidgetsMoved(positions);
}

void UIEditorCanvas::makeSameSize(bool useWidth) {
    if (!m_doc || m_selectedIds.size() < 2) return;
    UIWidget first; bool found = false;
    for (const UIWidget& w : m_doc->widgets())
        if (m_selectedIds.contains(w.id) && !found) { first = w; found = true; }
    if (!found) return;

    QHash<QString, QPointF> positions;
    for (const UIWidget& w : m_doc->widgets()) {
        if (!m_selectedIds.contains(w.id)) continue;
        UIWidget u = w;
        if (useWidth) u.width = first.width;
        else          u.height = first.height;
        m_doc->updateWidget(u);
        positions[u.id] = {u.x, u.y};
    }
    update();
    if (onWidgetsMoved) onWidgetsMoved(positions);
}

void UIEditorCanvas::setPreviewLevel(LevelDocument* level, float ppu) {
    m_level = level; m_ppu = ppu; update();
}

void UIEditorCanvas::setSelectedId(const QString& id) {
    m_selectedId = id;
    m_selectedIds.clear();
    if (!id.isEmpty()) m_selectedIds.insert(id);
    update();
}

QRectF UIEditorCanvas::widgetScreenRect(const UIWidget& w, const QRectF& parentRect) const {
    float ax, ay;
    const QString& a = w.anchor;
    if      (a == "左上" || a == "左中" || a == "左下") ax = parentRect.left();
    else if (a == "正上" || a == "居中" || a == "正下") ax = parentRect.center().x();
    else                                                  ax = parentRect.right();
    if      (a == "左上" || a == "正上" || a == "右上") ay = parentRect.top();
    else if (a == "左中" || a == "居中" || a == "右中") ay = parentRect.center().y();
    else                                                  ay = parentRect.bottom();
    return QRectF(ax + w.x, ay + w.y, w.width, w.height);
}

void UIEditorCanvas::renderWidget(QPainter& p, const UIWidget& w, const QRectF& parentRect) const {
    if (!w.visible) return;
    const QRectF r = widgetScreenRect(w, parentRect);
    const QString& t = w.type;

    // ── 第一层：精灵图片（所有控件类型通用）──────────────────────────────
    if (!w.imagePath.isEmpty()) {
        if (!m_pixmapCache.contains(w.imagePath))
            m_pixmapCache[w.imagePath] = QPixmap(w.imagePath);
        const QPixmap& px = m_pixmapCache[w.imagePath];
        if (!px.isNull()) {
            p.save();
            p.setOpacity(w.alpha);
            p.drawPixmap(r.toRect(), px.scaled(r.size().toSize(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
            p.setOpacity(1.0);
            p.restore();
        }
    }

    // ── 第二层：控件类型专属内容 ────────────────────────────────────────
    if (t == "UI.面板") {
        if (w.bgColor.alpha() > 0) p.fillRect(r, w.bgColor);
        else if (w.imagePath.isEmpty()) { p.save(); p.setPen(QPen(QColor(80,80,120,120), 1, Qt::DashLine)); p.drawRect(r); p.restore(); }
    } else if (t == "UI.文本") {
        p.save(); p.setPen(w.color);
        QFont f; f.setPixelSize(qMax(6, w.fontSize)); p.setFont(f);
        p.drawText(r, Qt::AlignVCenter | Qt::AlignLeft, w.text);
        p.restore();
    } else if (t == "UI.图片") {
        if (w.imagePath.isEmpty()) {
            p.save(); p.fillRect(r, QColor(60,60,80,120));
            p.setPen(QColor(150,150,200)); p.drawText(r, Qt::AlignCenter, "[图片]"); p.restore();
        }
    } else if (t == "UI.按钮") {
        if (w.imagePath.isEmpty())
            p.fillRect(r, w.bgColor.alpha() > 0 ? w.bgColor : QColor(60,60,100,200));
        p.save(); p.setPen(w.color);
        QFont f; f.setPixelSize(qMax(6, w.fontSize)); p.setFont(f);
        p.drawText(r, Qt::AlignCenter, w.text);
        p.restore();
    } else if (t == "UI.进度条") {
        if (w.imagePath.isEmpty())
            p.fillRect(r, w.bgColor.alpha() > 0 ? w.bgColor : QColor(40,40,40,200));
        QRectF fill = r; fill.setWidth(r.width() * qBound(0.0f, w.value, 1.0f));
        if (fill.width() > 0) p.fillRect(fill, w.fillColor);
    } else if (t == "UI.下拉菜单") {
        if (w.imagePath.isEmpty())
            p.fillRect(r, w.bgColor.alpha() > 0 ? w.bgColor : QColor(50,50,60,200));
        const QStringList opts = w.text.split('\n', Qt::SkipEmptyParts);
        const QString display = (w.selectedIndex < opts.size()) ? opts[w.selectedIndex] : "(空)";
        p.save(); p.setPen(w.color);
        QFont f; f.setPixelSize(qMax(6, w.fontSize)); p.setFont(f);
        p.drawText(r.adjusted(4,0,-16,0), Qt::AlignVCenter | Qt::AlignLeft, display);
        p.drawText(r.adjusted(0,0,-4,0),  Qt::AlignVCenter | Qt::AlignRight, "▾");
        p.restore();
    } else {
        if (w.imagePath.isEmpty()) {
            p.save(); p.setPen(QPen(QColor(100,100,180,100), 1, Qt::DotLine));
            p.drawRect(r); p.restore();
        }
    }

    if (m_selectedIds.contains(w.id)) {
        const bool isPrimary = (w.id == m_selectedId);
        p.save();
        p.setPen(QPen(QColor("#38bdf8"), isPrimary ? 2 : 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(r);
        if (isPrimary) {
            for (const QPointF& pt : {r.topLeft(), r.topRight(), r.bottomLeft(), r.bottomRight()})
                p.fillRect(QRectF(pt.x()-4, pt.y()-4, 8, 8), QColor("#38bdf8"));
        }
        p.restore();
    }

    if (t == "UI.面板"    || t == "UI.竖向布局" || t == "UI.横向布局" ||
        t == "UI.网格布局" || t == "UI.滚动视图")
        renderChildren(p, w.id, r, w);
}

void UIEditorCanvas::renderChildren(QPainter& p, const QString& parentId,
                                     const QRectF& parentRect, const UIWidget& parent) const {
    if (!m_doc) return;
    const QList<UIWidget> children = m_doc->childrenOf(parentId);

    if (parent.type == "UI.竖向布局") {
        float y = parentRect.top();
        for (const UIWidget& c : children) {
            renderWidget(p, c, QRectF(parentRect.left(), y - c.y, parentRect.width(), c.height + c.y));
            y += c.height + parent.spacing;
        }
    } else if (parent.type == "UI.横向布局") {
        float x = parentRect.left();
        for (const UIWidget& c : children) {
            renderWidget(p, c, QRectF(x - c.x, parentRect.top(), c.width + c.x, parentRect.height()));
            x += c.width + parent.spacing;
        }
    } else if (parent.type == "UI.网格布局") {
        int col = 0, row = 0;
        for (const UIWidget& c : children) {
            const float cx = parentRect.left() + col * (parent.cellW + parent.spacing);
            const float cy = parentRect.top()  + row * (parent.cellH + parent.spacing);
            renderWidget(p, c, QRectF(cx, cy, parent.cellW, parent.cellH));
            if (++col >= parent.columns) { col = 0; row++; }
        }
    } else {
        for (const UIWidget& c : children)
            renderWidget(p, c, parentRect);
    }
}

void UIEditorCanvas::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(35, 35, 40));

    const int gs = 20;
    p.setPen(QPen(QColor(45,45,50), 1));
    for (int x = 0; x < width(); x += gs)  p.drawLine(x, 0, x, height());
    for (int y = 0; y < height(); y += gs)  p.drawLine(0, y, width(), y);

    if (m_level)
        drawScenePreview(p);

    if (m_doc) {
        const QRectF vp = getViewportRect();
        for (const UIWidget& w : m_doc->rootWidgets())
            renderWidget(p, w, vp);

        // 多选整体 bounding box
        if (m_selectedIds.size() >= 2) {
            bool first = true;
            float minX = 0, minY = 0, maxX = 0, maxY = 0;
            for (const UIWidget& w : m_doc->widgets()) {
                if (!m_selectedIds.contains(w.id)) continue;
                QRectF r = widgetScreenRect(w, vp);
                if (first) { minX=(float)r.left(); minY=(float)r.top(); maxX=(float)r.right(); maxY=(float)r.bottom(); first=false; }
                else { minX=qMin(minX,(float)r.left()); minY=qMin(minY,(float)r.top()); maxX=qMax(maxX,(float)r.right()); maxY=qMax(maxY,(float)r.bottom()); }
            }
            if (!first) {
                p.setPen(QPen(Qt::white, 1, Qt::DashLine));
                p.setBrush(Qt::NoBrush);
                p.drawRect(QRectF(minX-6, minY-6, maxX-minX+12, maxY-minY+12));
            }
        }
    }

    // 框选矩形
    if (m_rubberBanding && !m_rubberRect.isNull()) {
        p.setPen(QPen(QColor(100, 150, 255, 200), 1, Qt::DashLine));
        p.setBrush(QColor(100, 150, 255, 30));
        p.drawRect(m_rubberRect);
    }
}

QRectF UIEditorCanvas::getViewportRect() const {
    float aspect = 1920.0f / 1080.0f;
    if (m_level) {
        for (const ActorData& a : m_level->sortedActors()) {
            if (a.cameraIsMain && (a.bpClass == "builtin/Camera" || a.components.contains("摄像机组件"))) {
                if (a.cameraResH > 0) aspect = (float)a.cameraResW / a.cameraResH;
                break;
            }
        }
    }
    return computeCameraRect(aspect);
}

QRectF UIEditorCanvas::computeCameraRect(float aspect) const {
    const float w = (float)width();
    const float h = (float)height();
    float camW, camH;
    if (h > 0.0f && w / h > aspect) {
        camH = h; camW = h * aspect;
    } else {
        camW = w; camH = (aspect > 0.0f) ? w / aspect : h;
    }
    return QRectF((w - camW) / 2.0f, (h - camH) / 2.0f, camW, camH);
}

QPointF UIEditorCanvas::cameraWorldToScreen(QPointF world, const QRectF& camRect, const ActorData& cam) const {
    const float halfH  = cam.cameraSize;
    const float aspect = cam.cameraResH > 0
                         ? (float)cam.cameraResW / cam.cameraResH : 1.7778f;
    const float halfW  = halfH * aspect;
    const float scaleX = (float)camRect.width()  / (halfW * 2.0f);
    const float scaleY = (float)camRect.height() / (halfH * 2.0f);
    return QPointF(
        camRect.center().x() + (world.x() - cam.x) * scaleX,
        camRect.center().y() - (world.y() - cam.y) * scaleY
    );
}

void UIEditorCanvas::drawScenePreview(QPainter& p) const {
    const QList<ActorData>& actors = m_level->sortedActors();

    const ActorData* cam = nullptr;
    for (const ActorData& a : actors) {
        if (a.cameraIsMain && (a.bpClass == "builtin/Camera" || a.components.contains("摄像机组件"))) {
            cam = &a;
            break;
        }
    }
    if (!cam) return;

    const float aspect = cam->cameraResH > 0
                         ? (float)cam->cameraResW / cam->cameraResH : 1.7778f;
    const QRectF camRect = computeCameraRect(aspect);
    p.fillRect(camRect, cam->cameraBackground);

    const float halfH  = cam->cameraSize;
    const float halfW  = halfH * aspect;
    const float scaleX = (float)camRect.width()  / (halfW * 2.0f);
    const float scaleY = (float)camRect.height() / (halfH * 2.0f);
    const float scale  = qMin(scaleX, scaleY);

    p.save();
    p.setClipRect(camRect);

    for (const ActorData& a : actors) {
        if (!a.active) continue;
        if (a.bpClass == "builtin/Camera" || a.components.contains("摄像机组件")) continue;

        const QPointF pos = cameraWorldToScreen({a.x, a.y}, camRect, *cam);

        p.save();
        if (a.rotation != 0.0f) {
            p.translate(pos.x(), pos.y());
            p.rotate(a.rotation);
            p.translate(-pos.x(), -pos.y());
        }

        bool drewPixmap = false;
        if (!a.spritePath.isEmpty()) {
            if (!m_pixmapCache.contains(a.spritePath))
                m_pixmapCache[a.spritePath] = QPixmap(a.spritePath);
            const QPixmap& px = m_pixmapCache[a.spritePath];
            if (!px.isNull()) {
                const float szW = px.width()  / m_ppu * scale * qMax(0.05f, qAbs(a.scaleX));
                const float szH = px.height() / m_ppu * scale * qMax(0.05f, qAbs(a.scaleY));
                QRectF aRect(pos.x() - szW / 2.0f, pos.y() - szH / 2.0f, szW, szH);
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
                    p.drawTiledPixmap(aRect.toRect(), px);
                else
                    p.drawPixmap(aRect.toRect(), px);
                if (a.spriteColor.red() != 255 || a.spriteColor.green() != 255
                        || a.spriteColor.blue() != 255) {
                    p.setCompositionMode(QPainter::CompositionMode_SourceAtop);
                    QColor tint = a.spriteColor; tint.setAlpha(100);
                    p.fillRect(aRect, tint);
                    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
                }
                p.setOpacity(1.0);
                p.restore();
                drewPixmap = true;
            }
        }

        if (!drewPixmap) {
            const float szBase = qMax(24.0f, 40.0f * scale);
            const float szW = szBase * qMax(0.05f, qAbs(a.scaleX));
            const float szH = szBase * qMax(0.05f, qAbs(a.scaleY));
            QRectF aRect(pos.x() - szW / 2.0f, pos.y() - szH / 2.0f, szW, szH);
            const QColor fill = bpClassColor(a.bpClass);
            p.setPen(QPen(fill.darker(160), 1.0));
            p.setBrush(fill);
            p.drawRect(aRect);
        }

        p.restore();
    }

    p.setClipping(false);
    p.restore();
}

QString UIEditorCanvas::hitTest(QPointF pos, const QString& parentId, const QRectF& parentRect) const {
    if (!m_doc) return {};
    const QList<UIWidget> children = parentId.isEmpty()
        ? m_doc->rootWidgets()
        : m_doc->childrenOf(parentId);
    for (int i = children.size() - 1; i >= 0; --i) {
        const UIWidget& w = children[i];
        const QRectF r = widgetScreenRect(w, parentRect);
        if (r.contains(pos)) {
            const QString childHit = hitTest(pos, w.id, r);
            return childHit.isEmpty() ? w.id : childHit;
        }
    }
    return {};
}

void UIEditorCanvas::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    const bool ctrl = (e->modifiers() & Qt::ControlModifier) != 0;
    const QRectF vp = getViewportRect();

    // 先检测缩放手柄（主选控件存在时）
    if (!m_selectedId.isEmpty() && !ctrl && m_doc) {
        for (const UIWidget& w : m_doc->widgets()) {
            if (w.id != m_selectedId) continue;
            ResizeHandle rh = hitResizeHandle(e->position(), w);
            if (rh != ResizeHandle::None) {
                m_resizing     = true;
                m_resizeHandle = rh;
                m_dragStart    = e->position();
                m_resizeInitX  = w.x;  m_resizeInitY = w.y;
                m_resizeInitW  = w.width; m_resizeInitH = w.height;
                return;
            }
            break;
        }
    }

    const QString hit = hitTest(e->position(), {}, vp);

    if (!hit.isEmpty()) {
        if (ctrl) {
            // Ctrl+点击：切换选中状态
            if (m_selectedIds.contains(hit)) m_selectedIds.remove(hit);
            else m_selectedIds.insert(hit);
            m_selectedId = m_selectedIds.isEmpty() ? QString() : hit;
            const QStringList ids = m_selectedIds.values();
            if (onMultiSelectionChanged) onMultiSelectionChanged(ids);
            else if (onSelectionChanged) onSelectionChanged(m_selectedId);
        } else {
            if (!m_selectedIds.contains(hit)) {
                m_selectedIds.clear();
                m_selectedIds.insert(hit);
                m_selectedId = hit;
                if (onSelectionChanged) onSelectionChanged(hit);
            } else {
                m_selectedId = hit;
            }
            // 开始拖拽：记录所有选中控件的起始位置
            m_dragging = true;
            m_dragStart = e->position();
            m_dragStartPositions.clear();
            for (const UIWidget& w : m_doc->widgets())
                if (m_selectedIds.contains(w.id))
                    m_dragStartPositions[w.id] = {w.x, w.y};
        }
        update();
    } else {
        // 点击空白
        if (!ctrl) {
            m_selectedIds.clear();
            m_selectedId.clear();
            if (onSelectionChanged) onSelectionChanged({});
        }
        m_rubberBanding = true;
        m_rubberStart   = e->pos();
        m_rubberRect    = QRect();
        update();
    }
}

void UIEditorCanvas::mouseMoveEvent(QMouseEvent* e) {
    // 缩放拖拽
    if (m_resizing && !m_selectedId.isEmpty() && m_doc) {
        const QPointF d = e->position() - m_dragStart;
        float dx = (float)d.x(), dy = (float)d.y();
        float nx = m_resizeInitX, ny = m_resizeInitY;
        float nw = m_resizeInitW, nh = m_resizeInitH;
        switch (m_resizeHandle) {
            case ResizeHandle::TL: nx+=dx; ny+=dy; nw-=dx; nh-=dy; break;
            case ResizeHandle::T:             ny+=dy;          nh-=dy; break;
            case ResizeHandle::TR:        ny+=dy; nw+=dx; nh-=dy; break;
            case ResizeHandle::R:                  nw+=dx;         break;
            case ResizeHandle::BR:         nw+=dx; nh+=dy; break;
            case ResizeHandle::B:                          nh+=dy; break;
            case ResizeHandle::BL: nx+=dx;  nw-=dx; nh+=dy; break;
            case ResizeHandle::L:  nx+=dx;  nw-=dx;         break;
            default: break;
        }
        nw = qMax(10.0f, nw); nh = qMax(10.0f, nh);
        if (onWidgetResized) onWidgetResized(m_selectedId, nx, ny, nw, nh);
        update();
        return;
    }

    // 悬停时更新缩放光标
    if (!m_dragging && !m_resizing && !m_selectedId.isEmpty() && m_doc) {
        for (const UIWidget& w : m_doc->widgets()) {
            if (w.id != m_selectedId) continue;
            const ResizeHandle rh = hitResizeHandle(e->position(), w);
            switch (rh) {
                case ResizeHandle::TL: case ResizeHandle::BR: setCursor(Qt::SizeFDiagCursor); break;
                case ResizeHandle::TR: case ResizeHandle::BL: setCursor(Qt::SizeBDiagCursor); break;
                case ResizeHandle::L:  case ResizeHandle::R:  setCursor(Qt::SizeHorCursor);   break;
                case ResizeHandle::T:  case ResizeHandle::B:  setCursor(Qt::SizeVerCursor);   break;
                default: setCursor(Qt::ArrowCursor); break;
            }
            break;
        }
    }

    if (m_rubberBanding) {
        m_rubberRect = QRect(m_rubberStart, e->pos()).normalized();
        update();
        return;
    }
    if (!m_dragging || m_selectedIds.isEmpty() || !m_doc) return;

    const QPointF totalDelta = e->position() - m_dragStart;
    const QRectF  vp = getViewportRect();

    if (m_selectedIds.size() == 1) {
        // 单选：使用原有 onWidgetMoved 回调
        QPointF start = m_dragStartPositions.value(m_selectedId, {});
        float newX = (float)(start.x() + totalDelta.x());
        float newY = (float)(start.y() + totalDelta.y());
        if (m_pixelSnapEnabled) {
            newX = std::round(newX / m_snapGrid) * m_snapGrid;
            newY = std::round(newY / m_snapGrid) * m_snapGrid;
        }
        if (onWidgetMoved) onWidgetMoved(m_selectedId, newX, newY);
    } else {
        // 多选：批量移动
        QHash<QString, QPointF> positions;
        for (const UIWidget& w : m_doc->widgets()) {
            if (!m_selectedIds.contains(w.id)) continue;
            QPointF start = m_dragStartPositions.value(w.id, {w.x, w.y});
            float newX = (float)(start.x() + totalDelta.x());
            float newY = (float)(start.y() + totalDelta.y());
            if (m_pixelSnapEnabled) {
                newX = std::round(newX / m_snapGrid) * m_snapGrid;
                newY = std::round(newY / m_snapGrid) * m_snapGrid;
            }
            positions[w.id] = {newX, newY};
        }
        if (onWidgetsMoved) onWidgetsMoved(positions);
    }
    update();
}

void UIEditorCanvas::mouseReleaseEvent(QMouseEvent* e) {
    if (m_resizing && e->button() == Qt::LeftButton) {
        m_resizing = false;
        m_resizeHandle = ResizeHandle::None;
        setCursor(Qt::ArrowCursor);
        return;
    }

    if (m_rubberBanding && e->button() == Qt::LeftButton) {
        m_rubberBanding = false;
        const bool ctrl = (e->modifiers() & Qt::ControlModifier) != 0;
        if (!ctrl) m_selectedIds.clear();
        if (m_doc && !m_rubberRect.isNull()) {
            const QRectF vp = getViewportRect();
            for (const UIWidget& w : m_doc->widgets()) {
                QRectF r = widgetScreenRect(w, vp);
                if (m_rubberRect.intersects(r.toRect()))
                    m_selectedIds.insert(w.id);
            }
        }
        m_selectedId = m_selectedIds.isEmpty() ? QString() : *m_selectedIds.begin();
        m_rubberRect = QRect();
        const QStringList ids = m_selectedIds.values();
        if (onMultiSelectionChanged) onMultiSelectionChanged(ids);
        else if (onSelectionChanged) onSelectionChanged(m_selectedId);
        update();
        return;
    }
    m_dragging = false;
    m_dragStartPositions.clear();
}

void UIEditorCanvas::contextMenuEvent(QContextMenuEvent* e) {
    QMenu menu(this);
    QMenu* addMenu = menu.addMenu("添加控件");
    const QStringList types = {
        "UI.面板","UI.文本","UI.图片","UI.按钮","UI.进度条","UI.下拉菜单",
        "UI.竖向布局","UI.横向布局","UI.网格布局","UI.滚动视图"
    };
    for (const QString& t : types)
        addMenu->addAction(t, [this, t]() { if (onAddWidget) onAddWidget(t); });
    if (!m_selectedIds.isEmpty())
        menu.addAction("删除选中", [this]() { if (onDeleteSelected) onDeleteSelected(); });
    menu.exec(e->globalPos());
}

void UIEditorCanvas::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasFormat("application/x-qabstractitemmodeldatalist"))
        e->acceptProposedAction();
}

void UIEditorCanvas::dropEvent(QDropEvent* e) {
    if (!m_doc) return;
    const QByteArray encoded = e->mimeData()->data("application/x-qabstractitemmodeldatalist");
    QDataStream stream(encoded);
    QString imagePath;
    while (!stream.atEnd()) {
        int row, col; QMap<int, QVariant> d;
        stream >> row >> col >> d;
        if (d.value(Qt::UserRole).toString() == "image") {
            imagePath = d.value(Qt::UserRole + 1).toString();
            break;
        }
    }
    if (imagePath.isEmpty()) return;

    // 找命中的控件（所有类型都可接受精灵图片）
    const QRectF vp = getViewportRect();
    const QString hitId = hitTest(e->position(), {}, vp);
    if (hitId.isEmpty()) return;
    if (onImageDropped) onImageDropped(hitId, imagePath);
    e->acceptProposedAction();
}

// ── UIEditor ──────────────────────────────────────────────────────────────

UIEditor::UIEditor(QWidget* parent) : QWidget(parent) {
    setObjectName("uiEditor");
    auto* rootLay = new QHBoxLayout(this);
    rootLay->setContentsMargins(0, 0, 0, 0);
    rootLay->setSpacing(0);

    auto* splitter = new QSplitter(Qt::Horizontal, this);

    // 左侧：控件树
    auto* leftWrap = new QWidget;
    auto* leftLay  = new QVBoxLayout(leftWrap);
    leftLay->setContentsMargins(0, 0, 0, 0);
    leftLay->setSpacing(0);

    auto* treeHeader = new QWidget;
    treeHeader->setFixedHeight(26);
    auto* th = new QHBoxLayout(treeHeader);
    th->setContentsMargins(8, 2, 4, 2);
    auto* treeLabel = new QLabel("控件树");
    auto* addBtn    = new QPushButton("+");
    addBtn->setFixedSize(20, 20);
    th->addWidget(treeLabel);
    th->addStretch();
    th->addWidget(addBtn);

    m_tree = new QTreeWidget;
    m_tree->setHeaderHidden(true);
    m_tree->setIndentation(14);
    m_tree->installEventFilter(this);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        if (!m_tree->itemAt(pos) || m_selectedId.isEmpty()) return;
        QMenu menu(m_tree);
        menu.addAction("删除", this, &UIEditor::onDeleteSelected);
        menu.exec(m_tree->viewport()->mapToGlobal(pos));
    });

    leftLay->addWidget(treeHeader);
    leftLay->addWidget(m_tree);
    leftWrap->setFixedWidth(180);

    // 中间：对齐工具栏 + 画布 + 底部工具栏
    auto* centerWrap = new QWidget;
    auto* centerLay  = new QVBoxLayout(centerWrap);
    centerLay->setContentsMargins(0, 0, 0, 0);
    centerLay->setSpacing(0);

    // ── 对齐工具栏 ──
    auto* alignBar = new QWidget;
    alignBar->setFixedHeight(28);
    alignBar->setObjectName("viewportToolBar");
    auto* alignLay = new QHBoxLayout(alignBar);
    alignLay->setContentsMargins(6, 2, 6, 2);
    alignLay->setSpacing(2);

    auto aBtn = [&](const QString& icon, const QString& tip) {
        auto* b = new QToolButton(alignBar);
        b->setText(icon); b->setToolTip(tip);
        b->setObjectName("vpTBBtn");
        b->setEnabled(false);
        alignLay->addWidget(b);
        m_alignBtns << b;
        return b;
    };
    auto aSep = [&]() {
        auto* f = new QFrame(alignBar);
        f->setFrameShape(QFrame::VLine);
        f->setObjectName("vpSep");
        alignLay->addSpacing(3); alignLay->addWidget(f); alignLay->addSpacing(3);
    };

    auto* bAlignL = aBtn("←|",  "左对齐");
    auto* bAlignHC= aBtn("|·|", "水平居中");
    auto* bAlignR = aBtn("|→",  "右对齐");
    aSep();
    auto* bAlignT = aBtn("↑—",  "上对齐");
    auto* bAlignVC= aBtn("—·—", "垂直居中");
    auto* bAlignB = aBtn("—↓",  "下对齐");
    aSep();
    auto* bDistH  = aBtn("⇔",   "水平等间距");
    auto* bDistV  = aBtn("⇕",   "垂直等间距");
    aSep();
    auto* bSameW  = aBtn("↔=",  "等宽");
    auto* bSameH  = aBtn("↕=",  "等高");

    alignLay->addSpacing(8);
    // 像素吸附控件
    auto* snapBtn = new QToolButton(alignBar);
    snapBtn->setText("⊡"); snapBtn->setToolTip("像素吸附");
    snapBtn->setObjectName("vpTBBtn");
    snapBtn->setCheckable(true); snapBtn->setChecked(true);
    alignLay->addWidget(snapBtn);
    auto* snapCombo = new QComboBox(alignBar);
    snapCombo->setObjectName("vpSnapCombo");
    snapCombo->addItems({"1px", "5px", "10px"});
    snapCombo->setFixedWidth(48);
    alignLay->addWidget(snapCombo);
    alignLay->addStretch();

    connect(bAlignL,  &QToolButton::clicked, this, [this](){ m_canvas->alignSelected(0); emit documentModified(); });
    connect(bAlignR,  &QToolButton::clicked, this, [this](){ m_canvas->alignSelected(1); emit documentModified(); });
    connect(bAlignHC, &QToolButton::clicked, this, [this](){ m_canvas->alignSelected(2); emit documentModified(); });
    connect(bAlignT,  &QToolButton::clicked, this, [this](){ m_canvas->alignSelected(3); emit documentModified(); });
    connect(bAlignB,  &QToolButton::clicked, this, [this](){ m_canvas->alignSelected(4); emit documentModified(); });
    connect(bAlignVC, &QToolButton::clicked, this, [this](){ m_canvas->alignSelected(5); emit documentModified(); });
    connect(bDistH,   &QToolButton::clicked, this, [this](){ m_canvas->distributeSelected(true);  emit documentModified(); });
    connect(bDistV,   &QToolButton::clicked, this, [this](){ m_canvas->distributeSelected(false); emit documentModified(); });
    connect(bSameW,   &QToolButton::clicked, this, [this](){ m_canvas->makeSameSize(true);  emit documentModified(); });
    connect(bSameH,   &QToolButton::clicked, this, [this](){ m_canvas->makeSameSize(false); emit documentModified(); });

    connect(snapBtn, &QToolButton::toggled, this, [this, snapCombo](bool on) {
        snapCombo->setEnabled(on);
        static const int grids[] = {1, 5, 10};
        m_canvas->setPixelSnap(on, grids[snapCombo->currentIndex()]);
    });
    connect(snapCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, snapBtn](int idx) {
        static const int grids[] = {1, 5, 10};
        if (snapBtn->isChecked()) m_canvas->setPixelSnap(true, grids[idx]);
    });

    centerLay->addWidget(alignBar);

    m_canvas = new UIEditorCanvas(this);

    auto* bottomBar = new QWidget;
    bottomBar->setFixedHeight(26);
    auto* bl = new QHBoxLayout(bottomBar);
    bl->setContentsMargins(8, 2, 8, 2);
    bl->addWidget(new QLabel("背景预览："));
    m_bgCombo = new QComboBox;
    m_bgCombo->addItem("关闭");
    m_bgCombo->setFixedWidth(140);
    bl->addWidget(m_bgCombo);
    bl->addStretch();

    centerLay->addWidget(m_canvas, 1);
    centerLay->addWidget(bottomBar);

    // 右侧：属性面板
    m_propScroll = new QScrollArea;
    m_propScroll->setWidgetResizable(true);
    m_propScroll->setFixedWidth(200);
    m_props = new QWidget;
    new QVBoxLayout(m_props);
    m_propScroll->setWidget(m_props);

    splitter->addWidget(leftWrap);
    splitter->addWidget(centerWrap);
    splitter->addWidget(m_propScroll);
    splitter->setStretchFactor(1, 1);
    rootLay->addWidget(splitter);

    connect(m_tree, &QTreeWidget::currentItemChanged,
            this, &UIEditor::onTreeSelectionChanged);

    m_tree->setEditTriggers(QAbstractItemView::EditKeyPressed);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
        if (item) m_tree->editItem(item, 0);
    });
    connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int) {
        if (!m_doc) return;
        const QString id = item->data(0, Qt::UserRole).toString();
        if (id.isEmpty()) return;
        const QString newName = item->text(0).trimmed();
        if (newName.isEmpty()) {
            for (const UIWidget& w : m_doc->widgets()) {
                if (w.id == id) { QSignalBlocker b(m_tree); item->setText(0, w.name); break; }
            }
            return;
        }
        for (const UIWidget& w : m_doc->widgets()) {
            if (w.id == id) {
                if (w.name == newName) break;
                UIWidget u = w; u.name = newName;
                m_doc->updateWidget(u);
                if (id == m_selectedId) rebuildPropsPanel(id);
                emit documentModified();
                break;
            }
        }
    });

    connect(m_bgCombo, &QComboBox::currentTextChanged, this, [this](const QString& name) {
        if (name == "关闭")
            m_canvas->setPreviewLevel(nullptr, m_ppu);
        emit previewLevelChanged(name);
    });

    // ── 注册画布回调 ──
    auto updateAlignBtns = [this](int selCount) {
        const bool canAlign = selCount >= 2;
        const bool canDist  = selCount >= 3;
        for (int i = 0; i < m_alignBtns.size(); ++i) {
            // 按钮 6-7 是分布按钮（需≥3），其余需≥2
            m_alignBtns[i]->setEnabled(i >= 6 && i <= 7 ? canDist : canAlign);
        }
    };

    m_canvas->onSelectionChanged = [this, updateAlignBtns](const QString& id) {
        m_selectedId = id;
        rebuildPropsPanel(id);
        updateAlignBtns(id.isEmpty() ? 0 : 1);
        QTreeWidgetItemIterator it(m_tree);
        while (*it) {
            if ((*it)->data(0, Qt::UserRole).toString() == id) {
                m_tree->setCurrentItem(*it);
                break;
            }
            ++it;
        }
    };

    m_canvas->onMultiSelectionChanged = [this, updateAlignBtns](QStringList ids) {
        const int n = ids.size();
        updateAlignBtns(n);
        if (n == 1) {
            m_selectedId = ids.first();
            rebuildPropsPanel(m_selectedId);
        } else {
            m_selectedId = ids.isEmpty() ? QString() : ids.first();
            if (n == 0) rebuildPropsPanel({});
            // 多选时不展示属性（保持现有空状态）
        }
        // 树中高亮第一个选中项
        if (!m_selectedId.isEmpty()) {
            QTreeWidgetItemIterator it(m_tree);
            while (*it) {
                if ((*it)->data(0, Qt::UserRole).toString() == m_selectedId) {
                    m_tree->setCurrentItem(*it);
                    break;
                }
                ++it;
            }
        }
    };

    m_canvas->onWidgetMoved = [this](const QString& id, float x, float y) {
        if (!m_doc) return;
        for (const UIWidget& w : m_doc->widgets()) {
            if (w.id == id) {
                UIWidget u = w; u.x = x; u.y = y;
                m_doc->updateWidget(u);
                rebuildPropsPanel(id);
                emit documentModified();
                break;
            }
        }
    };

    m_canvas->onWidgetsMoved = [this](QHash<QString,QPointF> positions) {
        if (!m_doc) return;
        for (const UIWidget& w : m_doc->widgets()) {
            if (!positions.contains(w.id)) continue;
            UIWidget u = w;
            u.x = (float)positions[w.id].x();
            u.y = (float)positions[w.id].y();
            m_doc->updateWidget(u);
        }
        emit documentModified();
    };

    m_canvas->onWidgetResized = [this](const QString& id, float x, float y, float w, float h) {
        if (!m_doc) return;
        for (const UIWidget& wgt : m_doc->widgets()) {
            if (wgt.id != id) continue;
            UIWidget u = wgt; u.x = x; u.y = y; u.width = w; u.height = h;
            m_doc->updateWidget(u);
            rebuildPropsPanel(id);
            emit documentModified();
            break;
        }
    };

    m_canvas->onAddWidget = [this](const QString& type) { onAddWidget(type); };
    m_canvas->onDeleteSelected = [this]() { onDeleteSelected(); };
    m_canvas->onImageDropped = [this](const QString& widgetId, const QString& imagePath) {
        if (!m_doc) return;
        for (const UIWidget& wi : m_doc->widgets()) {
            if (wi.id == widgetId) {
                UIWidget u = wi; u.imagePath = imagePath;
                m_doc->updateWidget(u);
                m_canvas->update();
                if (m_selectedId == widgetId) rebuildPropsPanel(widgetId);
                emit documentModified();
                break;
            }
        }
    };

    connect(addBtn, &QPushButton::clicked, this, [this]() {
        if (!m_doc) return;
        QMenu menu(this);
        const QStringList types = {
            "UI.面板","UI.文本","UI.图片","UI.按钮","UI.进度条","UI.下拉菜单",
            "UI.竖向布局","UI.横向布局","UI.网格布局","UI.滚动视图"
        };
        for (const QString& t : types)
            menu.addAction(t, [this, t]() { onAddWidget(t); });
        menu.exec(QCursor::pos());
    });
}

void UIEditor::loadDocument(UIDocument* doc) {
    m_doc = doc;
    m_canvas->setDoc(doc);
    m_selectedId.clear();
    rebuildTree();
    rebuildPropsPanel({});
}

void UIEditor::setPreviewLevel(LevelDocument* level, float ppu) {
    m_ppu = ppu;
    m_canvas->setPreviewLevel(level, ppu);
}

void UIEditor::setProjectRoot(const QString& root) {
    m_projectRoot = root;
}

void UIEditor::setAvailableLevels(const QStringList& levelNames) {
    m_levelNames = levelNames;
    m_bgCombo->blockSignals(true);
    const QString current = m_bgCombo->currentText();
    m_bgCombo->clear();
    m_bgCombo->addItem("关闭");
    m_bgCombo->addItems(levelNames);
    int idx = m_bgCombo->findText(current);
    m_bgCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    m_bgCombo->blockSignals(false);
}

void UIEditor::rebuildTree() {
    QSignalBlocker blocker(m_tree);
    m_tree->clear();
    if (!m_doc) return;

    std::function<void(QTreeWidgetItem*, const QString&)> addChildren =
        [&](QTreeWidgetItem* parent, const QString& parentId) {
        for (const UIWidget& w : m_doc->childrenOf(parentId)) {
            auto* item = parent
                ? new QTreeWidgetItem(parent, {w.name})
                : new QTreeWidgetItem(m_tree, {w.name});
            item->setData(0, Qt::UserRole, w.id);
            item->setFlags(item->flags() | Qt::ItemIsEditable);
            addChildren(item, w.id);
        }
    };

    for (const UIWidget& w : m_doc->rootWidgets()) {
        auto* item = new QTreeWidgetItem(m_tree, {w.name});
        item->setData(0, Qt::UserRole, w.id);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        addChildren(item, w.id);
    }
    m_tree->expandAll();
}

void UIEditor::onTreeSelectionChanged() {
    auto* item = m_tree->currentItem();
    if (!item) return;
    const QString id = item->data(0, Qt::UserRole).toString();
    m_selectedId = id;
    m_canvas->setSelectedId(id);
    rebuildPropsPanel(id);
}

void UIEditor::onAddWidget(const QString& type) {
    if (!m_doc) return;
    UIWidget w;
    w.id       = QUuid::createUuid().toString(QUuid::WithoutBraces);
    w.name     = type.split('.').last();
    w.type     = type;
    w.parentId = m_selectedId;
    w.x = 10; w.y = 10;
    w.width  = (type == "UI.进度条") ? 120 : (type.contains("布局") ? 200 : 100);
    w.height = (type == "UI.进度条") ? 14  : (type.contains("布局") ? 150 : 30);
    w.bgColor = (type == "UI.面板") ? QColor(30, 30, 50, 200) : QColor(0, 0, 0, 0);
    w.text    = (type == "UI.文本") ? "文本" : (type == "UI.按钮") ? "按钮" : "";
    m_doc->addWidget(w);
    rebuildTree();
    m_selectedId = w.id;
    m_canvas->setSelectedId(w.id);
    rebuildPropsPanel(w.id);
    emit documentModified();
}

void UIEditor::onDeleteSelected() {
    if (!m_doc || m_selectedId.isEmpty()) return;
    m_doc->removeWidget(m_selectedId);
    m_selectedId.clear();
    m_canvas->setSelectedId({});
    rebuildTree();
    rebuildPropsPanel({});
    emit documentModified();
}

bool UIEditor::eventFilter(QObject* obj, QEvent* e) {
    if (obj == m_tree && e->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(e);
        if (ke->key() == Qt::Key_Delete || ke->key() == Qt::Key_Backspace) {
            onDeleteSelected();
            return true;
        }
    }
    return QWidget::eventFilter(obj, e);
}

void UIEditor::rebuildPropsPanel(const QString& widgetId) {
    auto* lay = qobject_cast<QVBoxLayout*>(m_props->layout());
    if (!lay) return;
    while (lay->count() > 0) {
        auto* item = lay->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    if (!m_doc || widgetId.isEmpty()) { lay->addStretch(); return; }

    const UIWidget* wPtr = nullptr;
    for (const UIWidget& w : m_doc->widgets())
        if (w.id == widgetId) { wPtr = &w; break; }
    if (!wPtr) { lay->addStretch(); return; }
    const UIWidget w = *wPtr;

    auto addRow = [&](const QString& label, QWidget* ctrl) {
        auto* row = new QWidget;
        auto* rl  = new QHBoxLayout(row);
        rl->setContentsMargins(4, 1, 4, 1);
        rl->addWidget(new QLabel(label));
        rl->addWidget(ctrl, 1);
        lay->addWidget(row);
    };

    auto* nameEdit = new QLineEdit(w.name);
    addRow("名称", nameEdit);
    connect(nameEdit, &QLineEdit::editingFinished, this, [this, widgetId, nameEdit]() {
        if (!m_doc) return;
        for (const UIWidget& wi : m_doc->widgets()) {
            if (wi.id == widgetId) {
                UIWidget u = wi; u.name = nameEdit->text();
                m_doc->updateWidget(u); rebuildTree(); emit documentModified(); break;
            }
        }
    });

    auto* anchorPicker = new AnchorPicker;
    anchorPicker->setAnchor(w.anchor);
    addRow("锚点", anchorPicker);
    connect(anchorPicker, &AnchorPicker::anchorChanged, this, [this, widgetId](const QString& v) {
        if (!m_doc) return;
        for (const UIWidget& wi : m_doc->widgets()) {
            if (wi.id == widgetId) { UIWidget u = wi; u.anchor = v; m_doc->updateWidget(u); m_canvas->update(); emit documentModified(); break; }
        }
    });

    auto makeFloat = [](float val) {
        auto* sb = new QDoubleSpinBox;
        sb->setRange(-9999, 9999); sb->setDecimals(1); sb->setValue(val);
        return sb;
    };
    auto* xSb = makeFloat(w.x);      addRow("X",    xSb);
    auto* ySb = makeFloat(w.y);      addRow("Y",    ySb);
    auto* wSb = makeFloat(w.width);  addRow("宽度", wSb);
    auto* hSb = makeFloat(w.height); addRow("高度", hSb);

    auto updateLayout = [this, widgetId, xSb, ySb, wSb, hSb]() {
        if (!m_doc) return;
        for (const UIWidget& wi : m_doc->widgets()) {
            if (wi.id == widgetId) {
                UIWidget u = wi;
                u.x = (float)xSb->value(); u.y = (float)ySb->value();
                u.width = (float)wSb->value(); u.height = (float)hSb->value();
                m_doc->updateWidget(u); m_canvas->update(); emit documentModified(); break;
            }
        }
    };
    connect(xSb, &QDoubleSpinBox::editingFinished, this, updateLayout);
    connect(ySb, &QDoubleSpinBox::editingFinished, this, updateLayout);
    connect(wSb, &QDoubleSpinBox::editingFinished, this, updateLayout);
    connect(hSb, &QDoubleSpinBox::editingFinished, this, updateLayout);

    auto* visCheck = new QCheckBox("可见");
    visCheck->setChecked(w.visible);
    lay->addWidget(visCheck);
    connect(visCheck, &QCheckBox::toggled, this, [this, widgetId](bool v) {
        if (!m_doc) return;
        for (const UIWidget& wi : m_doc->widgets()) {
            if (wi.id == widgetId) { UIWidget u = wi; u.visible = v; m_doc->updateWidget(u); m_canvas->update(); emit documentModified(); break; }
        }
    });

    // ── 样式属性 ─────────────────────────────────────────────────────────────
    auto makeColorBtn = [&](const QColor& c) -> QPushButton* {
        auto* btn = new QPushButton;
        btn->setFixedHeight(22);
        QString sty = QString("background:%1;border:1px solid #555;").arg(c.name(QColor::HexArgb));
        btn->setStyleSheet(sty);
        return btn;
    };

    auto* bgBtn = makeColorBtn(w.bgColor);
    addRow("背景色", bgBtn);
    connect(bgBtn, &QPushButton::clicked, this, [this, widgetId, bgBtn]() {
        if (!m_doc) return;
        for (const UIWidget& wi : m_doc->widgets()) {
            if (wi.id != widgetId) continue;
            QColor c = QColorDialog::getColor(wi.bgColor, this, "选择背景色",
                                               QColorDialog::ShowAlphaChannel);
            if (!c.isValid()) break;
            UIWidget u = wi; u.bgColor = c; m_doc->updateWidget(u);
            bgBtn->setStyleSheet(QString("background:%1;border:1px solid #555;").arg(c.name(QColor::HexArgb)));
            m_canvas->update(); emit documentModified(); break;
        }
    });

    if (w.type == "UI.文本" || w.type == "UI.按钮" || w.type == "UI.下拉菜单") {
        auto* fgBtn = makeColorBtn(w.color);
        addRow("文字颜色", fgBtn);
        connect(fgBtn, &QPushButton::clicked, this, [this, widgetId, fgBtn]() {
            if (!m_doc) return;
            for (const UIWidget& wi : m_doc->widgets()) {
                if (wi.id != widgetId) continue;
                QColor c = QColorDialog::getColor(wi.color, this, "选择文字颜色");
                if (!c.isValid()) break;
                UIWidget u = wi; u.color = c; m_doc->updateWidget(u);
                fgBtn->setStyleSheet(QString("background:%1;border:1px solid #555;").arg(c.name(QColor::HexArgb)));
                m_canvas->update(); emit documentModified(); break;
            }
        });

        auto* fsSb = new QSpinBox;
        fsSb->setRange(6, 120); fsSb->setValue(w.fontSize);
        addRow("字体大小", fsSb);
        connect(fsSb, &QSpinBox::editingFinished, this, [this, widgetId, fsSb]() {
            if (!m_doc) return;
            for (const UIWidget& wi : m_doc->widgets()) {
                if (wi.id == widgetId) { UIWidget u = wi; u.fontSize = fsSb->value(); m_doc->updateWidget(u); m_canvas->update(); emit documentModified(); break; }
            }
        });
    }

    {
        auto* alphaSb = new QDoubleSpinBox;
        alphaSb->setRange(0.0, 1.0); alphaSb->setSingleStep(0.05); alphaSb->setDecimals(2);
        alphaSb->setValue(w.alpha);
        addRow("透明度", alphaSb);
        connect(alphaSb, &QDoubleSpinBox::editingFinished, this, [this, widgetId, alphaSb]() {
            if (!m_doc) return;
            for (const UIWidget& wi : m_doc->widgets()) {
                if (wi.id == widgetId) { UIWidget u = wi; u.alpha = (float)alphaSb->value(); m_doc->updateWidget(u); m_canvas->update(); emit documentModified(); break; }
            }
        });
    }

    // 精灵图片（所有控件类型通用）
    {
        auto* imgEdit = new QLineEdit(w.imagePath);
        imgEdit->setPlaceholderText("拖入图片或点击浏览...");
        imgEdit->setReadOnly(true);
        auto* browseBtn = new QPushButton("浏览");
        browseBtn->setFixedWidth(44);
        auto* imgRow = new QWidget;
        auto* imgLay = new QHBoxLayout(imgRow);
        imgLay->setContentsMargins(4, 1, 4, 1);
        imgLay->addWidget(new QLabel("精灵图片"));
        imgLay->addWidget(imgEdit, 1);
        imgLay->addWidget(browseBtn);
        lay->addWidget(imgRow);
        connect(browseBtn, &QPushButton::clicked, this, [this, widgetId, imgEdit]() {
            QDialog dlg(this);
            dlg.setWindowTitle("选择精灵图片");
            dlg.resize(500, 420);
            auto* vl   = new QVBoxLayout(&dlg);
            auto* grid = new QListWidget(&dlg);
            grid->setViewMode(QListWidget::IconMode);
            grid->setIconSize({64, 56});
            grid->setGridSize({88, 88});
            grid->setResizeMode(QListWidget::Adjust);
            grid->setMovement(QListWidget::Static);
            grid->setWrapping(true);
            grid->setSpacing(4);

            if (!m_projectRoot.isEmpty()) {
                QDirIterator it(m_projectRoot,
                    {"*.png","*.jpg","*.jpeg","*.bmp","*.svg","*.webp"},
                    QDir::Files, QDirIterator::Subdirectories);
                while (it.hasNext()) {
                    const QString path = it.next();
                    const QString name = QFileInfo(path).fileName();
                    QPixmap bg(64, 56); bg.fill(QColor(35, 35, 35));
                    QPixmap src(path);
                    if (!src.isNull()) {
                        QPixmap scaled = src.scaled(60, 52, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                        QPainter p(&bg);
                        p.drawPixmap((64 - scaled.width()) / 2, (56 - scaled.height()) / 2, scaled);
                    }
                    auto* item = new QListWidgetItem(QIcon(bg), name, grid);
                    item->setData(Qt::UserRole, path);
                    item->setSizeHint({88, 88});
                    item->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
                }
            }

            auto* hl        = new QHBoxLayout;
            auto* okBtn     = new QPushButton("确定", &dlg);
            auto* cancelBtn = new QPushButton("取消", &dlg);
            hl->addStretch();
            hl->addWidget(okBtn);
            hl->addWidget(cancelBtn);
            vl->addWidget(grid);
            vl->addLayout(hl);

            connect(grid, &QListWidget::itemDoubleClicked, &dlg, &QDialog::accept);
            connect(okBtn,     &QPushButton::clicked, &dlg, &QDialog::accept);
            connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

            if (dlg.exec() != QDialog::Accepted) return;
            auto* sel = grid->currentItem();
            if (!sel) return;
            const QString path = sel->data(Qt::UserRole).toString();
            imgEdit->setText(path);
            if (!m_doc) return;
            for (const UIWidget& wi : m_doc->widgets()) {
                if (wi.id == widgetId) { UIWidget u = wi; u.imagePath = path; m_doc->updateWidget(u); m_canvas->update(); emit documentModified(); break; }
            }
        });
    }

    if (w.type == "UI.文本" || w.type == "UI.按钮") {
        auto* te = new QLineEdit(w.text);
        addRow("文本", te);
        connect(te, &QLineEdit::editingFinished, this, [this, widgetId, te]() {
            if (!m_doc) return;
            for (const UIWidget& wi : m_doc->widgets()) {
                if (wi.id == widgetId) { UIWidget u = wi; u.text = te->text(); m_doc->updateWidget(u); m_canvas->update(); emit documentModified(); break; }
            }
        });
    }
    if (w.type == "UI.进度条") {
        auto* valSb = new QDoubleSpinBox;
        valSb->setRange(0, 1); valSb->setSingleStep(0.05); valSb->setValue(w.value);
        addRow("数值", valSb);
        connect(valSb, &QDoubleSpinBox::editingFinished, this, [this, widgetId, valSb]() {
            if (!m_doc) return;
            for (const UIWidget& wi : m_doc->widgets()) {
                if (wi.id == widgetId) { UIWidget u = wi; u.value = (float)valSb->value(); m_doc->updateWidget(u); m_canvas->update(); emit documentModified(); break; }
            }
        });
        auto* fillBtn = makeColorBtn(w.fillColor);
        addRow("填充色", fillBtn);
        connect(fillBtn, &QPushButton::clicked, this, [this, widgetId, fillBtn]() {
            if (!m_doc) return;
            for (const UIWidget& wi : m_doc->widgets()) {
                if (wi.id != widgetId) continue;
                QColor c = QColorDialog::getColor(wi.fillColor, this, "选择填充色");
                if (!c.isValid()) break;
                UIWidget u = wi; u.fillColor = c; m_doc->updateWidget(u);
                fillBtn->setStyleSheet(QString("background:%1;border:1px solid #555;").arg(c.name(QColor::HexArgb)));
                m_canvas->update(); emit documentModified(); break;
            }
        });
    }
    if (w.type == "UI.网格布局") {
        auto* colSb = new QSpinBox; colSb->setRange(1, 20); colSb->setValue(w.columns);
        addRow("列数", colSb);
        connect(colSb, &QSpinBox::editingFinished, this, [this, widgetId, colSb]() {
            if (!m_doc) return;
            for (const UIWidget& wi : m_doc->widgets()) {
                if (wi.id == widgetId) { UIWidget u = wi; u.columns = colSb->value(); m_doc->updateWidget(u); m_canvas->update(); emit documentModified(); break; }
            }
        });
    }

    lay->addStretch();
    auto* delBtn = new QPushButton("删除控件");
    lay->addWidget(delBtn);
    connect(delBtn, &QPushButton::clicked, this, &UIEditor::onDeleteSelected);
}
