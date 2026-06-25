#include "Viewport2D.h"
#include "UndoCommands.h"
#include "models/ActorTypeUtils.h"
#include <QPainter>
#include <QPaintEvent>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QFont>
#include <QUuid>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QDataStream>
#include <QClipboard>
#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <cmath>


Viewport2D::Viewport2D(QWidget* parent) : QWidget(parent) {
    setObjectName("viewport2D");
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(true);
}

// 内容浏览器把 .bp 类拖进视口 → 落点生成实例（虚幻主力摆放方式）
void Viewport2D::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasFormat("application/x-qabstractitemmodeldatalist"))
        e->acceptProposedAction();
}

void Viewport2D::dropEvent(QDropEvent* e) {
    const QByteArray encoded =
        e->mimeData()->data("application/x-qabstractitemmodeldatalist");
    QDataStream stream(encoded);
    while (!stream.atEnd()) {
        int row, col;
        QMap<int, QVariant> d;
        stream >> row >> col >> d;
        if (d.value(Qt::UserRole).toString() == "bp") {
            const QString bpPath = d.value(Qt::UserRole + 1).toString();
            const QPointF worldPos = screenToWorld(e->position().toPoint());
            emit bpClassDropped(bpPath, worldPos);
            e->acceptProposedAction();
            return;
        }
    }
}

// ── 外部接口 ──────────────────────────────────────────────────────────

void Viewport2D::loadLevel(LevelDocument* doc) {
    m_doc = doc;
    m_selectedId.clear();
    m_pixmapCache.clear();
    m_animCache.clear();
    update();
}

void Viewport2D::setSelectedId(const QString& id) {
    m_selectedId = id;
    m_selectedIds.clear();
    if (!id.isEmpty()) m_selectedIds.insert(id);
    update();
}

// 设置完整多选选区（供大纲面板多选同步），并广播 selectionChanged
void Viewport2D::setSelectedIds(const QStringList& ids) {
    m_selectedIds.clear();
    for (const QString& id : ids)
        if (!id.isEmpty()) m_selectedIds.insert(id);
    m_selectedId = m_selectedIds.isEmpty() ? QString() : ids.first();
    emit selectionChanged(m_selectedIds.values());
    update();
}

void Viewport2D::setGridSnap(bool enabled, float size) {
    m_gridSnapEnabled = enabled;
    m_gridSnapSize    = qMax(1.0f, size);
}

void Viewport2D::setRotSnap(bool enabled, float angle) {
    m_rotSnapEnabled = enabled;
    m_rotSnapAngle   = qMax(1.0f, angle);
}

void Viewport2D::alignSelected(int type) {
    if (!m_doc || m_selectedIds.size() < 2) return;
    QList<ActorData> sel;
    for (const ActorData& a : m_doc->actors())
        if (m_selectedIds.contains(a.id)) sel << a;
    if (sel.isEmpty()) return;

    float minX = sel[0].x, maxX = sel[0].x;
    float minY = sel[0].y, maxY = sel[0].y;
    for (const ActorData& a : sel) {
        minX = qMin(minX, a.x); maxX = qMax(maxX, a.x);
        minY = qMin(minY, a.y); maxY = qMax(maxY, a.y);
    }

    QList<ActorData> updated;
    for (ActorData a : sel) {
        switch (type) {
            case 0: a.x = minX; break;
            case 1: a.x = maxX; break;
            case 2: a.x = (minX + maxX) * 0.5f; break;
            case 3: a.y = minY; break;
            case 4: a.y = maxY; break;
            case 5: a.y = (minY + maxY) * 0.5f; break;
        }
        m_doc->updateActor(a);
        updated << a;
    }
    update();
    emit actorsAligned(updated);
}

void Viewport2D::setToolMode(ToolMode mode) {
    m_toolMode = mode;
    applyToolCursor();
    update();
    emit toolModeChanged(mode);
}

void Viewport2D::setUndoStack(QUndoStack* stack, std::function<void()> refresh) {
    m_undoStack = stack;
    m_onRefresh = std::move(refresh);
}

void Viewport2D::frameSelected() {
    if (!m_doc) return;
    if (m_selectedId.isEmpty()) {
        m_offset = {0, 0};
    } else {
        for (const ActorData& a : m_doc->actors()) {
            if (a.id != m_selectedId) continue;
            m_offset = QPointF(-a.x * m_zoom, -a.y * m_zoom);
            break;
        }
    }
    update();
}

void Viewport2D::selectAll() {
    if (!m_doc) return;
    m_selectedIds.clear();
    for (const ActorData& a : m_doc->actors())
        m_selectedIds.insert(a.id);
    m_selectedId = m_selectedIds.isEmpty() ? QString() : *m_selectedIds.begin();
    emit selectionChanged(m_selectedIds.values());
    update();
}

