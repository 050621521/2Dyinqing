#include "GameViewport.h"
#include "UIRuntime.h"
#include "models/ActorTypeUtils.h"
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QWheelEvent>
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

static QString gvMouseButtonToId(Qt::MouseButton button) {
    if (button == Qt::LeftButton) return "左键";
    if (button == Qt::RightButton) return "右键";
    if (button == Qt::MiddleButton) return "中键";
    return "未知";
}

static Qt::MouseButton gvFirstPressedButton(Qt::MouseButtons buttons) {
    if (buttons & Qt::LeftButton) return Qt::LeftButton;
    if (buttons & Qt::RightButton) return Qt::RightButton;
    if (buttons & Qt::MiddleButton) return Qt::MiddleButton;
    return Qt::NoButton;
}

GameViewport::GameViewport(QWidget* parent) : QWidget(parent) {
    setObjectName("gameViewport");
    setFocusPolicy(Qt::StrongFocus);
}

void GameViewport::keyPressEvent(QKeyEvent* e) {
    // 过滤系统按键自动重复：只认真实的物理按下/松开，否则会破坏"持续按住"集合
    if (e->isAutoRepeat()) return;
    const QString key = gvKeyToId(e->key());
    if (!key.isEmpty()) emit keyPressed(key);
}

void GameViewport::keyReleaseEvent(QKeyEvent* e) {
    if (e->isAutoRepeat()) return;
    const QString key = gvKeyToId(e->key());
    if (!key.isEmpty()) emit keyReleased(key);
}

void GameViewport::loadLevel(LevelDocument* doc) {
    m_doc = doc;
    m_pixmapCache.clear();
    m_animCache.clear();
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
        drawPrintLog(p);
        return;
    }

    const float aspect = cam->cameraResH > 0
                         ? (float)cam->cameraResW / cam->cameraResH
                         : 1.7778f;
    const QRectF camRect = computeCameraRect(aspect);
    p.fillRect(camRect, cam->cameraBackground);
    drawScene(p, *actorsList, *cam, camRect);
    renderUI(p, camRect, cam);
    drawPrintLog(p);
}

void GameViewport::syncPrintLog(const QStringList& log) {
    m_printLog = log;
    while (m_printLog.size() > 8)
        m_printLog.removeFirst();
    update();
}

void GameViewport::clearPrintLog() {
    m_printLog.clear();
    update();
}

