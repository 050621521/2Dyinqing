#include "BattleUiPresenter.h"

#include "UIRuntime.h"

#include <QSet>
#include <QtGlobal>

BattleUiPresenter::BattleUiPresenter(UIRuntime* uiRuntime)
    : m_uiRuntime(uiRuntime), m_bindingRuntime(uiRuntime) {}

QString BattleUiPresenter::findBattleUiName() const {
    return m_bindingRuntime.findUiByBindingOrWidget(
        {"battle.hpText", "battle.energyText", "battle.messageText"},
        {m_binding.hpText, m_binding.energyText, m_binding.messageText});
}

void BattleUiPresenter::refresh(const BattleRuntime* battle, const QString& message) {
    if (!battle || !m_uiRuntime || !battle->active()) return;
    const QString uiName = findBattleUiName();
    if (uiName.isEmpty()) return;

    const BattleUnit& player = battle->player();
    const int maxHp = qMax(1, player.maxHp);
    const int maxEnergy = qMax(1, player.maxEnergy);
    const QString hpText = m_bindingRuntime.widgetName(uiName, "battle.hpText", m_binding.hpText);
    const QString hpBar = m_bindingRuntime.widgetName(uiName, "battle.hpBar", m_binding.hpBar);
    const QString energyText = m_bindingRuntime.widgetName(uiName, "battle.energyText", m_binding.energyText);
    const QString energyBar = m_bindingRuntime.widgetName(uiName, "battle.energyBar", m_binding.energyBar);
    const QString messageText = m_bindingRuntime.widgetName(uiName, "battle.messageText", m_binding.messageText);
    const QString deckText = m_bindingRuntime.widgetName(uiName, "battle.deckText", m_binding.deckText);
    const QString discardText = m_bindingRuntime.widgetName(uiName, "battle.discardText", m_binding.discardText);
    m_uiRuntime->setTextByName(uiName, hpText,
        QString("%1 / %2").arg(player.hp).arg(player.maxHp));
    m_uiRuntime->setValueByName(uiName, hpBar, (float)player.hp / maxHp);
    m_uiRuntime->setTextByName(uiName, energyText,
        QString("%1 / %2").arg(player.energy).arg(player.maxEnergy));
    m_uiRuntime->setValueByName(uiName, energyBar, (float)player.energy / maxEnergy);
    if (!message.isEmpty())
        m_uiRuntime->setTextByName(uiName, messageText, message);

    int discardCount = 0;
    QSet<QString> handWidgetNames;
    for (const BattleCard& card : battle->hand()) {
        const QString widget = m_binding.cardPrefix + card.name;
        if (card.used) {
            ++discardCount;
            m_uiRuntime->setWidgetVisibleByName(uiName, widget, false);
            continue;
        }

        handWidgetNames.insert(widget);
        m_uiRuntime->setWidgetVisibleByName(uiName, widget, true);
        m_uiRuntime->setTextByName(uiName, m_binding.costPrefix + card.name, QString::number(card.cost));
        const QString desc = card.description.trimmed().isEmpty()
                           ? QString("%1能量").arg(card.cost)
                           : card.description.trimmed();
        m_uiRuntime->setTextByName(uiName, m_binding.descriptionPrefix + card.name, desc);
    }
    m_uiRuntime->setTextByName(uiName, deckText, QString("牌库 %1").arg(battle->deck().size()));
    m_uiRuntime->setTextByName(uiName, discardText, QString("弃牌 %1").arg(discardCount + battle->discard().size()));

    for (const UIInstance* inst : m_uiRuntime->shownInstances()) {
        if (inst->uiName != uiName) continue;
        for (const UIWidget& w : inst->docCopy.widgets()) {
            if (!w.name.startsWith(m_binding.cardPrefix)) continue;
            if (!handWidgetNames.contains(w.name))
                m_uiRuntime->setWidgetVisibleByName(uiName, w.name, false);
        }
        break;
    }
}
