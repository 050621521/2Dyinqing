#include "UiBindingRuntime.h"

#include "UIRuntime.h"

#include <QStringList>

UiBindingRuntime::UiBindingRuntime(UIRuntime* uiRuntime)
    : m_uiRuntime(uiRuntime) {}

QString UiBindingRuntime::findUiByBindingOrWidget(const QStringList& bindingKeys,
                                                  const QStringList& fallbackWidgetNames) const {
    if (!m_uiRuntime) return {};
    for (const UIInstance* inst : m_uiRuntime->shownInstances()) {
        for (const UIWidget& w : inst->docCopy.widgets()) {
            if (!w.bindingKey.isEmpty() && bindingKeys.contains(w.bindingKey))
                return inst->uiName;
            if (fallbackWidgetNames.contains(w.name))
                return inst->uiName;
        }
    }
    return {};
}

QString UiBindingRuntime::widgetName(const QString& uiName,
                                     const QString& bindingKey,
                                     const QString& fallbackWidgetName) const {
    if (!m_uiRuntime) return fallbackWidgetName;
    for (const UIInstance* inst : m_uiRuntime->shownInstances()) {
        if (inst->uiName != uiName) continue;
        for (const UIWidget& w : inst->docCopy.widgets()) {
            if (w.bindingKey == bindingKey)
                return w.name;
        }
        break;
    }
    return fallbackWidgetName;
}
