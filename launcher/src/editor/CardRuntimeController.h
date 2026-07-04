#pragma once

#include "BattleRuntime.h"

#include <QString>

class DataTableRuntimeService;
class EffectRuntime;

class CardRuntimeController {
public:
    CardRuntimeController(BattleRuntime* battle = nullptr,
                          DataTableRuntimeService* dataTables = nullptr,
                          EffectRuntime* effects = nullptr);

    void setBattleRuntime(BattleRuntime* battle) { m_battle = battle; }
    void setDataTableRuntime(DataTableRuntimeService* dataTables) { m_dataTables = dataTables; }
    void setEffectRuntime(EffectRuntime* effects) { m_effects = effects; }

    bool setHandFromTable(const QString& tableRef) const;
    CardEffectResult useCard(const QString& cardId, int cardIndex) const;
    CardEffectResult useCardByWidget(const QString& widgetName) const;

private:
    CardEffectResult applyPreparedCard(const CardPlayResult& prepared) const;
    static bool isBlueprintEffect(const QString& effectRef);

    BattleRuntime* m_battle = nullptr;
    DataTableRuntimeService* m_dataTables = nullptr;
    EffectRuntime* m_effects = nullptr;
};
