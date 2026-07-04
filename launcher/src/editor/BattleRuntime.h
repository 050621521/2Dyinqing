#pragma once

#include <QList>
#include <QSet>
#include <QString>

struct AttributeSet {
    int hp = 0;
    int maxHp = 0;
    int shield = 0;
    int energy = 0;
    int maxEnergy = 0;
};

struct GameplayStatus {
    QString tag;
    int stacks = 1;
    int remainingTurns = 0;
};

struct BattleUnit {
    QString id;
    QString name;
    QString team;
    int hp = 0;
    int maxHp = 0;
    int shield = 0;
    int energy = 0;
    int maxEnergy = 0;
    AttributeSet attributes;
    QSet<QString> tags;
    QList<GameplayStatus> statuses;

    bool alive() const { return hp > 0; }
};

struct BattleCard {
    QString id;
    QString name;
    QString description;
    QString effect;
    QString target;
    int cost = 0;
    int value = 0;
    bool used = false;
};

struct CardEffectResult {
    bool success = false;
    QString message;
};

struct CardPlayResult {
    bool success = false;
    QString message;
    BattleCard card;
};

class BattleRuntime {
public:
    void startDefault(int playerHp, int enemyHp, int energy, int maxEnergy, int playerMaxHp = 0);

    bool active() const { return m_active; }
    bool ended() const { return m_ended; }
    QString result() const { return m_result; }
    QString reason() const { return m_reason; }
    QString activeTeam() const { return m_activeTeam; }

    const BattleUnit& player() const { return m_player; }
    const BattleUnit& enemy() const { return m_enemy; }
    const QList<BattleCard>& hand() const { return m_hand; }
    const QList<BattleCard>& deck() const { return m_deck; }
    const QList<BattleCard>& discard() const { return m_discard; }
    const QList<BattleCard>& exhaust() const { return m_exhaust; }

    void setHand(const QList<BattleCard>& cards);
    QString handText() const;
    CardEffectResult useCard(int cardIndex);
    CardEffectResult useCardById(const QString& cardId);
    CardPlayResult prepareCardUse(int cardIndex);
    CardPlayResult prepareCardUseById(const QString& cardId);
    CardEffectResult applyCardEffect(const BattleCard& card);
    CardEffectResult damageUnit(const QString& unitKey, int amount);
    CardEffectResult healUnit(const QString& unitKey, int amount);
    CardEffectResult addShield(const QString& unitKey, int amount);
    CardEffectResult addEnergy(const QString& unitKey, int amount);
    CardEffectResult addTag(const QString& unitKey, const QString& tag);
    CardEffectResult removeTag(const QString& unitKey, const QString& tag);
    CardEffectResult addStatus(const QString& unitKey, const QString& tag, int stacks = 1, int turns = 0);
    CardEffectResult setStatusStacks(const QString& unitKey, const QString& tag, int stacks);
    bool hasTag(const QString& unitKey, const QString& tag) const;
    void drawCards(int count);
    CardEffectResult endTurn();
    static bool isPointInRange(const QString& shape,
                               float originX, float originY,
                               float targetX, float targetY,
                               float radius, float width, float height,
                               float dirX, float dirY, float angleDegrees);

private:
    void checkEndState();
    BattleUnit* unitByKey(const QString& unitKey);
    CardEffectResult useCardAt(int idx);
    CardPlayResult prepareCardUseAt(int idx);
    int applyDamage(BattleUnit& target, int amount);
    void clampUnits();
    void syncFieldsFromAttributes(BattleUnit& unit);
    void syncAttributesFromFields(BattleUnit& unit);
    void discardHand();
    void recycleDiscardIntoDeck();
    void tickStatuses(BattleUnit& unit);

    bool m_active = false;
    bool m_ended = false;
    QString m_result;
    QString m_reason;
    QString m_activeTeam;
    BattleUnit m_player;
    BattleUnit m_enemy;
    QList<BattleCard> m_deck;
    QList<BattleCard> m_hand;
    QList<BattleCard> m_discard;
    QList<BattleCard> m_exhaust;
};
