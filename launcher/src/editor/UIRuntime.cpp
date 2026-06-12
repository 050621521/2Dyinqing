#include "UIRuntime.h"
#include <QUuid>
#include <QDir>

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

void UIRuntime::showByName   (const QString& n) { if (auto* i=findByName(n)) showInstance(i->instanceId); }
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

QString UIRuntime::createInstance(const QString& uiName) {
    const QString path = m_projectRoot + "/UI/" + uiName + ".ui";
    UIDocument tmpl;
    if (!tmpl.load(path)) return {};

    auto* inst = new UIInstance;
    inst->instanceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    inst->uiName     = uiName;
    inst->docCopy    = tmpl;
    inst->shown      = false;
    m_all << inst;
    emit uiStateChanged();
    return inst->instanceId;
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
