#include "GameViewport.h"
#include "UIRuntime.h"
#include "models/ActorTypeUtils.h"
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QFont>
#include <cmath>

static QString gvKeyToId(int k) {
    switch (k) {
        case Qt::Key_W:       return "W";
        case Qt::Key_A:       return "A";
        case Qt::Key_S:       return "S";
        case Qt::Key_D:       return "D";
        case Qt::Key_Up:      return "Up";
        case Qt::Key_Down:    return "Down";
        case Qt::Key_Left:    return "Left";
        case Qt::Key_Right:   return "Right";
        case Qt::Key_Space:   return "Space";
        case Qt::Key_Return:
        case Qt::Key_Enter:   return "Return";
        case Qt::Key_Escape:  return "Escape";
        case Qt::Key_Shift:   return "Shift";
        case Qt::Key_Control: return "Control";
        default:              return {};
    }
}

GameViewport::GameViewport(QWidget* parent) : QWidget(parent) {
    setObjectName("gameViewport");
    setFocusPolicy(Qt::StrongFocus);
}

void GameViewport::keyPressEvent(QKeyEvent* e) {
    const QString key = gvKeyToId(e->key());
    if (!key.isEmpty()) emit keyPressed(key);
}

void GameViewport::keyReleaseEvent(QKeyEvent* e) {
    const QString key = gvKeyToId(e->key());
    if (!key.isEmpty()) emit keyReleased(key);
}

void GameViewport::loadLevel(LevelDocument* doc) {
    m_doc = doc;
    m_pixmapCache.clear();
    update();
}

void GameViewport::setRuntimeActors(const QList<ActorData>& actors) {
    m_runtimeActors = actors;
    update();
}

void GameViewport::setRuntimeMode(bool on) {
    m_runtimeMode = on;
    if (!on) m_runtimeActors.clear();
    update();
}

void GameViewport::setPixelsPerUnit(float ppu) {
    m_ppu = qMax(1.0f, ppu);
    update();
}

void GameViewport::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), Qt::black);

    const QList<ActorData>* actorsList = nullptr;
    if (m_runtimeMode)
        actorsList = &m_runtimeActors;
    else if (m_doc)
        actorsList = &m_doc->sortedActors();
    else
        return;

    const ActorData* cam = nullptr;
    for (const ActorData& a : *actorsList) {
        if (a.cameraIsMain && (a.bpClass == "builtin/Camera" || a.components.contains("摄像机组件"))) {
            cam = &a;
            break;
        }
    }

    if (!cam) {
        p.setPen(QColor(180, 180, 180));
        p.setFont(QFont("PingFang SC", 14));
        p.drawText(rect(), Qt::AlignCenter, "场景中无主摄像机");
        return;
    }

    const float aspect = cam->cameraResH > 0
                         ? (float)cam->cameraResW / cam->cameraResH
                         : 1.7778f;
    const QRectF camRect = computeCameraRect(aspect);
    p.fillRect(camRect, cam->cameraBackground);
    drawScene(p, *actorsList, *cam, camRect);
    renderUI(p, camRect);
}

QRectF GameViewport::computeCameraRect(float aspect) const {
    const float w = (float)width();
    const float h = (float)height();
    float camW, camH;
    if (h > 0.0f && w / h > aspect) {
        camH = h;
        camW = h * aspect;
    } else {
        camW = w;
        camH = (aspect > 0.0f) ? w / aspect : h;
    }
    return QRectF((w - camW) / 2.0f, (h - camH) / 2.0f, camW, camH);
}

QPointF GameViewport::cameraWorldToScreen(QPointF world, const QRectF& camRect,
                                           const ActorData& cam) const {
    const float halfH  = cam.cameraSize;
    const float aspect = cam.cameraResH > 0
                         ? (float)cam.cameraResW / cam.cameraResH
                         : 1.7778f;
    const float halfW  = halfH * aspect;
    const float scaleX = (float)camRect.width()  / (halfW * 2.0f);
    const float scaleY = (float)camRect.height() / (halfH * 2.0f);
    return QPointF(
        camRect.center().x() + (world.x() - cam.x) * scaleX,
        camRect.center().y() - (world.y() - cam.y) * scaleY
    );
}

