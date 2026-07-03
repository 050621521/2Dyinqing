#include "UIRuntime.h"
#include <QUuid>
#include <QDir>
#include <algorithm>

UIRuntime::UIRuntime(const QString& projectRoot, QObject* parent)
    : QObject(parent), m_projectRoot(projectRoot) {}

UIRuntime::~UIRuntime() { qDeleteAll(m_all); }

UIInstance* UIRuntime::findInstance(const QString& id) {
    for (UIInstance* inst : m_all)
        if (inst->instanceId == id) return inst;
    return nullptr;
}

UIInstance* UIRuntime::findByName(const QString& uiName) {
    for (UIInstance* inst : m_all)
        if (inst->uiName == uiName) return inst;
    return nullptr;
}

void UIRuntime::showByName(const QString& n) {
    if (!findByName(n)) createInstance(n);  // 不存在则自动创建
    if (auto* i = findByName(n)) showInstance(i->instanceId);
}
void UIRuntime::hideByName   (const QString& n) { if (auto* i=findByName(n)) hideInstance(i->instanceId); }
void UIRuntime::destroyByName(const QString& n) { if (auto* i=findByName(n)) destroyInstance(i->instanceId); }
void UIRuntime::setTextByName(const QString& n, const QString& w, const QString& t) {
    if (auto* i=findByName(n)) setText(i->instanceId, w, t);
}
void UIRuntime::setValueByName(const QString& n, const QString& w, float v) {
    if (auto* i=findByName(n)) setValue(i->instanceId, w, v);
}
void UIRuntime::setPositionByName(const QString& n, float x, float y) {
    if (auto* i=findByName(n)) setPosition(i->instanceId, x, y);
}
void UIRuntime::setWidgetVisibleByName(const QString& n, const QString& w, bool v) {
    if (auto* i=findByName(n)) setWidgetVisible(i->instanceId, w, v);
}
void UIRuntime::setScrollByName(const QString& n, const QString& w, float x, float y) {
    if (auto* i=findByName(n)) setScroll(i->instanceId, w, x, y);
}
void UIRuntime::setWidgetColorByName(const QString& n, const QString& w, const QColor& c) {
    if (auto* i=findByName(n)) setWidgetColor(i->instanceId, w, c);
}
void UIRuntime::setWidgetAlphaByName(const QString& n, const QString& w, float a) {
    if (auto* i=findByName(n)) setWidgetAlpha(i->instanceId, w, a);
}
void UIRuntime::setWidgetSizeByName(const QString& n, const QString& w, float width, float height) {
    if (auto* i=findByName(n)) setWidgetSize(i->instanceId, w, width, height);
}

QString UIRuntime::createInstance(const QString& uiName) {
    const QString path = m_projectRoot + "/UI/" + uiName + ".ui";
    UIDocument tmpl;
    if (!tmpl.load(path)) return {};

    // 所有控件默认隐藏，由「显示UI」节点按需显示
    for (const UIWidget& w : tmpl.widgets()) {
        UIWidget hidden = w;
        hidden.visible = false;
        tmpl.updateWidget(hidden);
    }

    auto* inst = new UIInstance;
    inst->instanceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    inst->uiName     = uiName;
    inst->docCopy    = tmpl;
    inst->shown      = true;   // 实例始终在渲染列表，靠 widget.visible 控制显示
    m_all   << inst;
    m_shown << inst;
    emit uiStateChanged();
    return inst->instanceId;
}

QString UIRuntime::showWidgetByName(const QString& uiName, const QString& widgetName) {
    UIInstance* inst = findByName(uiName);
    if (!inst) {
        const QString id = createInstance(uiName);
        inst = findInstance(id);
        if (!inst) return {};
    }
    // widgetName 为空 → 显示整个 UI 的所有控件
    if (widgetName.isEmpty()) {
        for (const UIWidget& w : inst->docCopy.widgets()) {
            UIWidget updated = w;
            updated.visible = true;
            inst->docCopy.updateWidget(updated);
        }
        emit uiStateChanged();
        return inst->instanceId;
    }
    // 显示指定控件及其所有祖先容器
    for (const UIWidget& w : inst->docCopy.widgets()) {
        if (w.name != widgetName) continue;
        UIWidget updated = w;
        updated.visible = true;
        inst->docCopy.updateWidget(updated);
        QString parentId = updated.parentId;
        while (!parentId.isEmpty()) {
            bool found = false;
            for (const UIWidget& pw : inst->docCopy.widgets()) {
                if (pw.id != parentId) continue;
                UIWidget pu = pw;
                pu.visible = true;
                inst->docCopy.updateWidget(pu);
                parentId = pu.parentId;
                found = true;
                break;
            }
            if (!found) break;
        }
        break;
    }
    emit uiStateChanged();
    return inst->instanceId;
}

void UIRuntime::hideWidgetByName(const QString& uiName, const QString& widgetName) {
    setWidgetVisibleByName(uiName, widgetName, false);
}

void UIRuntime::showInstance(const QString& id) {
    UIInstance* inst = findInstance(id);
    if (!inst || inst->shown) return;
    inst->shown = true;
    if (!m_shown.contains(inst)) m_shown << inst;
    emit uiStateChanged();
}

void UIRuntime::hideInstance(const QString& id) {
    UIInstance* inst = findInstance(id);
    if (!inst || !inst->shown) return;
    inst->shown = false;
    m_shown.removeOne(inst);
    emit uiStateChanged();
}

void UIRuntime::destroyInstance(const QString& id) {
    UIInstance* inst = findInstance(id);
    if (!inst) return;
    m_shown.removeOne(inst);
    m_all.removeOne(inst);
    delete inst;
    emit uiStateChanged();
}