void Viewport2D::clearSelection() {
    m_selectedId.clear();
    m_selectedIds.clear();
    emit selectionChanged({});
    update();
}

void Viewport2D::duplicateSelected() {
    if (!m_doc || m_selectedIds.isEmpty() || !m_undoStack || !m_onRefresh) return;
    QList<ActorData> copies;
    for (const ActorData& a : m_doc->actors()) {
        if (!m_selectedIds.contains(a.id)) continue;
        ActorData copy = a;
        copy.id   = QUuid::createUuid().toString(QUuid::WithoutBraces);
        copy.name = a.name + " 副本";
        copy.x   += 50.0f;
        copy.y   += 50.0f;
        copies << copy;
    }
    if (copies.isEmpty()) return;
    m_undoStack->beginMacro("复制 Actor");
    for (const ActorData& c : copies)
        m_undoStack->push(new ActorAddCmd(m_doc, c, m_onRefresh));
    m_undoStack->endMacro();
    m_selectedIds.clear();
    for (const ActorData& c : copies) {
        m_selectedIds.insert(c.id);
        emit actorCreated(c);
    }
    m_selectedId = copies.first().id;
    emit selectionChanged(m_selectedIds.values());
    update();
}

// 复制：把选中的 Actor 序列化写入系统剪贴板，可跨关卡粘贴
void Viewport2D::copySelected() {
    if (!m_doc || m_selectedIds.isEmpty()) return;
    QJsonArray arr;
    for (const ActorData& a : m_doc->actors()) {
        if (!m_selectedIds.contains(a.id)) continue;
        arr.append(a.toJson());
    }
    if (arr.isEmpty()) return;
    QJsonObject root;
    root["__yinqing_actors__"] = arr;
    QApplication::clipboard()->setText(
        QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
}

// 粘贴：从系统剪贴板读取 Actor 并加入当前关卡（生成新 id，整体偏移避免重叠）
void Viewport2D::pasteFromClipboard() {
    if (!m_doc || !m_undoStack || !m_onRefresh) return;
    const QByteArray text = QApplication::clipboard()->text().toUtf8();
    if (text.isEmpty()) return;
    QJsonParseError err;
    const QJsonDocument jdoc = QJsonDocument::fromJson(text, &err);
    if (err.error != QJsonParseError::NoError || !jdoc.isObject()) return;
    const QJsonArray arr = jdoc.object().value("__yinqing_actors__").toArray();
    if (arr.isEmpty()) return;

    QList<ActorData> pasted;
    for (const QJsonValue& v : arr) {
        ActorData a = ActorData::fromJson(v.toObject());
        a.id  = QUuid::createUuid().toString(QUuid::WithoutBraces);
        a.x  += 50.0f;
        a.y  += 50.0f;
        pasted << a;
    }
    if (pasted.isEmpty()) return;

    m_undoStack->beginMacro("粘贴 Actor");
    for (const ActorData& c : pasted)
        m_undoStack->push(new ActorAddCmd(m_doc, c, m_onRefresh));
    m_undoStack->endMacro();

    m_selectedIds.clear();
    for (const ActorData& c : pasted) {
        m_selectedIds.insert(c.id);
        emit actorCreated(c);
    }
    m_selectedId = pasted.first().id;
    emit selectionChanged(m_selectedIds.values());
    update();
}

void Viewport2D::deleteSelected() {
    if (m_selectedIds.isEmpty() || !m_doc) return;
    if (m_undoStack && m_onRefresh) {
        m_undoStack->beginMacro("删除 Actor");
        for (const QString& id : m_selectedIds) {
            for (const ActorData& a : m_doc->actors()) {
                if (a.id == id) {
                    m_undoStack->push(new ActorRemoveCmd(m_doc, a, m_onRefresh));
                    break;
                }
            }
        }
        m_undoStack->endMacro();
    } else {
        for (const QString& id : m_selectedIds)
            m_doc->removeActor(id);
        if (m_onRefresh) m_onRefresh();
    }
    for (const QString& id : m_selectedIds) emit actorRemoved(id);
    m_selectedId.clear();
    m_selectedIds.clear();
    emit selectionChanged({});
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
        m_selectedIds.clear();
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
    // Y 取反：世界 Y 向上为正，屏幕 Y 向下为正（与游戏视图、关卡数据一致）
    return QPointF(center.x() + world.x() * m_zoom,
                   center.y() - world.y() * m_zoom);
}

QPointF Viewport2D::screenToWorld(QPointF screen) const {
    QPointF center(width() / 2.0 + m_offset.x(), height() / 2.0 + m_offset.y());
    return QPointF((screen.x() - center.x()) / m_zoom,
                   (center.y() - screen.y()) / m_zoom);
}

// ── 绘制 ──────────────────────────────────────────────────────────────

// 精灵类无图片时的「无图」占位小图标（图片字形：边框+太阳+山），
// 固定屏幕尺寸、画在对象中心，方便开发时定位。
static void drawNoSpriteIcon(QPainter& p, const QPointF& c, float maxSize,
                             const QColor& col) {
    const float s = qMin(18.0f, maxSize * 0.6f);
    if (s < 5.0f) return;   // 太小就不画图标，只留细框
    const QRectF fr(c.x() - s / 2, c.y() - s / 2, s, s);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(col, 1.3f));
    p.drawRoundedRect(fr, s * 0.14, s * 0.14);
    p.setPen(Qt::NoPen);
    p.setBrush(col);
    p.drawEllipse(QPointF(fr.left() + s * 0.32, fr.top() + s * 0.32), s * 0.1, s * 0.1);
    QPolygonF mt;
    mt << QPointF(fr.left() + s * 0.18, fr.bottom() - s * 0.18)
       << QPointF(fr.left() + s * 0.46, fr.top()    + s * 0.52)
       << QPointF(fr.right() - s * 0.14, fr.bottom() - s * 0.18);
    p.drawPolygon(mt);
}

