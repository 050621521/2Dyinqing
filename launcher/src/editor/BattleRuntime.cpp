#include "BattleRuntime.h"

#include <QtGlobal>
#include <cmath>

void BattleRuntime::startDefault(int playerHp, int enemyHp, int energy, int maxEnergy) {
    m_active = true;
    m_ended = false;
    m_result.clear();
    m_reason.clear();
    m_activeTeam = "玩家";

    const int safePlayerHp = qMax(1, playerHp);
    const int safeEnemyHp = qMax(1, enemyHp);
    const int safeMaxEnergy = qMax(1, maxEnergy);
    const int safeEnergy = qBound(0, energy, safeMaxEnergy);

    m_player = {"player", "玩家", "玩家", safePlayerHp, safePlayerHp, 0, safeEnergy, safeMaxEnergy};
    m_enemy = {"enemy", "敌人", "敌方", safeEnemyHp, safeEnemyHp, 0, 0, 0};
    m_hand = {
        {"attack", "攻击牌", "damage", "enemy", 1, 8, false},
        {"guard", "防守牌", "shield", "self", 1, 6, false},
        {"heal", "治疗牌", "heal", "self", 2, 5, false},
        {"charge", "回能牌", "energy", "self", 0, 1, false}
    };
}

QString BattleRuntime::handText() const {
    QStringList parts;
    for (int i = 0; i < m_hand.size(); ++i) {
        const BattleCard& c = m_hand[i];
        const QString state = c.used ? "已用" : QString::number(c.cost) + "能量";
        parts << QString("%1.%2(%3)").arg(i + 1).arg(c.name, state);
    }
    return parts.join(" / ");
}

CardEffectResult BattleRuntime::useCard(int cardIndex) {
    if (!m_active) return {false, "战斗未开始"};
    if (m_ended) return {false, "战斗已结束"};
    if (m_activeTeam != "玩家") return {false, "敌方回合"};

    const int idx = cardIndex - 1;
    if (idx < 0 || idx >= m_hand.size()) return {false, "卡牌无效"};
    BattleCard& card = m_hand[idx];
    if (card.used) return {false, "卡牌已使用"};
    if (m_player.energy < card.cost) return {false, "能量不足"};

    m_player.energy -= card.cost;
    card.used = true;

    if (card.effect == "damage") {
        const int dealt = applyDamage(m_enemy, card.value);
        checkEndState();
        return {true, QString("%1造成%2点伤害").arg(card.name).arg(dealt)};
    }
    if (card.effect == "shield") {
        m_player.shield += card.value;
        return {true, QString("%1获得%2点护盾").arg(card.name).arg(card.value)};
    }
    if (card.effect == "heal") {
        const int before = m_player.hp;
        m_player.hp = qMin(m_player.maxHp, m_player.hp + card.value);
        return {true, QString("%1回复%2点生命").arg(card.name).arg(m_player.hp - before)};
    }
    if (card.effect == "energy") {
        const int before = m_player.energy;
        m_player.energy = qMin(m_player.maxEnergy, m_player.energy + card.value);
        return {true, QString("%1恢复%2点能量").arg(card.name).arg(m_player.energy - before)};
    }

    return {false, "卡牌效果未实现"};
}

CardEffectResult BattleRuntime::endTurn() {
    if (!m_active) return {false, "战斗未开始"};
    if (m_ended) return {false, "战斗已结束"};

    m_activeTeam = "敌方";
    const int dealt = applyDamage(m_player, 6);
    checkEndState();
    if (m_ended) return {true, QString("敌方造成%1点伤害").arg(dealt)};

    m_player.shield = 0;
    m_player.energy = m_player.maxEnergy;
    m_activeTeam = "玩家";
    return {true, QString("敌方造成%1点伤害，玩家回合").arg(dealt)};
}

void BattleRuntime::checkEndState() {
    clampUnits();
    if (m_enemy.hp <= 0) {
        m_ended = true;
        m_result = "胜利";
        m_reason = "敌方被击败";
        m_activeTeam.clear();
    } else if (m_player.hp <= 0) {
        m_ended = true;
        m_result = "失败";
        m_reason = "玩家被击败";
        m_activeTeam.clear();
    }
}

int BattleRuntime::applyDamage(BattleUnit& target, int amount) {
    const int safeAmount = qMax(0, amount);
    const int absorbed = qMin(target.shield, safeAmount);
    target.shield -= absorbed;
    const int damage = safeAmount - absorbed;
    target.hp = qMax(0, target.hp - damage);
    return damage;
}

void BattleRuntime::clampUnits() {
    m_player.hp = qBound(0, m_player.hp, m_player.maxHp);
    m_enemy.hp = qBound(0, m_enemy.hp, m_enemy.maxHp);
    m_player.energy = qBound(0, m_player.energy, m_player.maxEnergy);
    m_player.shield = qMax(0, m_player.shield);
    m_enemy.shield = qMax(0, m_enemy.shield);
}

bool BattleRuntime::isPointInRange(const QString& shape,
                                   float originX, float originY,
                                   float targetX, float targetY,
                                   float radius, float width, float height,
                                   float dirX, float dirY, float angleDegrees) {
    const QString s = shape.trimmed();
    const float dx = targetX - originX;
    const float dy = targetY - originY;
    const float dist2 = dx * dx + dy * dy;
    const float safeRadius = qMax(0.0f, radius);
    if (s == "圆形" || s.compare("circle", Qt::CaseInsensitive) == 0)
        return dist2 <= safeRadius * safeRadius;

    if (s == "方形" || s.compare("square", Qt::CaseInsensitive) == 0) {
        const float half = safeRadius > 0.0f ? safeRadius : qMax(width, height) * 0.5f;
        return std::abs(dx) <= half && std::abs(dy) <= half;
    }

    if (s == "长方形" || s == "矩形" || s.compare("rect", Qt::CaseInsensitive) == 0) {
        const float halfW = qMax(0.0f, width) * 0.5f;
        const float halfH = qMax(0.0f, height) * 0.5f;
        return std::abs(dx) <= halfW && std::abs(dy) <= halfH;
    }

    if (s == "扇形" || s.compare("sector", Qt::CaseInsensitive) == 0) {
        if (dist2 > safeRadius * safeRadius) return false;
        float len = std::sqrt(dirX * dirX + dirY * dirY);
        if (len <= 0.0001f) { dirX = 1.0f; dirY = 0.0f; len = 1.0f; }
        const float targetLen = std::sqrt(dist2);
        if (targetLen <= 0.0001f) return true;
        const float dot = (dx * dirX + dy * dirY) / (targetLen * len);
        const float clamped = qBound(-1.0f, dot, 1.0f);
        const float radians = std::acos(clamped);
        const float degrees = radians * 180.0f / 3.14159265358979323846f;
        return degrees <= qMax(0.0f, angleDegrees) * 0.5f;
    }

    return false;
}
