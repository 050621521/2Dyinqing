#pragma once

#include <QString>

class UIRuntime;

class UiBindingRuntime {
public:
    explicit UiBindingRuntime(UIRuntime* uiRuntime = nullptr);

    void setUIRuntime(UIRuntime* uiRuntime) { m_uiRuntime = uiRuntime; }
    QString findUiByBindingOrWidget(const QStringList& bindingKeys, const QStringList& fallbackWidgetNames) const;
    QString widgetName(const QString& uiName, const QString& bindingKey, const QString& fallbackWidgetName) const;

private:
    UIRuntime* m_uiRuntime = nullptr;
};