void Viewport2D::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    p.fillRect(rect(), QColor(0x1a, 0x1a, 0x1a));

    drawGrid(p);
    drawAxes(p);
    drawOriginMark(p);
    drawActors(p);
    drawSelectionOverlay(p);

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

        // 解析有效贴图：运行态用动画瞬态帧；编辑态用动画器默认片段首帧；否则用静态精灵图
        QString pxKey = a.spritePath;
        QRect   animSrc;            // 非空 = 画精灵表子矩形
        bool    useAnimFrame = false;
        if (m_runtimeMode && !a.animSheetPath.isEmpty()) {
            pxKey = a.animSheetPath;
            animSrc = a.animSrc;
            useAnimFrame = true;
        } else if (!m_runtimeMode && a.components.contains("动画器")
                   && !a.animAsset.isEmpty() && !a.animDefaultClip.isEmpty()) {
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

        // 预加载贴图；精灵显示尺寸 = 像素尺寸 / m_ppu × zoom × scale
        if (!pxKey.isEmpty() && !m_pixmapCache.contains(pxKey))
            m_pixmapCache[pxKey] = QPixmap(pxKey);
        const bool hasPx = !pxKey.isEmpty() && !m_pixmapCache[pxKey].isNull();
        const QSize pxDims = !hasPx ? QSize()
            : (useAnimFrame ? animSrc.size() : m_pixmapCache[pxKey].size());

        const float szBase = qMax(24.0f, 40.0f * m_zoom);
        const float szW = hasPx
            ? pxDims.width()  / m_ppu * m_zoom * qMax(0.05f, qAbs(a.scaleX))
            : szBase * qMax(0.05f, qAbs(a.scaleX));
        const float szH = hasPx
            ? pxDims.height() / m_ppu * m_zoom * qMax(0.05f, qAbs(a.scaleY))
            : szBase * qMax(0.05f, qAbs(a.scaleY));
        const float sz  = (szW + szH) * 0.5f;
        QRectF rect(pos.x() - szW / 2, pos.y() - szH / 2, szW, szH);
        const bool isSelected = m_selectedIds.contains(a.id);
        const bool isPrimary  = (a.id == m_selectedId);

        QColor fill = bpClassColor(a.bpClass);

        QPen outline(isSelected ? QColor(255, 255, 255) : fill.darker(160),
                     isSelected ? 2.5 : 1.0);

        // 旋转变换（选中框也随之旋转）
        p.save();
        if (a.rotation != 0.0f) {
            p.translate(pos.x(), pos.y());
            p.rotate(a.rotation);
            p.translate(-pos.x(), -pos.y());
        }

        // 优先：有贴图则任何类型都渲染图片
        bool drewPixmap = false;
        if (hasPx) {
            const QPixmap& px = m_pixmapCache[pxKey];
            {
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
                    p.drawPixmap(rect.toRect(), px, animSrc);
                else if (a.drawMode == "平铺")
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
        const bool hasCamera = (a.bpClass == "builtin/Camera" || a.components.contains("摄像机组件"));
        if (hasCamera && !m_runtimeMode) {
            const float halfH = a.cameraSize * m_zoom;
            const float halfW = halfH * (a.cameraResH > 0 ? (float)a.cameraResW / a.cameraResH : 1.7778f);
            QRectF frustum(pos.x() - halfW, pos.y() - halfH, halfW * 2, halfH * 2);
            if (a.cameraIsMain)
                p.setPen(QPen(QColor(255, 255, 255, 220), 1.5f, Qt::SolidLine));
            else
                p.setPen(QPen(QColor(80, 160, 240, isSelected ? 200 : 70), 1.5f, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawRect(frustum);
        }

        // 无贴图时按类型绘制占位图形
        if (!drewPixmap) {
            if (a.bpClass == "builtin/Empty") {
                const float cs = sz * 0.28f;
                p.setPen(QPen(fill, isSelected ? 2.0f : 1.5f));
                p.setBrush(Qt::NoBrush);
                p.drawLine(QPointF(pos.x() - cs, pos.y()), QPointF(pos.x() + cs, pos.y()));
                p.drawLine(QPointF(pos.x(), pos.y() - cs), QPointF(pos.x(), pos.y() + cs));
                p.drawEllipse(pos, cs * 0.4, cs * 0.4);
            } else if (a.bpClass == "builtin/Trigger") {
                QPen dp = outline; dp.setStyle(Qt::DashLine);
                p.setPen(dp);
                p.setBrush(QColor(fill.red(), fill.green(), fill.blue(), 55));
                p.drawRect(rect);
            } else if (a.bpClass == "builtin/Light") {
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
            } else if (a.bpClass == "builtin/Camera") {
                p.setPen(outline);
                p.setBrush(fill);
                p.drawRect(rect);
                p.setBrush(Qt::white); p.setPen(Qt::NoPen);
                QPolygonF tri;
                tri << QPointF(pos.x() + sz * 0.28f, pos.y())
                    << QPointF(pos.x() + sz * 0.5f,  pos.y() - sz * 0.18f)
                    << QPointF(pos.x() + sz * 0.5f,  pos.y() + sz * 0.18f);
                p.drawPolygon(tri);
            } else {
                // 精灵类无图片：只画空心细框 + 中心「无图」小图标，便于定位
                QPen ph(fill.lighter(150), isSelected ? 2.0f : 1.2f);
                ph.setStyle(isSelected ? Qt::SolidLine : Qt::DashLine);
                p.setPen(ph);
                p.setBrush(Qt::NoBrush);
                p.drawRect(rect);
                drawNoSpriteIcon(p, pos, sz, fill.lighter(160));
            }
        }

        // 禁用状态蒙版（仅遮实心可视：贴图 / 相机 / 光源 / 触发器；空对象与无图精灵的细框占位不遮）
        const bool hasFilledVisual = drewPixmap
            || a.bpClass == "builtin/Camera" || a.bpClass == "builtin/Light"
            || a.bpClass == "builtin/Trigger";
        if (!a.active && hasFilledVisual) {
            p.setBrush(QColor(0, 0, 0, 130));
            p.setPen(Qt::NoPen);
            p.drawRect(rect);
        }

        // 选中外框（随旋转）
        if (isSelected) {
            p.setPen(QPen(QColor(255, 200, 50), 1.5, Qt::SolidLine));
            p.setBrush(Qt::NoBrush);
            if (a.bpClass == "builtin/Empty" && !drewPixmap) {
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

        // 碰撞盒绿框（AABB，屏幕空间，不随旋转）；重叠=青、阻挡=绿
        if (!m_runtimeMode && a.components.contains("碰撞盒") && a.colliderEnabled) {
            const QRectF cr = colliderScreenRect(a);
            const QColor cc = (a.colliderResponse == "重叠")
                              ? QColor(70, 200, 255) : QColor(60, 220, 90);
            p.setPen(QPen(cc, isSelected ? 2.0f : 1.3f, Qt::SolidLine));
            p.setBrush(Qt::NoBrush);
            p.drawRect(cr);
            if (isPrimary) {           // 选中主对象：画 8 个拖拽手柄
                p.setBrush(cc); p.setPen(Qt::NoPen);
                for (const QRectF& h : colliderHandleRects(cr))
                    p.drawRect(h);
            }
        }

        // Gizmo 在屏幕坐标绘制（不受旋转影响）
        if (isPrimary) drawGizmo(p, a, rect, pos);
    }
}

// 碰撞盒在屏幕上的矩形（中心 = 对象位置 + 偏移，尺寸 = 世界宽高 × zoom）
QRectF Viewport2D::colliderScreenRect(const ActorData& a) const {
    const QPointF c = worldToScreen({a.x + a.colliderOffsetX, a.y + a.colliderOffsetY});
    const float w = a.colliderW * m_zoom, h = a.colliderH * m_zoom;
    return QRectF(c.x() - w / 2.0f, c.y() - h / 2.0f, w, h);
}

// 8 个手柄矩形：4 角 + 4 边中点（顺序见 ColliderHandle 枚举）
QList<QRectF> Viewport2D::colliderHandleRects(const QRectF& cr) const {
    const float s = 4.0f;
    auto hr = [&](float cx, float cy){ return QRectF(cx - s, cy - s, s * 2, s * 2); };
    return {
        hr(cr.left(),    cr.top()),      hr(cr.right(),   cr.top()),
        hr(cr.left(),    cr.bottom()),   hr(cr.right(),   cr.bottom()),
        hr(cr.center().x(), cr.top()),   hr(cr.center().x(), cr.bottom()),
        hr(cr.left(), cr.center().y()),  hr(cr.right(), cr.center().y()),
    };
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

// ── 辅助：Actor 屏幕矩形 ──────────────────────────────────────────────

QRectF Viewport2D::actorScreenRect(const ActorData& a) const {
    QPointF pos = worldToScreen({a.x, a.y});

    // 与 drawActors 一致：动画器默认片段首帧的帧尺寸优先
    QString pxKey = a.spritePath;
    QSize   frameDims;
    bool    useAnimFrame = false;
    if (a.components.contains("动画器") && !a.animAsset.isEmpty() && !a.animDefaultClip.isEmpty()) {
        if (!m_animCache.contains(a.animAsset)) {
            AnimationAsset as; as.load(a.animAsset);
            m_animCache.insert(a.animAsset, as);
        }
        const AnimationAsset& as = m_animCache[a.animAsset];
        if (const AnimClip* clip = as.findClip(a.animDefaultClip)) {
            if (!as.sheet.isEmpty()) {
                pxKey = as.sheet;
                frameDims = as.frameRect(*clip, 0).size();
                useAnimFrame = true;
            }
        }
    }

    if (!pxKey.isEmpty() && !m_pixmapCache.contains(pxKey))
        m_pixmapCache[pxKey] = QPixmap(pxKey);
    const bool hasPx = !pxKey.isEmpty() && !m_pixmapCache[pxKey].isNull();
    const QSize pxDims = !hasPx ? QSize()
        : (useAnimFrame ? frameDims : m_pixmapCache[pxKey].size());
    const float szBase = qMax(24.0f, 40.0f * m_zoom);
    const float szW = hasPx
        ? pxDims.width()  / m_ppu * m_zoom * qMax(0.05f, qAbs(a.scaleX))
        : szBase * qMax(0.05f, qAbs(a.scaleX));
    const float szH = hasPx
        ? pxDims.height() / m_ppu * m_zoom * qMax(0.05f, qAbs(a.scaleY))
        : szBase * qMax(0.05f, qAbs(a.scaleY));
    return QRectF(pos.x() - szW / 2, pos.y() - szH / 2, szW, szH);
}

// ── 多选 / 框选 覆盖层 ───────────────────────────────────────────────

void Viewport2D::drawSelectionOverlay(QPainter& p) {
    // 框选矩形（蓝色半透明）
    if (m_rubberBanding && !m_rubberRect.isNull()) {
        p.setPen(QPen(QColor(100, 150, 255, 200), 1, Qt::DashLine));
        p.setBrush(QColor(100, 150, 255, 30));
        p.drawRect(m_rubberRect);
    }

    // 多选整体 bounding box（白色虚线）
    if (m_selectedIds.size() >= 2 && m_doc) {
        bool first = true;
        float minX = 0, minY = 0, maxX = 0, maxY = 0;
        for (const ActorData& a : m_doc->actors()) {
            if (!m_selectedIds.contains(a.id)) continue;
            QRectF r = actorScreenRect(a);
            if (first) {
                minX = (float)r.left(); minY = (float)r.top();
                maxX = (float)r.right(); maxY = (float)r.bottom();
                first = false;
            } else {
                minX = qMin(minX, (float)r.left()); minY = qMin(minY, (float)r.top());
                maxX = qMax(maxX, (float)r.right()); maxY = qMax(maxY, (float)r.bottom());
            }
        }
        if (!first) {
            p.setPen(QPen(Qt::white, 1, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawRect(QRectF(minX - 8, minY - 8, maxX - minX + 16, maxY - minY + 16));
        }
    }
}

// ── 交互 ──────────────────────────────────────────────────────────────

static QString qtKeyToId(int k) {
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

void Viewport2D::keyPressEvent(QKeyEvent* e) {
    if (m_runtimeMode) {
        const QString key = qtKeyToId(e->key());
        if (!key.isEmpty())
            emit keyPressed(key);
        return;
    }
    QWidget::keyPressEvent(e);
}

void Viewport2D::keyReleaseEvent(QKeyEvent* e) {
    if (m_runtimeMode) {
        const QString key = qtKeyToId(e->key());
        if (!key.isEmpty())
            emit keyReleased(key);
        return;
    }
    QWidget::keyReleaseEvent(e);
}

void Viewport2D::wheelEvent(QWheelEvent* e) {
    float factor = e->angleDelta().y() > 0 ? 1.15f : (1.0f / 1.15f);
    m_zoom = qBound(0.05f, m_zoom * factor, 50.0f);
    update();
}

void Viewport2D::mousePressEvent(QMouseEvent* e) {
    if (m_runtimeMode) return;
    const bool ctrl = (e->modifiers() & Qt::ControlModifier) != 0;

    if (e->button() == Qt::LeftButton) {
        if (m_doc) {
            // 碰撞盒手柄拖拽（最高优先）：主选对象启用了碰撞盒时
            if (!m_selectedId.isEmpty() && !ctrl) {
                for (const ActorData& a : m_doc->actors()) {
                    if (a.id != m_selectedId) continue;
                    if (a.colliderEnabled && a.components.contains("碰撞盒")) {
                        const QList<QRectF> hr = colliderHandleRects(colliderScreenRect(a));
                        static const ColliderHandle order[8] = {
                            ColliderHandle::TL, ColliderHandle::TR, ColliderHandle::BL, ColliderHandle::BR,
                            ColliderHandle::Top, ColliderHandle::Bottom, ColliderHandle::Left, ColliderHandle::Right };
                        ColliderHandle hit = ColliderHandle::None;
                        for (int i = 0; i < 8; ++i)
                            if (hr[i].adjusted(-3, -3, 3, 3).contains(e->pos())) { hit = order[i]; break; }
                        if (hit != ColliderHandle::None) {
                            m_colliderHandle = hit;
                            const float cx = a.x + a.colliderOffsetX, cy = a.y + a.colliderOffsetY;
                            m_cbStartL = cx - a.colliderW / 2; m_cbStartR = cx + a.colliderW / 2;
                            m_cbStartB = cy - a.colliderH / 2; m_cbStartT = cy + a.colliderH / 2;
                            m_dragBeforeActors.clear();
                            m_dragBeforeActors << a;
                            m_dragging    = true;
                            m_dragActorId = a.id;
                            update();
                            return;
                        }
                    }
                    break;
                }
            }

            // Move 模式：先检测主选 Actor 的箭头端点（Gizmo 轴拖拽）
            if (m_toolMode == ToolMode::Move && !m_selectedId.isEmpty() && !ctrl) {
                for (const ActorData& a : m_doc->actors()) {
                    if (a.id != m_selectedId) continue;
                    QPointF pos = worldToScreen({a.x, a.y});
                    const float len = 60.0f, hs = 12.0f;
                    QRectF xHandle(pos.x() + len - hs + 5, pos.y() - hs, hs * 2, hs * 2);
                    QRectF yHandle(pos.x() - hs, pos.y() - len - hs - 5, hs * 2, hs * 2);
                    ScaleHandle hit = ScaleHandle::None;
                    if      (xHandle.contains(e->pos())) hit = ScaleHandle::AxisX;
                    else if (yHandle.contains(e->pos())) hit = ScaleHandle::AxisY;
                    if (hit != ScaleHandle::None) {
                        m_scaleHandle = hit;
                        m_dragBeforeActors.clear();
                        for (const ActorData& b : m_doc->actors())
                            if (m_selectedIds.contains(b.id))
                                m_dragBeforeActors << b;
                        m_dragging    = true;
                        m_dragActorId = a.id;
                        m_dragMouseStartWorld = screenToWorld(e->pos());
                        m_dragStartPositions.clear();
                        for (const ActorData& b : m_doc->actors())
                            if (m_selectedIds.contains(b.id))
                                m_dragStartPositions[b.id] = {b.x, b.y};
                        update();
                        return;
                    }
                    break;
                }
            }

            // Scale 模式：先检测主选 Actor 的 Gizmo 端点
            if (m_toolMode == ToolMode::Scale && !m_selectedId.isEmpty() && !ctrl) {
                for (const ActorData& a : m_doc->actors()) {
                    if (a.id != m_selectedId) continue;
                    QPointF pos = worldToScreen({a.x, a.y});
                    const float len = 60.0f, hs = 10.0f;
                    QRectF xHandle(pos.x() + len - hs, pos.y() - hs,       hs * 2, hs * 2);
                    QRectF yHandle(pos.x() - hs,       pos.y() - len - hs, hs * 2, hs * 2);
                    QRectF cHandle(pos.x() - hs,       pos.y() - hs,       hs * 2, hs * 2);
                    ScaleHandle hit = ScaleHandle::None;
                    if      (xHandle.contains(e->pos())) hit = ScaleHandle::AxisX;
                    else if (yHandle.contains(e->pos())) hit = ScaleHandle::AxisY;
                    else if (cHandle.contains(e->pos())) hit = ScaleHandle::Center;
                    if (hit != ScaleHandle::None) {
                        m_scaleHandle = hit;
                        m_dragBeforeActors.clear();
                        m_dragBeforeActors << a;
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

            // 命中测试 Actor
            for (const ActorData& a : m_doc->actors()) {
                if (!actorScreenRect(a).contains(e->pos())) continue;

                if (ctrl) {
                    // Ctrl+点击：切换选中状态
                    if (m_selectedIds.contains(a.id))
                        m_selectedIds.remove(a.id);
                    else
                        m_selectedIds.insert(a.id);
                    m_selectedId = m_selectedIds.isEmpty() ? QString() : a.id;
                    emit selectionChanged(m_selectedIds.values());
                } else {
                    // 普通点击：如果未选中则单选；如果已在选区则保持选区，准备拖拽
                    if (!m_selectedIds.contains(a.id)) {
                        m_selectedIds.clear();
                        m_selectedIds.insert(a.id);
                        emit selectionChanged({a.id});
                        emit actorSelected(a);
                    }
                    m_selectedId  = a.id;
                    m_dragBeforeActors.clear();
                    for (const ActorData& b : m_doc->actors())
                        if (m_selectedIds.contains(b.id))
                            m_dragBeforeActors << b;
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
                        m_dragMouseStartWorld = screenToWorld(e->pos());
                        m_dragStartPositions.clear();
                        for (const ActorData& b : m_doc->actors())
                            if (m_selectedIds.contains(b.id))
                                m_dragStartPositions[b.id] = {b.x, b.y};
                    }
                }
                update();
                return;
            }

            // 点击空白区域
            if (!ctrl) {
                m_selectedId.clear();
                m_selectedIds.clear();
                m_dragging = false;
                emit selectionChanged({});
            }
            m_rubberBanding = true;
            m_rubberStart   = e->pos();
            m_rubberRect    = QRect();
            update();
        }

    } else if (e->button() == Qt::RightButton) {
        if (!m_doc) return;

        // 判断是否点在某个 Actor 上
        for (const ActorData& a : m_doc->actors()) {
            if (!actorScreenRect(a).contains(e->pos())) continue;
            {
                // 右键点在 Actor 上：弹出操作菜单
                const QString actorId   = a.id;
                const QString actorName = a.name;
                QMenu menu(this);
                menu.addAction("删除 "" + actorName + """, [this, actorId, a]() {
                    if (!m_doc) return;
                    if (m_undoStack && m_onRefresh) {
                        m_undoStack->push(new ActorRemoveCmd(m_doc, a, m_onRefresh));
                    } else {
                        m_doc->removeActor(actorId);
                        if (m_onRefresh) m_onRefresh();
                    }
                    if (m_selectedId == actorId) {
                        m_selectedId.clear();
                        m_selectedIds.clear();
                        emit selectionChanged({});
                    }
                    update();
                    emit actorRemoved(actorId);
                });
                menu.exec(e->globalPosition().toPoint());
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
                a.bpClass    = "builtin/" + type;
                a.components = defaultComponents(type);
                a.x    = (float)worldPos.x();
                a.y    = (float)worldPos.y();
                if (m_undoStack && m_onRefresh) {
                    m_undoStack->push(new ActorAddCmd(m_doc, a, m_onRefresh));
                } else {
                    m_doc->addActor(a);
                    if (m_onRefresh) m_onRefresh();
                }
                m_selectedId = a.id;
                m_selectedIds.clear();
                m_selectedIds.insert(a.id);
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

    // 框选拖拽
    if (m_rubberBanding) {
        m_rubberRect = QRect(m_rubberStart, e->pos()).normalized();
        update();
        return;
    }

    // 碰撞盒手柄拖拽：按手柄改动对应世界边，反算 宽/高/偏移
    if (m_colliderHandle != ColliderHandle::None && m_doc) {
        const QPointF mw = screenToWorld(e->pos());
        float L = m_cbStartL, R = m_cbStartR, B = m_cbStartB, T = m_cbStartT;
        const ColliderHandle h = m_colliderHandle;
        const bool left   = (h == ColliderHandle::TL || h == ColliderHandle::BL || h == ColliderHandle::Left);
        const bool right  = (h == ColliderHandle::TR || h == ColliderHandle::BR || h == ColliderHandle::Right);
        const bool top    = (h == ColliderHandle::TL || h == ColliderHandle::TR || h == ColliderHandle::Top);
        const bool bottom = (h == ColliderHandle::BL || h == ColliderHandle::BR || h == ColliderHandle::Bottom);
        if (left)   L = (float)mw.x();
        if (right)  R = (float)mw.x();
        if (top)    T = (float)mw.y();
        if (bottom) B = (float)mw.y();
        for (ActorData a : m_doc->actors()) {
            if (a.id != m_dragActorId) continue;
            const float cx = (L + R) / 2.0f, cy = (B + T) / 2.0f;
            a.colliderW = qMax(1.0f, std::abs(R - L));
            a.colliderH = qMax(1.0f, std::abs(T - B));
            a.colliderOffsetX = cx - a.x;
            a.colliderOffsetY = cy - a.y;
            m_doc->updateActor(a);
            emit actorDragging(a);
            break;
        }
        update();
        return;
    }

    if (m_dragging && m_doc) {
        if (m_toolMode == ToolMode::Rotate) {
            float newRot = m_dragRotStart + (e->pos().x() - m_dragAnchor.x()) * 0.4f;
            if (m_rotSnapEnabled)
                newRot = std::round(newRot / m_rotSnapAngle) * m_rotSnapAngle;
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
            // Move 模式：以绝对起始位置 + 总偏移量计算（避免吸附漂移）
            QPointF totalDelta = screenToWorld(e->pos()) - m_dragMouseStartWorld;
            ActorData primaryActor;
            bool foundPrimary = false;
            for (ActorData a : m_doc->actors()) {
                if (!m_selectedIds.contains(a.id)) continue;
                QPointF start = m_dragStartPositions.value(a.id, {a.x, a.y});
                float newX = (float)start.x();
                float newY = (float)start.y();
                // 轴约束
                if (m_scaleHandle == ScaleHandle::AxisX)
                    newX = (float)(start.x() + totalDelta.x());
                else if (m_scaleHandle == ScaleHandle::AxisY)
                    newY = (float)(start.y() + totalDelta.y());
                else {
                    newX = (float)(start.x() + totalDelta.x());
                    newY = (float)(start.y() + totalDelta.y());
                }
                // 网格吸附
                if (m_gridSnapEnabled) {
                    if (m_scaleHandle != ScaleHandle::AxisY)
                        newX = std::round(newX / m_gridSnapSize) * m_gridSnapSize;
                    if (m_scaleHandle != ScaleHandle::AxisX)
                        newY = std::round(newY / m_gridSnapSize) * m_gridSnapSize;
                }
                a.x = newX; a.y = newY;
                m_doc->updateActor(a);
                if (a.id == m_dragActorId) { primaryActor = a; foundPrimary = true; }
            }
            if (foundPrimary) emit actorDragging(primaryActor);
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

    if (e->button() == Qt::LeftButton && m_rubberBanding) {
        m_rubberBanding = false;
        // 框选结束：选中矩形内所有 Actor
        const bool ctrl = (e->modifiers() & Qt::ControlModifier) != 0;
        if (!ctrl) m_selectedIds.clear();
        if (m_doc && !m_rubberRect.isNull()) {
            for (const ActorData& a : m_doc->actors()) {
                if (m_rubberRect.contains(actorScreenRect(a).center().toPoint()))
                    m_selectedIds.insert(a.id);
            }
        }
        m_selectedId = m_selectedIds.isEmpty() ? QString() : *m_selectedIds.begin();
        m_rubberRect = QRect();
        emit selectionChanged(m_selectedIds.values());
        update();
        return;
    }

    // 碰撞盒手柄拖拽结束
    if (e->button() == Qt::LeftButton && m_colliderHandle != ColliderHandle::None) {
        m_colliderHandle = ColliderHandle::None;
        m_dragging = false;
        if (m_doc)
            for (const ActorData& a : m_doc->actors())
                if (a.id == m_dragActorId) { emit actorTransformed(a); break; }
        if (m_onRefresh) m_onRefresh();
        update();
        return;
    }

    if (e->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        if (m_doc) {
            // 收集拖拽后状态
            if (m_undoStack && m_onRefresh && !m_dragBeforeActors.isEmpty()) {
                QSet<QString> beforeIds;
                for (const ActorData& b : m_dragBeforeActors) beforeIds.insert(b.id);
                QList<ActorData> afterActors;
                for (const ActorData& a : m_doc->actors())
                    if (beforeIds.contains(a.id)) afterActors << a;
                // 判断是否有实际变化
                bool changed = false;
                for (const ActorData& b : m_dragBeforeActors) {
                    for (const ActorData& a : afterActors) {
                        if (a.id == b.id && (a.x != b.x || a.y != b.y
                            || a.rotation != b.rotation
                            || a.scaleX != b.scaleX || a.scaleY != b.scaleY)) {
                            changed = true; break;
                        }
                    }
                    if (changed) break;
                }
                if (changed)
                    m_undoStack->push(new ActorTransformCmd(m_doc, m_dragBeforeActors, afterActors, m_onRefresh));
            }
            m_dragBeforeActors.clear();

            for (const ActorData& a : m_doc->actors()) {
                if (a.id == m_dragActorId) {
                    emit actorTransformed(a);
                    break;
                }
            }
        }
        m_dragActorId.clear();
        m_dragStartPositions.clear();
    }
    if (e->button() == Qt::MiddleButton || e->button() == Qt::RightButton) {
        m_panning = false;
        applyToolCursor();
    }
}