void GameViewport::drawPrintLog(QPainter& p) const {
    if (m_printLog.isEmpty()) return;
    p.setClipping(false);

    const int lineH = 22;
    const int padX  = 10;
    const int rows  = m_printLog.size();

    QFont font("Menlo", 13);
    p.setFont(font);
    QFontMetrics fm(font);

    int maxW = 60;
    QStringList display;
    for (const QString& s : m_printLog) {
        QString d = s.isEmpty() ? "(空)" : s;
        display << d;
        maxW = qMax(maxW, fm.horizontalAdvance(d));
    }

    const int bgH = rows * lineH + 10;
    const int bgY = height() - bgH - 10;
    QRect bg(padX - 4, bgY - 4, maxW + 20, bgH);
    p.fillRect(bg, QColor(0, 0, 0, 200));

    p.setPen(QColor(80, 230, 80, 230)); // 绿色，UE4 风格
    for (int i = 0; i < rows; ++i) {
        int y = bgY + i * lineH + lineH - 4;
        p.drawText(padX + 4, y, display[i]);
    }
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

QPointF GameViewport::worldToScreen(QPointF world, const QRectF& camRect,
                                     const ActorData& cam) {
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

QPointF GameViewport::cameraWorldToScreen(QPointF world, const QRectF& camRect,
                                           const ActorData& cam) const {
    return worldToScreen(world, camRect, cam);
}

QPointF GameViewport::cameraScreenToWorld(QPointF screen, const QRectF& camRect,
                                           const ActorData& cam) const {
    const float halfH  = cam.cameraSize;
    const float aspect = cam.cameraResH > 0
                         ? (float)cam.cameraResW / cam.cameraResH
                         : 1.7778f;
    const float halfW  = halfH * aspect;
    const float scaleX = (float)camRect.width()  / (halfW * 2.0f);
    const float scaleY = (float)camRect.height() / (halfH * 2.0f);
    if (scaleX <= 0.0f || scaleY <= 0.0f) return screen;
    return QPointF(cam.x + (screen.x() - camRect.center().x()) / scaleX,
                   cam.y - (screen.y() - camRect.center().y()) / scaleY);
}

bool GameViewport::currentCameraContext(QRectF& outCamRect, ActorData& outCam) const {
    const QList<ActorData>* actorsList = nullptr;
    if (m_runtimeMode)
        actorsList = &m_runtimeActors;
    else if (m_doc)
        actorsList = &m_doc->sortedActors();
    if (!actorsList) return false;
    for (const ActorData& a : *actorsList) {
        if (a.cameraIsMain && (a.bpClass == "builtin/Camera" || a.components.contains("摄像机组件"))) {
            const float aspect = a.cameraResH > 0 ? (float)a.cameraResW / a.cameraResH : 1.7778f;
            outCamRect = computeCameraRect(aspect);
            outCam = a;
            return true;
        }
    }
    return false;
}

QPointF GameViewport::screenToWorldOrSelf(QPointF screen) const {
    QRectF camRect;
    ActorData cam;
    if (!currentCameraContext(camRect, cam) || !camRect.contains(screen)) return screen;
    return cameraScreenToWorld(screen, camRect, cam);
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

        // 解析有效贴图：运行态用动画瞬态帧；非运行态用动画器默认片段首帧；否则静态精灵图
        QString pxKey = a.spritePath;
        QRect   animSrc;
        bool    useAnimFrame = false;
        if (!a.animSheetPath.isEmpty()) {
            pxKey = a.animSheetPath;
            animSrc = a.animSrc;
            useAnimFrame = true;
        } else if (a.components.contains("动画器") && !a.animAsset.isEmpty()
                   && !a.animDefaultClip.isEmpty()) {
            if (!m_animCache.contains(a.animAsset)) {
                AnimationAsset as; as.load(a.animAsset);
                m_animCache.insert(a.animAsset, as);
            }
            const AnimationAsset& as = m_animCache[a.animAsset];
            if (const AnimClip* clip = as.findClip(a.animDefaultClip)) {
                if (!as.sheet.isEmpty()) {
                    pxKey = as.sheet;
                    animSrc = as.frameRect(*clip, 0);
                    useAnimFrame = true;
                }
            }
        }

        bool drewPixmap = false;
        if (!pxKey.isEmpty()) {
            if (!m_pixmapCache.contains(pxKey))
                m_pixmapCache[pxKey] = QPixmap(pxKey);
            const QPixmap& px = m_pixmapCache[pxKey];
            if (!px.isNull()) {
                const QSize pxDims = useAnimFrame ? animSrc.size() : px.size();
                const float szW = pxDims.width()  / m_ppu * scale * qMax(0.05f, qAbs(a.scaleX));
                const float szH = pxDims.height() / m_ppu * scale * qMax(0.05f, qAbs(a.scaleY));
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
                if (useAnimFrame)
                    p.drawPixmap(aRect.toRect(), px, animSrc);
                else if (a.drawMode == "平铺")
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

        // 游戏视图是最终画面：没精灵图片就什么都不画（不再画占位方块）

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
        t == "UI.网格布局" || t == "UI.滚动视图") {
        if (w.clipChildren || t == "UI.滚动视图") {
            p.save();
            p.setClipRect(r);
            renderChildren(p, w.id, r, w, doc);
            p.restore();
        } else {
        renderChildren(p, w.id, r, w, doc);
        }
        if (t == "UI.滚动视图") {
            p.save();
            p.setPen(Qt::NoPen);
            const QColor track(25, 34, 45, 180);
            const QColor thumb(143, 184, 200, 210);
            const QRectF hThumb = horizontalScrollThumbRect(w, r, doc);
            if (!hThumb.isEmpty()) {
                const QRectF trackRect(r.left() + 6, r.bottom() - 10, r.width() - 12, 5);
                p.setBrush(track);
                p.drawRoundedRect(trackRect, 2, 2);
                p.setBrush(thumb);
                p.drawRoundedRect(hThumb, 3, 3);
            }
            const QRectF vThumb = verticalScrollThumbRect(w, r, doc);
            if (!vThumb.isEmpty()) {
                const QRectF trackRect(r.right() - 10, r.top() + 6, 5, r.height() - 12);
                p.setBrush(track);
                p.drawRoundedRect(trackRect, 2, 2);
                p.setBrush(thumb);
                p.drawRoundedRect(vThumb, 3, 3);
            }
            p.restore();
        }
    }
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
    if (parent.type == "UI.滚动视图") {
        const QRectF scrolledRect(parentRect.left() - parent.scrollX,
                                  parentRect.top()  - parent.scrollY,
                                  parentRect.width(), parentRect.height());
        for (const UIWidget& child : children)
            renderWidget(p, child, scrolledRect, doc);
        return;
    }
    // 面板：子节点用自身锚点定位
    for (const UIWidget& child : children)
        renderWidget(p, child, parentRect, doc);
}

void GameViewport::renderUI(QPainter& p, const QRectF& camRect, const ActorData* cam) const {
    if (!m_uiRuntime) return;
    const float canonicalW = (cam && cam->cameraResW > 0) ? (float)cam->cameraResW : 1920.0f;
    const float canonicalH = (cam && cam->cameraResH > 0) ? (float)cam->cameraResH : 1080.0f;
    const float sx = camRect.width()  / canonicalW;
    const float sy = camRect.height() / canonicalH;
    const QRectF canonicalRect(0, 0, canonicalW, canonicalH);

    p.save();
    p.setClipRect(camRect);
    for (const UIInstance* inst : m_uiRuntime->shownInstances()) {
        p.save();
        if (!inst->followActorId.isEmpty() && cam) {
            const ActorData* tgt = nullptr;
            for (const ActorData& a : m_runtimeActors)
                if (a.id == inst->followActorId) { tgt = &a; break; }
            if (!tgt) { p.restore(); continue; }
            const QPointF sp = worldToScreen({tgt->x, tgt->y}, camRect, *cam);
            p.translate(sp.x(), sp.y());
            p.scale(sx, sy);
            p.translate(-canonicalW / 2.0f + inst->followOffsetX,
                        -canonicalH / 2.0f + inst->followOffsetY);
        } else {
            p.translate(camRect.left() + inst->screenX, camRect.top() + inst->screenY);
            p.scale(sx, sy);
        }
        const UIDocument& doc = inst->docCopy;
        for (const UIWidget& root : doc.rootWidgets())
            renderWidget(p, root, canonicalRect, doc);
        p.restore();
    }
    renderDragGhost(p, camRect, cam);
    p.restore();
}

void GameViewport::renderDragGhost(QPainter& p, const QRectF& camRect, const ActorData* cam) const {
    if (!m_uiRuntime || !m_uiDragActive || m_dragInstanceId.isEmpty() || m_dragVisualWidgetName.isEmpty())
        return;

    const float canonicalW = (cam && cam->cameraResW > 0) ? (float)cam->cameraResW : 1920.0f;
    const float canonicalH = (cam && cam->cameraResH > 0) ? (float)cam->cameraResH : 1080.0f;
    const float sx = camRect.width()  / canonicalW;
    const float sy = camRect.height() / canonicalH;

    for (const UIInstance* inst : m_uiRuntime->shownInstances()) {
        if (inst->instanceId != m_dragInstanceId) continue;
        const UIDocument& doc = inst->docCopy;
        const UIWidget* w = findWidgetByName(doc, m_dragVisualWidgetName);
        if (!w) return;

        const QPointF topLeft = m_dragVisualCanonical - m_dragVisualOffset;
        const QRectF ghostParent(topLeft.x() - w->x, topLeft.y() - w->y,
                                 w->width, w->height);

        p.save();
        p.translate(camRect.left() + inst->screenX, camRect.top() + inst->screenY);
        p.scale(sx, sy);
        p.translate(topLeft.x() + w->width / 2.0f, topLeft.y() + w->height / 2.0f);
        p.scale(1.08, 1.08);
        p.translate(-(topLeft.x() + w->width / 2.0f), -(topLeft.y() + w->height / 2.0f));
        p.setOpacity(0.92);
        renderWidget(p, *w, ghostParent, doc);
        p.restore();
        return;
    }
}

void GameViewport::mousePressEvent(QMouseEvent* e) {
    if (!m_uiRuntime || !m_runtimeMode) {
        const QPointF world = screenToWorldOrSelf(e->position());
        emit mousePressedInGame(e->position().x(), e->position().y(),
                                world.x(), world.y(), gvMouseButtonToId(e->button()));
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
    const float canonicalW = (cam && cam->cameraResW > 0) ? (float)cam->cameraResW : 1920.0f;
    const float canonicalH = (cam && cam->cameraResH > 0) ? (float)cam->cameraResH : 1080.0f;
    const float sx = camRect.width()  / canonicalW;
    const float sy = camRect.height() / canonicalH;
    const QRectF canonicalRect(0, 0, canonicalW, canonicalH);
    const QPointF pos = e->pos();
    for (const UIInstance* inst : m_uiRuntime->shownInstances()) {
        const UIDocument& doc = inst->docCopy;
        // 将屏幕坐标转换回规范坐标系（1920×1080）
        const QPointF localPos = toCanonicalPos(pos, camRect, sx, sy, inst);
        for (const UIWidget& root : doc.rootWidgets()) {
            QString thumbWidget;
            Qt::Orientation thumbOrientation = Qt::Horizontal;
            if (hitTestScrollThumb(localPos, root, canonicalRect, doc, thumbWidget, thumbOrientation)) {
                for (const UIWidget& w : doc.widgets()) {
                    if (w.name != thumbWidget) continue;
                    m_scrollInstanceId = inst->instanceId;
                    m_scrollWidgetName = thumbWidget;
                    m_scrollPressPos = localPos;
                    m_scrollStartX = w.scrollX;
                    m_scrollStartY = w.scrollY;
                    m_scrollThumbDragging = true;
                    m_scrollThumbOrientation = thumbOrientation;
                    e->accept();
                    return;
                }
            }
            QString scrollWidget;
            if (hitTestScrollWidget(localPos, root, canonicalRect, doc, scrollWidget)) {
                for (const UIWidget& w : doc.widgets()) {
                    if (w.name != scrollWidget) continue;
                    m_scrollInstanceId = inst->instanceId;
                    m_scrollWidgetName = scrollWidget;
                    m_scrollPressPos = pos;
                    m_scrollStartX = w.scrollX;
                    m_scrollStartY = w.scrollY;
                    m_scrollDragging = true;
                    break;
                }
            } else {
                QString anyWidget;
                if (hitTestAnyWidget(localPos, root, canonicalRect, doc, anyWidget)) {
                    const QString cardWidget = cardRootForWidget(doc, anyWidget);
                    m_dragInstanceId = inst->instanceId;
                    m_dragWidgetName = cardWidget;
                    m_dragVisualWidgetName = cardWidget.startsWith("卡_") ? cardWidget : QString();
                    m_dragPressCanonical = localPos;
                    m_dragVisualCanonical = localPos;
                    m_dragVisualOffset = QPointF();
                    QRectF dragRect;
                    if (!m_dragVisualWidgetName.isEmpty()
                        && widgetRectByName(m_dragVisualWidgetName, root, canonicalRect, doc, dragRect)) {
                        m_dragVisualOffset = localPos - dragRect.topLeft();
                    }
                    m_uiDragActive = false;
                }
            }
            QString hitWidget;
            if (hitTestWidget(localPos, root, canonicalRect, doc, hitWidget)) {
                m_uiRuntime->notifyButtonClicked(inst->instanceId, hitWidget);
                e->accept();
                return;
            }
        }
    }
    const QPointF world = screenToWorldOrSelf(e->position());
    emit mousePressedInGame(e->position().x(), e->position().y(),
                            world.x(), world.y(), gvMouseButtonToId(e->button()));
    QWidget::mousePressEvent(e);
}

void GameViewport::mouseMoveEvent(QMouseEvent* e) {
    if (!m_uiRuntime) {
        const QPointF world = screenToWorldOrSelf(e->position());
        if (e->buttons() == Qt::NoButton)
            emit mouseMovedInGame(e->position().x(), e->position().y(), world.x(), world.y());
        else
            emit mouseDraggedInGame(e->position().x(), e->position().y(), world.x(), world.y(),
                                    gvMouseButtonToId(gvFirstPressedButton(e->buttons())));
        QWidget::mouseMoveEvent(e);
        return;
    }
    if (!m_dragInstanceId.isEmpty() && !m_dragWidgetName.isEmpty()) {
        const UIInstance* dragInst = nullptr;
        for (const UIInstance* inst : m_uiRuntime->shownInstances()) {
            if (inst->instanceId == m_dragInstanceId) { dragInst = inst; break; }
        }
        if (dragInst) {
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
            const float canonicalW = (cam && cam->cameraResW > 0) ? (float)cam->cameraResW : 1920.0f;
            const float canonicalH = (cam && cam->cameraResH > 0) ? (float)cam->cameraResH : 1080.0f;
            const QPointF localPos = toCanonicalPos(e->pos(), camRect,
                                                    camRect.width() / canonicalW,
                                                    camRect.height() / canonicalH,
                                                    dragInst);
            if (!m_uiDragActive) {
                const QPointF d = localPos - m_dragPressCanonical;
                if (std::hypot(d.x(), d.y()) > 6.0) {
                    m_uiDragActive = true;
                    m_dragVisualCanonical = localPos;
                    m_uiRuntime->notifyDragStarted(m_dragInstanceId, m_dragWidgetName,
                                                   (float)localPos.x(), (float)localPos.y());
                }
            }
            if (m_uiDragActive) {
                m_dragVisualCanonical = localPos;
                m_uiRuntime->notifyDragMoved(m_dragInstanceId, m_dragWidgetName,
                                             (float)localPos.x(), (float)localPos.y());
                update();
                e->accept();
                return;
            }
        }
    }
    if (m_scrollThumbDragging && !m_scrollInstanceId.isEmpty()) {
        const UIInstance* target = nullptr;
        for (const UIInstance* inst : m_uiRuntime->shownInstances()) {
            if (inst->instanceId == m_scrollInstanceId) { target = inst; break; }
        }
        if (!target) {
            m_scrollThumbDragging = false;
            QWidget::mouseMoveEvent(e);
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
        const float canonicalW = (cam && cam->cameraResW > 0) ? (float)cam->cameraResW : 1920.0f;
        const float canonicalH = (cam && cam->cameraResH > 0) ? (float)cam->cameraResH : 1080.0f;
        const QPointF localPos = toCanonicalPos(e->position(), camRect,
                                                camRect.width() / canonicalW,
                                                camRect.height() / canonicalH,
                                                target);
        for (const UIWidget& w : target->docCopy.widgets()) {
            if (w.name != m_scrollWidgetName) continue;
            const QRectF widgetRect = widgetScreenRect(w, QRectF(0, 0, canonicalW, canonicalH));
            const float maxX = maxScrollX(w, target->docCopy);
            const float maxY = maxScrollY(w, target->docCopy);
            float nextX = w.scrollX;
            float nextY = w.scrollY;
            if (m_scrollThumbOrientation == Qt::Horizontal && maxX > 0.0f) {
                const QRectF thumb = horizontalScrollThumbRect(w, widgetRect, target->docCopy);
                const float travel = qMax(1.0f, (float)widgetRect.width() - 12.0f - (float)thumb.width());
                nextX = qBound(0.0f, m_scrollStartX + (float)(localPos.x() - m_scrollPressPos.x()) * (maxX / travel), maxX);
            } else if (m_scrollThumbOrientation == Qt::Vertical && maxY > 0.0f) {
                const QRectF thumb = verticalScrollThumbRect(w, widgetRect, target->docCopy);
                const float travel = qMax(1.0f, (float)widgetRect.height() - 12.0f - (float)thumb.height());
                nextY = qBound(0.0f, m_scrollStartY + (float)(localPos.y() - m_scrollPressPos.y()) * (maxY / travel), maxY);
            }
            m_uiRuntime->setScroll(m_scrollInstanceId, m_scrollWidgetName, nextX, nextY);
            e->accept();
            return;
        }
    }
    if (m_scrollDragging && !m_scrollInstanceId.isEmpty()) {
        const UIInstance* target = nullptr;
        for (const UIInstance* inst : m_uiRuntime->shownInstances()) {
            if (inst->instanceId == m_scrollInstanceId) { target = inst; break; }
        }
        if (!target) {
            m_scrollDragging = false;
            QWidget::mouseMoveEvent(e);
            return;
        }
        for (const UIWidget& w : target->docCopy.widgets()) {
            if (w.name != m_scrollWidgetName) continue;
            const QPointF delta = e->pos() - m_scrollPressPos;
            const float nextX = qBound(0.0f, m_scrollStartX - (float)delta.x(), maxScrollX(w, target->docCopy));
            const float nextY = qBound(0.0f, m_scrollStartY - (float)delta.y(), maxScrollY(w, target->docCopy));
            m_uiRuntime->setScroll(m_scrollInstanceId, m_scrollWidgetName, nextX, nextY);
            e->accept();
            return;
        }
    }
    const QPointF world = screenToWorldOrSelf(e->position());
    if (e->buttons() == Qt::NoButton)
        emit mouseMovedInGame(e->position().x(), e->position().y(), world.x(), world.y());
    else
        emit mouseDraggedInGame(e->position().x(), e->position().y(), world.x(), world.y(),
                                gvMouseButtonToId(gvFirstPressedButton(e->buttons())));
    QWidget::mouseMoveEvent(e);
}

void GameViewport::mouseReleaseEvent(QMouseEvent* e) {
    if (!m_dragInstanceId.isEmpty() && m_uiRuntime) {
        const UIInstance* dragInst = nullptr;
        for (const UIInstance* inst : m_uiRuntime->shownInstances()) {
            if (inst->instanceId == m_dragInstanceId) { dragInst = inst; break; }
        }
        if (dragInst && m_uiDragActive) {
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
            const float canonicalW = (cam && cam->cameraResW > 0) ? (float)cam->cameraResW : 1920.0f;
            const float canonicalH = (cam && cam->cameraResH > 0) ? (float)cam->cameraResH : 1080.0f;
            const QPointF localPos = toCanonicalPos(e->pos(), camRect,
                                                    camRect.width() / canonicalW,
                                                    camRect.height() / canonicalH,
                                                    dragInst);
            m_uiRuntime->notifyDropped(m_dragInstanceId, m_dragWidgetName,
                                       (float)localPos.x(), (float)localPos.y());
        }
        m_dragInstanceId.clear();
        m_dragWidgetName.clear();
        m_dragVisualWidgetName.clear();
        m_dragVisualOffset = QPointF();
        m_dragVisualCanonical = QPointF();
        m_uiDragActive = false;
        update();
    }
    if (m_scrollDragging) {
        m_scrollDragging = false;
        m_scrollInstanceId.clear();
        m_scrollWidgetName.clear();
        e->accept();
        return;
    }
    if (m_scrollThumbDragging) {
        m_scrollThumbDragging = false;
        m_scrollInstanceId.clear();
        m_scrollWidgetName.clear();
        e->accept();
        return;
    }
    const QPointF world = screenToWorldOrSelf(e->position());
    emit mouseReleasedInGame(e->position().x(), e->position().y(),
                             world.x(), world.y(), gvMouseButtonToId(e->button()));
    QWidget::mouseReleaseEvent(e);
}

void GameViewport::wheelEvent(QWheelEvent* e) {
    if (!m_uiRuntime || !m_runtimeMode) {
        const QPointF world = screenToWorldOrSelf(e->position());
        emit mouseWheeledInGame(e->position().x(), e->position().y(), world.x(), world.y(),
                                e->angleDelta().x(), e->angleDelta().y());
        QWidget::wheelEvent(e);
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
    const float canonicalW = (cam && cam->cameraResW > 0) ? (float)cam->cameraResW : 1920.0f;
    const float canonicalH = (cam && cam->cameraResH > 0) ? (float)cam->cameraResH : 1080.0f;
    const float sx = camRect.width()  / canonicalW;
    const float sy = camRect.height() / canonicalH;
    const QRectF canonicalRect(0, 0, canonicalW, canonicalH);

    for (const UIInstance* inst : m_uiRuntime->shownInstances()) {
        const UIDocument& doc = inst->docCopy;
        const QPointF localPos = toCanonicalPos(e->position(), camRect, sx, sy, inst);
        for (const UIWidget& root : doc.rootWidgets()) {
            QString scrollWidget;
            if (!hitTestScrollWidget(localPos, root, canonicalRect, doc, scrollWidget)) continue;
            for (const UIWidget& w : doc.widgets()) {
                if (w.name != scrollWidget) continue;
                const QPoint num = e->angleDelta();
                const bool horizontal = (e->modifiers() & Qt::ShiftModifier) || std::abs(num.x()) > std::abs(num.y());
                const float step = horizontal ? -num.x() : -num.y();
                const float nextX = horizontal ? qBound(0.0f, w.scrollX + step, maxScrollX(w, doc)) : w.scrollX;
                const float nextY = horizontal ? w.scrollY : qBound(0.0f, w.scrollY + step, maxScrollY(w, doc));
                m_uiRuntime->setScroll(inst->instanceId, scrollWidget, nextX, nextY);
                e->accept();
                return;
            }
        }
    }
    const QPointF world = screenToWorldOrSelf(e->position());
    emit mouseWheeledInGame(e->position().x(), e->position().y(), world.x(), world.y(),
                            e->angleDelta().x(), e->angleDelta().y());
    QWidget::wheelEvent(e);
}

bool GameViewport::hitTestWidget(QPointF pos, const UIWidget& w, QRectF parentRect,
                                  const UIDocument& doc, QString& outWidget) const {
    if (!w.visible) return false;
    const QRectF r = widgetScreenRect(w, parentRect);
    if ((w.clipChildren || w.type == "UI.滚动视图") && !r.contains(pos))
        return false;
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
    if (parent.type == "UI.滚动视图") {
        const QRectF scrolledRect(parentRect.left() - parent.scrollX,
                                  parentRect.top()  - parent.scrollY,
                                  parentRect.width(), parentRect.height());
        for (const UIWidget& child : children) {
            if (hitTestWidget(pos, child, scrolledRect, doc, outWidget))
                return true;
        }
        return false;
    }
    for (const UIWidget& child : children) {
        if (hitTestWidget(pos, child, parentRect, doc, outWidget))
            return true;
    }
    return false;
}

QRectF GameViewport::childrenBounds(const QString& parentId, const UIDocument& doc) const {
    QRectF bounds;
    bool any = false;
    for (const UIWidget& child : doc.childrenOf(parentId)) {
        if (!child.visible) continue;
        QRectF r(child.x, child.y, child.width, child.height);
        bounds = any ? bounds.united(r) : r;
        any = true;
    }
    return any ? bounds : QRectF();
}

float GameViewport::maxScrollX(const UIWidget& w, const UIDocument& doc) const {
    const QRectF b = childrenBounds(w.id, doc);
    const float contentW = qMax(w.contentWidth, (float)b.right());
    return qMax(0.0f, contentW - w.width);
}

float GameViewport::maxScrollY(const UIWidget& w, const UIDocument& doc) const {
    const QRectF b = childrenBounds(w.id, doc);
    const float contentH = qMax(w.contentHeight, (float)b.bottom());
    return qMax(0.0f, contentH - w.height);
}

QRectF GameViewport::horizontalScrollThumbRect(const UIWidget& w, const QRectF& r,
                                               const UIDocument& doc) const {
    const float maxX = maxScrollX(w, doc);
    if (w.type != "UI.滚动视图" || maxX <= 0.0f || r.width() <= 24.0f) return {};
    const QRectF b = childrenBounds(w.id, doc);
    const float contentW = qMax(w.contentWidth, (float)b.right());
    if (contentW <= 0.0f) return {};
    const float trackLeft = r.left() + 6.0f;
    const float trackWidth = qMax(1.0f, (float)r.width() - 12.0f);
    const float thumbWidth = qBound(28.0f, trackWidth * ((float)w.width / contentW), trackWidth);
    const float travel = qMax(0.0f, trackWidth - thumbWidth);
    const float ratio = maxX > 0.0f ? qBound(0.0f, w.scrollX / maxX, 1.0f) : 0.0f;
    return QRectF(trackLeft + travel * ratio, r.bottom() - 12.0f, thumbWidth, 8.0f);
}

QRectF GameViewport::verticalScrollThumbRect(const UIWidget& w, const QRectF& r,
                                             const UIDocument& doc) const {
    const float maxY = maxScrollY(w, doc);
    if (w.type != "UI.滚动视图" || maxY <= 0.0f || r.height() <= 24.0f) return {};
    const QRectF b = childrenBounds(w.id, doc);
    const float contentH = qMax(w.contentHeight, (float)b.bottom());
    if (contentH <= 0.0f) return {};
    const float trackTop = r.top() + 6.0f;
    const float trackHeight = qMax(1.0f, (float)r.height() - 12.0f);
    const float thumbHeight = qBound(28.0f, trackHeight * ((float)w.height / contentH), trackHeight);
    const float travel = qMax(0.0f, trackHeight - thumbHeight);
    const float ratio = maxY > 0.0f ? qBound(0.0f, w.scrollY / maxY, 1.0f) : 0.0f;
    return QRectF(r.right() - 12.0f, trackTop + travel * ratio, 8.0f, thumbHeight);
}

bool GameViewport::hitTestScrollThumb(QPointF pos, const UIWidget& w, QRectF parentRect,
                                      const UIDocument& doc, QString& outWidget,
                                      Qt::Orientation& outOrientation) const {
    if (!w.visible) return false;
    const QRectF r = widgetScreenRect(w, parentRect);
    const QString& t = w.type;
    if (t == "UI.面板" || t == "UI.竖向布局" || t == "UI.横向布局" ||
        t == "UI.网格布局" || t == "UI.滚动视图") {
        const QList<UIWidget> children = doc.childrenOf(w.id);
        QRectF childRect = r;
        if (w.type == "UI.滚动视图")
            childRect = QRectF(r.left() - w.scrollX, r.top() - w.scrollY, r.width(), r.height());
        for (const UIWidget& child : children) {
            if (hitTestScrollThumb(pos, child, childRect, doc, outWidget, outOrientation))
                return true;
        }
    }
    if (w.type != "UI.滚动视图") return false;
    const QRectF hThumb = horizontalScrollThumbRect(w, r, doc);
    if (!hThumb.isEmpty() && hThumb.adjusted(-2, -3, 2, 3).contains(pos)) {
        outWidget = w.name;
        outOrientation = Qt::Horizontal;
        return true;
    }
    const QRectF vThumb = verticalScrollThumbRect(w, r, doc);
    if (!vThumb.isEmpty() && vThumb.adjusted(-3, -2, 3, 2).contains(pos)) {
        outWidget = w.name;
        outOrientation = Qt::Vertical;
        return true;
    }
    return false;
}

bool GameViewport::hitTestScrollWidget(QPointF pos, const UIWidget& w, QRectF parentRect,
                                       const UIDocument& doc, QString& outWidget) const {
    if (!w.visible) return false;
    const QRectF r = widgetScreenRect(w, parentRect);
    const QString& t = w.type;
    if (t == "UI.面板" || t == "UI.竖向布局" || t == "UI.横向布局" ||
        t == "UI.网格布局" || t == "UI.滚动视图") {
        const QList<UIWidget> children = doc.childrenOf(w.id);
        QRectF childRect = r;
        if (w.type == "UI.滚动视图")
            childRect = QRectF(r.left() - w.scrollX, r.top() - w.scrollY, r.width(), r.height());
        for (const UIWidget& child : children) {
            if (hitTestScrollWidget(pos, child, childRect, doc, outWidget))
                return true;
        }
        if (w.type == "UI.滚动视图") {
            QString childHit;
            for (const UIWidget& child : children) {
                if (hitTestAnyWidget(pos, child, childRect, doc, childHit))
                    return false;
            }
        }
    }
    if (w.type == "UI.滚动视图" && r.contains(pos)) {
        outWidget = w.name;
        return true;
    }
    return false;
}

bool GameViewport::hitTestAnyWidget(QPointF pos, const UIWidget& w, QRectF parentRect,
                                    const UIDocument& doc, QString& outWidget) const {
    if (!w.visible) return false;
    const QRectF r = widgetScreenRect(w, parentRect);
    if ((w.clipChildren || w.type == "UI.滚动视图") && !r.contains(pos))
        return false;
    const QString& t = w.type;
    if (t == "UI.面板" || t == "UI.竖向布局" || t == "UI.横向布局" ||
        t == "UI.网格布局" || t == "UI.滚动视图") {
        const QList<UIWidget> children = doc.childrenOf(w.id);
        QRectF childRect = r;
        if (w.type == "UI.滚动视图")
            childRect = QRectF(r.left() - w.scrollX, r.top() - w.scrollY, r.width(), r.height());
        for (auto it = children.crbegin(); it != children.crend(); ++it) {
            if (hitTestAnyWidget(pos, *it, childRect, doc, outWidget))
                return true;
        }
    }
    if (r.contains(pos)) {
        outWidget = w.name;
        return true;
    }
    return false;
}

const UIWidget* GameViewport::findWidgetByName(const UIDocument& doc, const QString& name) const {
    for (const UIWidget& w : doc.widgets())
        if (w.name == name) return &w;
    return nullptr;
}

const UIWidget* GameViewport::findWidgetById(const UIDocument& doc, const QString& id) const {
    for (const UIWidget& w : doc.widgets())
        if (w.id == id) return &w;
    return nullptr;
}

QString GameViewport::cardRootForWidget(const UIDocument& doc, const QString& widgetName) const {
    const UIWidget* w = findWidgetByName(doc, widgetName);
    for (int guard = 0; w && guard < 64; ++guard) {
        if (w->name.startsWith("卡_")) return w->name;
        if (w->parentId.isEmpty()) break;
        w = findWidgetById(doc, w->parentId);
    }
    return widgetName;
}

bool GameViewport::widgetRectByName(const QString& widgetName, const UIWidget& w,
                                    QRectF parentRect, const UIDocument& doc, QRectF& outRect) const {
    if (!w.visible) return false;
    const QRectF r = widgetScreenRect(w, parentRect);
    if (w.name == widgetName) {
        outRect = r;
        return true;
    }

    const QList<UIWidget> children = doc.childrenOf(w.id);
    if (children.isEmpty()) return false;

    if (w.type == "UI.竖向布局") {
        float y = r.top();
        for (const UIWidget& child : children) {
            if (!child.visible) continue;
            if (widgetRectByName(widgetName, child,
                                 QRectF(r.left(), y - child.y, r.width(), child.height + child.y),
                                 doc, outRect))
                return true;
            y += child.height + w.spacing;
        }
        return false;
    }
    if (w.type == "UI.横向布局") {
        float x = r.left();
        for (const UIWidget& child : children) {
            if (!child.visible) continue;
            if (widgetRectByName(widgetName, child,
                                 QRectF(x - child.x, r.top(), child.width + child.x, r.height()),
                                 doc, outRect))
                return true;
            x += child.width + w.spacing;
        }
        return false;
    }
    if (w.type == "UI.网格布局") {
        int col = 0, row = 0;
        for (const UIWidget& child : children) {
            if (!child.visible) {
                col++;
                if (col >= w.columns) { col = 0; row++; }
                continue;
            }
            const float cx = r.left() + col * (w.cellW + w.spacing);
            const float cy = r.top()  + row * (w.cellH + w.spacing);
            if (widgetRectByName(widgetName, child, QRectF(cx, cy, w.cellW, w.cellH), doc, outRect))
                return true;
            col++;
            if (col >= w.columns) { col = 0; row++; }
        }
        return false;
    }

    QRectF childRect = r;
    if (w.type == "UI.滚动视图")
        childRect = QRectF(r.left() - w.scrollX, r.top() - w.scrollY, r.width(), r.height());
    for (const UIWidget& child : children) {
        if (widgetRectByName(widgetName, child, childRect, doc, outRect))
            return true;
    }
    return false;
}

QPointF GameViewport::toCanonicalPos(const QPointF& pos, const QRectF& camRect,
                                     float sx, float sy, const UIInstance* inst) const {
    return QPointF((pos.x() - camRect.left() - inst->screenX) / sx,
                   (pos.y() - camRect.top()  - inst->screenY) / sy);
}
