#pragma once

#include <QList>
#include <QString>

struct BattleUnit {
    QString id;
    QString name;
    QString team;
    int hp = 0;
    int maxHp = 0;
    int shield = 0;
    int energy = 0;
    int maxEnergy = 0;

    bool alive() const { return hp > 0; }
};

struct BattleCard {
    QString id;
    QString name;
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

class BattleRuntime {
public:
    void startDefault(int playerHp, int enemyHp, int energy, int maxEnergy);

    bool active() const { return m_active; }
    bool ended() const { return m_ended; }
    QString result() const { return m_result; }
    QString reason() const { return m_reason; }
    QString activeTeam() const { return m_activeTeam; }

    const BattleUnit& player() const { return m_player; }
    const BattleUnit& enemy() const { return m_enemy; }
    const QList<BattleCard>& hand() const { return m_hand; }

    QString handText() const;
    CardEffectResult useCard(int cardIndex);
    CardEffectResult endTurn();
    static bool isPointInRange(const QString& shape,
                               float originX, float originY,
                               float targetX, float targetY,
                               float radius, float width, float height,
                               float dirX, float dirY, float angleDegrees);

private:
    void checkEndState();
    int applyDamage(BattleUnit& target, int amount);
    void clampUnits();

    bool m_active = false;
    bool m_ended = false;
    QString m_result;
    QString m_reason;
    QString m_activeTeam;
    BattleUnit m_player;
    BattleUnit m_enemy;
    QList<BattleCard> m_hand;
};
