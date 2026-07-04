#include "CardRuntimeController.h"

#include "DataTableRuntimeService.h"
#include "EffectRuntime.h"

CardRuntimeController::CardRuntimeController(BattleRuntime* battle,
                                             DataTableRuntimeService* dataTables,
                                             EffectRuntime* effects)
    : m_battle(battle), m_dataTables(dataTables), m_effects(effects) {}

bool CardRuntimeController::setHandFromTable(const QString& tableRef) const {
    if (!m_battle || !m_dataTables) return false;
    const QList<BattleCard> cards = m_dataTables->cardsFromTable(tableRef);
    if (cards.isEmpty()) return false;
    m_battle->setHand(cards);
    return true;
}

CardEffectResult CardRuntimeController::useCard(const QString& cardId, int cardIndex) const {
    if (!m_battle) return {false, "战斗未开始"};
    const CardPlayResult prepared = cardId.trimmed().isEmpty()
                                  ? m_battle->prepareCardUse(cardIndex)
                                  : m_battle->prepareCardUseById(cardId);
    return applyPreparedCard(prepared);
}

CardEffectResult CardRuntimeController::useCardByWidget(const QString& widgetName) const {
    if (!m_battle || !widgetName.startsWith("卡_")) return {false, {}};
    const QString cardName = widgetName.mid(2).trimmed();
    if (cardName.isEmpty()) return {false, {}};
    return applyPreparedCard(m_battle->prepareCardUseById(cardName));
}

CardEffectResult CardRuntimeController::applyPreparedCard(const CardPlayResult& prepared) const {
    if (!m_battle) return {false, "战斗未开始"};
    if (!prepared.success) return {false, prepared.message};

    if (isBlueprintEffect(prepared.card.effect) && m_effects) {
        EffectContext context;
        context.source = "player";
        context.target = prepared.card.target.isEmpty() ? "enemy" : prepared.card.target;
        context.cardId = prepared.card.id;
        context.recordId = prepared.card.id;
        context.value = prepared.card.value;
        return m_effects->executeBlueprint(prepared.card.effect, context, prepared.card.name);
    }

    return m_battle->applyCardEffect(prepared.card);
}

bool CardRuntimeController::isBlueprintEffect(const QString& effectRef) {
    return effectRef.endsWith(".bp") || effectRef.contains('/');
}