void UIRuntime::setText(const QString& id, const QString& widgetName, const QString& text) {
    UIInstance* inst = findInstance(id);
    if (!inst) return;
    for (const UIWidget& w : inst->docCopy.widgets()) {
        if (w.name == widgetName) {
            UIWidget updated = w;
            updated.text = text;
            inst->docCopy.updateWidget(updated);
            break;
        }
    }
    emit uiStateChanged();
}

void UIRuntime::setValue(const QString& id, const QString& widgetName, float value) {
    UIInstance* inst = findInstance(id);
    if (!inst) return;
    for (const UIWidget& w : inst->docCopy.widgets()) {
        if (w.name == widgetName) {
            UIWidget updated = w;
            updated.value = qBound(0.0f, value, 1.0f);
            inst->docCopy.updateWidget(updated);
            break;
        }
    }
    emit uiStateChanged();
}

void UIRuntime::setPosition(const QString& id, float x, float y) {
    UIInstance* inst = findInstance(id);
    if (!inst) return;
    inst->screenX = x;
    inst->screenY = y;
    emit uiStateChanged();
}

void UIRuntime::setScroll(const QString& id, const QString& widgetName, float x, float y) {
    UIInstance* inst = findInstance(id);
    if (!inst) return;
    for (const UIWidget& w : inst->docCopy.widgets()) {
        if (w.name == widgetName) {
            UIWidget updated = w;
            const float maxX = qMax(0.0f, updated.contentWidth  - updated.width);
            const float maxY = qMax(0.0f, updated.contentHeight - updated.height);
            updated.scrollX = qBound(0.0f, x, maxX > 0.0f ? maxX : qMax(0.0f, x));
            updated.scrollY = qBound(0.0f, y, maxY > 0.0f ? maxY : qMax(0.0f, y));
            inst->docCopy.updateWidget(updated);
            break;
        }
    }
    emit uiStateChanged();
}

void UIRuntime::setWidgetColor(const QString& id, const QString& widgetName, const QColor& color) {
    UIInstance* inst = findInstance(id);
    if (!inst) return;
    for (const UIWidget& w : inst->docCopy.widgets()) {
        if (w.name == widgetName) {
            UIWidget updated = w;
            updated.color = color;
            inst->docCopy.updateWidget(updated);
            break;
        }
    }
    emit uiStateChanged();
}

void UIRuntime::setWidgetAlpha(const QString& id, const QString& widgetName, float alpha) {
    UIInstance* inst = findInstance(id);
    if (!inst) return;
    for (const UIWidget& w : inst->docCopy.widgets()) {
        if (w.name == widgetName) {
            UIWidget updated = w;
            updated.alpha = qBound(0.0f, alpha, 1.0f);
            inst->docCopy.updateWidget(updated);
            break;
        }
    }
    emit uiStateChanged();
}

void UIRuntime::setWidgetSize(const QString& id, const QString& widgetName, float width, float height) {
    UIInstance* inst = findInstance(id);
    if (!inst) return;
    for (const UIWidget& w : inst->docCopy.widgets()) {
        if (w.name == widgetName) {
            UIWidget updated = w;
            updated.width = qMax(1.0f, width);
            updated.height = qMax(1.0f, height);
            inst->docCopy.updateWidget(updated);
            break;
        }
    }
    emit uiStateChanged();
}

void UIRuntime::setFollowActor(const QString& instanceId, const QString& actorId,
                               float offsetX, float offsetY) {
    UIInstance* inst = findInstance(instanceId);
    if (!inst) return;
    inst->followActorId = actorId;
    inst->followOffsetX = offsetX;
    inst->followOffsetY = offsetY;
    emit uiStateChanged();
}

void UIRuntime::setFollowActorRef(const QString& ref, const QString& actorId,
                                  float offsetX, float offsetY) {
    UIInstance* inst = findInstance(ref);   // 先按实例 id
    if (!inst) inst = findByName(ref);      // 再按 UI 名
    if (!inst) return;
    setFollowActor(inst->instanceId, actorId, offsetX, offsetY);
}

void UIRuntime::clearFollow(const QString& instanceId) {
    if (UIInstance* inst = findInstance(instanceId)) {
        inst->followActorId.clear();
        emit uiStateChanged();
    }
}

void UIRuntime::notifyButtonClicked(const QString& instanceId, const QString& widgetName) {
    emit buttonClicked(instanceId, widgetName);
}

void UIRuntime::notifyDropdownChanged(const QString& instanceId, const QString& widgetName, int index) {
    emit dropdownChanged(instanceId, widgetName, index);
}

void UIRuntime::notifyDragStarted(const QString& instanceId, const QString& widgetName, float x, float y) {
    emit dragStarted(instanceId, widgetName, x, y);
}

void UIRuntime::notifyDragMoved(const QString& instanceId, const QString& widgetName, float x, float y) {
    emit dragMoved(instanceId, widgetName, x, y);
}

void UIRuntime::notifyDropped(const QString& instanceId, const QString& widgetName, float x, float y) {
    emit dropped(instanceId, widgetName, x, y);
}

void UIRuntime::notifyDragCanceled(const QString& instanceId, const QString& widgetName, float x, float y) {
    emit dragCanceled(instanceId, widgetName, x, y);
}

void UIRuntime::setWidgetVisible(const QString& id, const QString& widgetName, bool visible) {
    UIInstance* inst = findInstance(id);
    if (!inst) return;
    for (const UIWidget& w : inst->docCopy.widgets()) {
        if (w.name == widgetName) {
            UIWidget updated = w;
            updated.visible = visible;
            inst->docCopy.updateWidget(updated);
            break;
        }
    }
    emit uiStateChanged();
}
