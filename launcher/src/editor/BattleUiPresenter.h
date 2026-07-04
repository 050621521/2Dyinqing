#pragma once

#include "BattleRuntime.h"
#include "UiBindingRuntime.h"

#include <QString>

class UIRuntime;

struct BattleUiBinding {
    QString hpText = "HP数值";
    QString hpBar = "HP条";
    QString energyText = "能量数值";
    QString energyBar = "MP条";
    QString messageText = "战斗消息";
    QString deckText = "牌库按钮";
    QString discardText = "弃牌按钮";
    QString cardPrefix = "卡_";
    QString costPrefix = "费用_";
    QString descriptionPrefix = "卡描述_";
};

class BattleUiPresenter {
public:
    explicit BattleUiPresenter(UIRuntime* uiRuntime = nullptr);

    void setUIRuntime(UIRuntime* uiRuntime) { m_uiRuntime = uiRuntime; m_bindingRuntime.setUIRuntime(uiRuntime); }
    void setBinding(const BattleUiBinding& binding) { m_binding = binding; }
    void refresh(const BattleRuntime* battle, const QString& message);

private:
    QString findBattleUiName() const;

    UIRuntime* m_uiRuntime = nullptr;
    BattleUiBinding m_binding;
    UiBindingRuntime m_bindingRuntime;
};