void GameViewport::drawScene(QPainter& p, const QList<ActorData>& actors,
                              const ActorData& cam, const QRectF& camRect) {
    const float halfH  = cam.cameraSize;
    const float aspect = cam.cameraResH > 0
                         ? (float)cam.cameraResW / cam.cameraResH
                         : 1.7778f;
    const float halfW  = halfH * aspect;
    const float scaleX = (float)camRect.width()  / (halfW * 2.0f);
    const float scaleY = (float)camRect.height() / (halfH * 2.0f);
    const float scale  = qMin(scaleX, scaleY);

    p.setClipRect(camRect);

    for (const ActorData& a : actors) {
        if (a.bpClass == "builtin/Camera" || a.components.contains("摄像机组件")) continue;
        if (!a.active || !a.spriteVisible) continue;

        QPointF pos = cameraWorldToScreen({a.x, a.y}, camRect, cam);

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
}

void GameViewport::setUIRuntime(UIRuntime* ui) {
    m_uiRuntime = ui;
    update();
}

QRectF GameViewport::widgetScreenRect(const UIWidget& w, const QRectF& parentRect) const {
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

void GameViewport::renderWidget(QPainter& p, const UIWidget& w,
                                 const QRectF& parentRect, const UIDocument& doc) const {
    if (!w.visible) return;
    const QRectF r = widgetScreenRect(w, parentRect);
    const QString& t = w.type;

    // 第一层：精灵图片（所有控件通用）
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

    // 第二层：控件类型专属内容
    if (t == "UI.面板") {
        if (w.bgColor.alpha() > 0) p.fillRect(r, w.bgColor);
    } else if (t == "UI.文本") {
        p.save();
        p.setPen(w.color);
        QFont f; f.setPixelSize(qMax(6, w.fontSize));
        p.setFont(f);
        p.drawText(r, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextWordWrap, w.text);
        p.restore();
    } else if (t == "UI.按钮") {
        if (w.imagePath.isEmpty())
            p.fillRect(r, w.bgColor.alpha() > 0 ? w.bgColor : QColor(60, 60, 80, 200));
        p.save();
        p.setPen(w.color);
        QFont f; f.setPixelSize(qMax(6, w.fontSize));
        p.setFont(f);
        p.drawText(r, Qt::AlignCenter, w.text);
        p.restore();
    } else if (t == "UI.进度条") {
        if (w.imagePath.isEmpty())
            p.fillRect(r, w.bgColor.alpha() > 0 ? w.bgColor : QColor(50, 50, 50, 200));
        QRectF fill = r;
        fill.setWidth(r.width() * qBound(0.0f, w.value, 1.0f));
        if (fill.width() > 0) p.fillRect(fill, w.fillColor);
    } else if (t == "UI.下拉菜单") {
        if (w.imagePath.isEmpty())
            p.fillRect(r, w.bgColor.alpha() > 0 ? w.bgColor : QColor(50, 50, 60, 200));
        const QStringList opts = w.text.split('\n', Qt::SkipEmptyParts);
        const QString display = (w.selectedIndex < opts.size())
                                ? opts[w.selectedIndex] : "(空)";
        p.save();
        p.setPen(w.color);
        QFont f; f.setPixelSize(qMax(6, w.fontSize));
        p.setFont(f);
        p.drawText(r.adjusted(6, 0, -20, 0), Qt::AlignVCenter | Qt::AlignLeft, display);
        p.drawText(r.adjusted(0, 0, -4, 0),  Qt::AlignVCenter | Qt::AlignRight, "▾");
        p.restore();
    }

    // 渲染子控件
    if (t == "UI.面板"    || t == "UI.竖向布局" || t == "UI.横向布局" ||
        t == "UI.网格布局" || t == "UI.滚动视图")
        renderChildren(p, w.id, r, w, doc);
}

void GameViewport::renderChildren(QPainter& p, const QString& parentId,
                                   const QRectF& parentRect, const UIWidget& parent,
                                   const UIDocument& doc) const {
    const QList<UIWidget> children = doc.childrenOf(parentId);

    if (parent.type == "UI.竖向布局") {
        float y = parentRect.top();
        for (const UIWidget& child : children) {
            if (!child.visible) continue;
            renderWidget(p, child, QRectF(parentRect.left(), y - child.y,
                                          parentRect.width(), child.height + child.y), doc);
            y += child.height + parent.spacing;
        }
        return;
    }
    if (parent.type == "UI.横向布局") {
        float x = parentRect.left();
        for (const UIWidget& child : children) {
            if (!child.visible) continue;
            renderWidget(p, child, QRectF(x - child.x, parentRect.top(),
                                          child.width + child.x, parentRect.height()), doc);
            x += child.width + parent.spacing;
        }
        return;
    }
    if (parent.type == "UI.网格布局") {
        int col = 0, row = 0;
        for (const UIWidget& child : children) {
            if (!child.visible) { col++; if (col >= parent.columns) { col = 0; row++; } continue; }
            const float cx = parentRect.left() + col * (parent.cellW + parent.spacing);
            const float cy = parentRect.top()  + row * (parent.cellH + parent.spacing);
            renderWidget(p, child, QRectF(cx, cy, parent.cellW, parent.cellH), doc);
            col++;
            if (col >= parent.columns) { col = 0; row++; }
        }
        return;
    }
    // 面板 / 滚动视图：子节点用自身锚点定位
    for (const UIWidget& child : children)
        renderWidget(p, child, parentRect, doc);
}

void GameViewport::renderUI(QPainter& p, const QRectF& camRect) const {
    if (!m_uiRuntime) return;
    for (const UIInstance* inst : m_uiRuntime->shownInstances()) {
        p.save();
        p.translate(inst->screenX, inst->screenY);
        const UIDocument& doc = inst->docCopy;
        for (const UIWidget& root : doc.rootWidgets())
            renderWidget(p, root, camRect, doc);
        p.restore();
    }
}

void GameViewport::mousePressEvent(QMouseEvent* e) {
    if (!m_uiRuntime || !m_runtimeMode) {
        QWidget::mousePressEvent(e);
        return;
    }
    const QList<ActorData>& actors = m_runtimeActors;
    const ActorData* cam = nullptr;
    for (const ActorData& a : actors) {
        if (a.cameraIsMain && (a.bpClass == "builtin/Camera" || a.components.contains("摄像机组件"))) {
            cam = &a;
            break;
        }
    }
    const float aspect = (cam && cam->cameraResH > 0)
                         ? (float)cam->cameraResW / cam->cameraResH : 1.7778f;
    const QRectF camRect = computeCameraRect(aspect);
    const QPointF pos = e->pos();
    for (const UIInstance* inst : m_uiRuntime->shownInstances()) {
        const UIDocument& doc = inst->docCopy;
        const QPointF localPos = pos - QPointF(inst->screenX, inst->screenY);
        for (const UIWidget& root : doc.rootWidgets()) {
            QString hitWidget;
            if (hitTestWidget(localPos, root, camRect, doc, hitWidget)) {
                m_uiRuntime->notifyButtonClicked(inst->instanceId, hitWidget);
                e->accept();
                return;
            }
        }
    }
    QWidget::mousePressEvent(e);
}

bool GameViewport::hitTestWidget(QPointF pos, const UIWidget& w, QRectF parentRect,
                                  const UIDocument& doc, QString& outWidget) const {
    if (!w.visible) return false;
    const QRectF r = widgetScreenRect(w, parentRect);
    if (w.type == "UI.按钮" && r.contains(pos)) {
        outWidget = w.name;
        return true;
    }
    const QString& t = w.type;
    if (t == "UI.面板" || t == "UI.竖向布局" || t == "UI.横向布局" ||
        t == "UI.网格布局" || t == "UI.滚动视图")
        return hitTestChildren(pos, w.id, r, w, doc, outWidget);
    return false;
}

bool GameViewport::hitTestChildren(QPointF pos, const QString& parentId, QRectF parentRect,
                                    const UIWidget& parent, const UIDocument& doc, QString& outWidget) const {
    const QList<UIWidget> children = doc.childrenOf(parentId);
    if (parent.type == "UI.竖向布局") {
        float y = parentRect.top();
        for (const UIWidget& child : children) {
            if (!child.visible) continue;
            if (hitTestWidget(pos, child,
                              QRectF(parentRect.left(), y - child.y, parentRect.width(), child.height + child.y),
                              doc, outWidget))
                return true;
            y += child.height + parent.spacing;
        }
        return false;
    }
    if (parent.type == "UI.横向布局") {
        float x = parentRect.left();
        for (const UIWidget& child : children) {
            if (!child.visible) continue;
            if (hitTestWidget(pos, child,
                              QRectF(x - child.x, parentRect.top(), child.width + child.x, parentRect.height()),
                              doc, outWidget))
                return true;
            x += child.width + parent.spacing;
        }
        return false;
    }
    if (parent.type == "UI.网格布局") {
        int col = 0, row = 0;
        for (const UIWidget& child : children) {
            if (!child.visible) { col++; if (col >= parent.columns) { col = 0; row++; } continue; }
            const float cx = parentRect.left() + col * (parent.cellW + parent.spacing);
            const float cy = parentRect.top()  + row * (parent.cellH + parent.spacing);
            if (hitTestWidget(pos, child, QRectF(cx, cy, parent.cellW, parent.cellH), doc, outWidget))
                return true;
            col++;
            if (col >= parent.columns) { col = 0; row++; }
        }
        return false;
    }
    for (const UIWidget& child : children) {
        if (hitTestWidget(pos, child, parentRect, doc, outWidget))
            return true;
    }
    return false;
}
