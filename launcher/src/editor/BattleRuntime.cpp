#include "BattleRuntime.h"

#include <QtGlobal>
#include <cmath>
#include <utility>

void BattleRuntime::startDefault(int playerHp, int enemyHp, int energy, int maxEnergy, int playerMaxHp) {
    m_active = true;
    m_ended = false;
    m_result.clear();
    m_reason.clear();
    m_activeTeam = "玩家";

    const int safePlayerMaxHp = qMax(1, playerMaxHp > 0 ? playerMaxHp : playerHp);
    const int safePlayerHp = qBound(1, playerHp, safePlayerMaxHp);
    const int safeEnemyHp = qMax(1, enemyHp);
    const int safeMaxEnergy = qMax(1, maxEnergy);
    const int safeEnergy = qBound(0, energy, safeMaxEnergy);

    m_player = {"player", "玩家", "玩家", safePlayerHp, safePlayerMaxHp, 0, safeEnergy, safeMaxEnergy};
    m_enemy = {"enemy", "敌人", "敌方", safeEnemyHp, safeEnemyHp, 0, 0, 0};
    syncAttributesFromFields(m_player);
    syncAttributesFromFields(m_enemy);
    const QList<BattleCard> defaultCards = {
        {"attack", "攻击牌", "造成8点伤害", "damage", "enemy", 1, 8, false},
        {"guard", "防守牌", "获得6点护盾", "shield", "self", 1, 6, false},
        {"heal", "治疗牌", "回复5点生命", "heal", "self", 2, 5, false},
        {"charge", "回能牌", "恢复1点能量", "energy", "self", 0, 1, false}
    };
    setHand(defaultCards);
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

void BattleRuntime::setHand(const QList<BattleCard>& cards) {
    if (cards.isEmpty()) return;
    m_deck = cards;
    m_hand.clear();
    m_discard.clear();
    m_exhaust.clear();
    drawCards(qMin(4, m_deck.size()));
}

CardEffectResult BattleRuntime::useCard(int cardIndex) {
    return useCardAt(cardIndex - 1);
}

CardEffectResult BattleRuntime::useCardById(const QString& cardId) {
    const QString wanted = cardId.trimmed();
    if (wanted.isEmpty()) return {false, "卡牌无效"};
    for (int i = 0; i < m_hand.size(); ++i) {
        if (m_hand[i].id == wanted || m_hand[i].name == wanted)
            return useCardAt(i);
    }
    return {false, "卡牌无效"};
}

CardEffectResult BattleRuntime::useCardAt(int idx) {
    const CardPlayResult prepared = prepareCardUseAt(idx);
    if (!prepared.success) return {false, prepared.message};
    return applyCardEffect(prepared.card);
}

CardPlayResult BattleRuntime::prepareCardUse(int cardIndex) {
    return prepareCardUseAt(cardIndex - 1);
}

CardPlayResult BattleRuntime::prepareCardUseById(const QString& cardId) {
    const QString wanted = cardId.trimmed();
    if (wanted.isEmpty()) return {false, "卡牌无效", {}};
    for (int i = 0; i < m_hand.size(); ++i) {
        if (m_hand[i].id == wanted || m_hand[i].name == wanted)
            return prepareCardUseAt(i);
    }
    return {false, "卡牌无效", {}};
}

CardPlayResult BattleRuntime::prepareCardUseAt(int idx) {
    if (!m_active) return {false, "战斗未开始"};
    if (m_ended) return {false, "战斗已结束"};
    if (m_activeTeam != "玩家") return {false, "敌方回合"};

    if (idx < 0 || idx >= m_hand.size()) return {false, "卡牌无效"};
    BattleCard card = m_hand[idx];
    if (card.used) return {false, "卡牌已使用"};
    if (m_player.energy < card.cost) return {false, "能量不足"};

    m_player.energy -= card.cost;
    syncAttributesFromFields(m_player);
    card.used = true;
    m_hand.removeAt(idx);
    m_discard.append(card);
    return {true, QString("使用%1").arg(card.name), card};
}

CardEffectResult BattleRuntime::applyCardEffect(const BattleCard& card) {
    if (card.effect == "damage") {
        const int dealt = applyDamage(m_enemy, card.value);
        checkEndState();
        return {true, QString("%1造成%2点伤害").arg(card.name).arg(dealt)};
    }
    if (card.effect == "shield") {
        m_player.shield += card.value;
        syncAttributesFromFields(m_player);
        return {true, QString("%1获得%2点护盾").arg(card.name).arg(card.value)};
    }
    if (card.effect == "heal") {
        const int before = m_player.hp;
        m_player.hp = qMin(m_player.maxHp, m_player.hp + card.value);
        syncAttributesFromFields(m_player);
        return {true, QString("%1回复%2点生命").arg(card.name).arg(m_player.hp - before)};
    }
    if (card.effect == "energy") {
        const int before = m_player.energy;
        m_player.energy = qMin(m_player.maxEnergy, m_player.energy + card.value);
        syncAttributesFromFields(m_player);
        return {true, QString("%1恢复%2点能量").arg(card.name).arg(m_player.energy - before)};
    }

    return {false, "卡牌效果未实现"};
}

BattleUnit* BattleRuntime::unitByKey(const QString& unitKey) {
    const QString key = unitKey.trimmed().toLower();
    if (key == "enemy" || key == "敌人" || key == "敌方") return &m_enemy;
    return &m_player;
}

CardEffectResult BattleRuntime::damageUnit(const QString& unitKey, int amount) {
    BattleUnit* target = unitByKey(unitKey);
    if (!target) return {false, "目标无效"};
    const int dealt = applyDamage(*target, amount);
    checkEndState();
    return {true, QString("%1受到%2点伤害").arg(target->name).arg(dealt)};
}

CardEffectResult BattleRuntime::healUnit(const QString& unitKey, int amount) {
    BattleUnit* target = unitByKey(unitKey);
    if (!target) return {false, "目标无效"};
    const int before = target->hp;
    target->hp = qMin(target->maxHp, target->hp + qMax(0, amount));
    syncAttributesFromFields(*target);
    return {true, QString("%1回复%2点生命").arg(target->name).arg(target->hp - before)};
}

CardEffectResult BattleRuntime::addShield(const QString& unitKey, int amount) {
    BattleUnit* target = unitByKey(unitKey);
    if (!target) return {false, "目标无效"};
    target->shield += qMax(0, amount);
    syncAttributesFromFields(*target);
    return {true, QString("%1获得%2点护盾").arg(target->name).arg(qMax(0, amount))};
}

CardEffectResult BattleRuntime::addEnergy(const QString& unitKey, int amount) {
    BattleUnit* target = unitByKey(unitKey);
    if (!target) return {false, "目标无效"};
    const int before = target->energy;
    target->energy = qMin(target->maxEnergy, target->energy + qMax(0, amount));
    syncAttributesFromFields(*target);
    return {true, QString("%1恢复%2点能量").arg(target->name).arg(target->energy - before)};
}

CardEffectResult BattleRuntime::addTag(const QString& unitKey, const QString& tag) {
    return addStatus(unitKey, tag, 1, 0);
}

CardEffectResult BattleRuntime::addStatus(const QString& unitKey, const QString& tag, int stacks, int turns) {
    BattleUnit* target = unitByKey(unitKey);
    const QString t = tag.trimmed();
    if (!target || t.isEmpty()) return {false, "状态无效"};
    target->tags.insert(t);
    for (GameplayStatus& status : target->statuses) {
        if (status.tag != t) continue;
        status.stacks = qMax(1, status.stacks + qMax(1, stacks));
        status.remainingTurns = qMax(status.remainingTurns, qMax(0, turns));
        return {true, QString("%1获得状态%2(%3层)").arg(target->name, t).arg(status.stacks)};
    }
    target->statuses.append({t, qMax(1, stacks), qMax(0, turns)});
    return {true, QString("%1获得状态%2").arg(target->name, t)};
}

CardEffectResult BattleRuntime::removeTag(const QString& unitKey, const QString& tag) {
    BattleUnit* target = unitByKey(unitKey);
    const QString t = tag.trimmed();
    if (!target || t.isEmpty()) return {false, "状态无效"};
    target->tags.remove(t);
    target->statuses.removeIf([&](const GameplayStatus& status) { return status.tag == t; });
    return {true, QString("%1移除状态%2").arg(target->name, t)};
}

CardEffectResult BattleRuntime::setStatusStacks(const QString& unitKey, const QString& tag, int stacks) {
    BattleUnit* target = unitByKey(unitKey);
    const QString t = tag.trimmed();
    if (!target || t.isEmpty()) return {false, "状态无效"};
    if (stacks <= 0) return removeTag(unitKey, tag);
    target->tags.insert(t);
    for (GameplayStatus& status : target->statuses) {
        if (status.tag == t) {
            status.stacks = stacks;
            return {true, QString("%1状态%2变为%3层").arg(target->name, t).arg(stacks)};
        }
    }
    target->statuses.append({t, stacks, 0});
    return {true, QString("%1获得状态%2(%3层)").arg(target->name, t).arg(stacks)};
}

bool BattleRuntime::hasTag(const QString& unitKey, const QString& tag) const {
    const QString key = unitKey.trimmed().toLower();
    const BattleUnit* target = (key == "enemy" || key == "敌人" || key == "敌方") ? &m_enemy : &m_player;
    return target->tags.contains(tag.trimmed());
}

CardEffectResult BattleRuntime::endTurn() {
    if (!m_active) return {false, "战斗未开始"};
    if (m_ended) return {false, "战斗已结束"};

    m_activeTeam = "敌方";
    const int dealt = applyDamage(m_player, 6);
    checkEndState();
    if (m_ended) return {true, QString("敌方造成%1点伤害").arg(dealt)};

    discardHand();
    m_player.shield = 0;
    tickStatuses(m_player);
    tickStatuses(m_enemy);
    m_player.energy = m_player.maxEnergy;
    syncAttributesFromFields(m_player);
    m_activeTeam = "玩家";
    drawCards(4);
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
    syncAttributesFromFields(target);
    return damage;
}

void BattleRuntime::clampUnits() {
    m_player.hp = qBound(0, m_player.hp, m_player.maxHp);
    m_enemy.hp = qBound(0, m_enemy.hp, m_enemy.maxHp);
    m_player.energy = qBound(0, m_player.energy, m_player.maxEnergy);
    m_player.shield = qMax(0, m_player.shield);
    m_enemy.shield = qMax(0, m_enemy.shield);
    syncAttributesFromFields(m_player);
    syncAttributesFromFields(m_enemy);
}

void BattleRuntime::syncFieldsFromAttributes(BattleUnit& unit) {
    unit.hp = unit.attributes.hp;
    unit.maxHp = unit.attributes.maxHp;
    unit.shield = unit.attributes.shield;
    unit.energy = unit.attributes.energy;
    unit.maxEnergy = unit.attributes.maxEnergy;
}

void BattleRuntime::syncAttributesFromFields(BattleUnit& unit) {
    unit.attributes.hp = unit.hp;
    unit.attributes.maxHp = unit.maxHp;
    unit.attributes.shield = unit.shield;
    unit.attributes.energy = unit.energy;
    unit.attributes.maxEnergy = unit.maxEnergy;
}

void BattleRuntime::drawCards(int count) {
    for (int i = 0; i < count; ++i) {
        if (m_deck.isEmpty()) recycleDiscardIntoDeck();
        if (m_deck.isEmpty()) return;
        BattleCard card = m_deck.takeFirst();
        card.used = false;
        m_hand.append(card);
    }
}

void BattleRuntime::discardHand() {
    for (BattleCard card : std::as_const(m_hand)) {
        card.used = true;
        m_discard.append(card);
    }
    m_hand.clear();
}

void BattleRuntime::recycleDiscardIntoDeck() {
    if (m_discard.isEmpty()) return;
    for (BattleCard card : std::as_const(m_discard)) {
        card.used = false;
        m_deck.append(card);
    }
    m_discard.clear();
}

void BattleRuntime::tickStatuses(BattleUnit& unit) {
    for (int i = unit.statuses.size() - 1; i >= 0; --i) {
        GameplayStatus& status = unit.statuses[i];
        if (status.remainingTurns <= 0) continue;
        status.remainingTurns -= 1;
        if (status.remainingTurns <= 0) {
            unit.tags.remove(status.tag);
            unit.statuses.removeAt(i);
        }
    }
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
