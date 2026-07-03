#include "BPRuntime.h"
#include "UIRuntime.h"
#include <QSet>
#include <QDebug>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QUuid>
#include <QDir>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QColor>
#include "BPEval.h"
#include "models/BPMacro.h"
#include <algorithm>
#include <cmath>

static std::pair<QString,QString> splitWidgetRef(const QString& ref) {
    int sep = ref.indexOf("::");
    if (sep < 0) return {ref, {}};
    return {ref.left(sep), ref.mid(sep + 2)};
}

void BPRuntime::setUIRuntime(UIRuntime* ui) {
    m_uiRuntime = ui;
    if (!ui) return;
    connect(ui, &UIRuntime::buttonClicked,
            this, &BPRuntime::triggerButtonClick);
    connect(ui, &UIRuntime::dropdownChanged,
            this, &BPRuntime::triggerDropdownChanged);
    connect(ui, &UIRuntime::dragStarted,
            this, &BPRuntime::triggerUIDragStarted);
    connect(ui, &UIRuntime::dragMoved,
            this, &BPRuntime::triggerUIDragMoved);
    connect(ui, &UIRuntime::dropped,
            this, &BPRuntime::triggerUIDropped);
    connect(ui, &UIRuntime::dragCanceled,
            this, &BPRuntime::triggerUIDragCanceled);
}

BPRuntime::BPRuntime(const LevelDocument* doc, QObject* parent)
    : QObject(parent)
{
    m_battle = std::make_unique<BattleRuntime>();
    if (!doc) return;
    m_nodes       = doc->bpNodes();
    m_connections = doc->bpConnections();
    m_actors      = doc->actors();

    // 局部变量：每次运行初始化为类型零值（数组=空数组）
    for (const GlobalVarDef& d : doc->localVars()) {
        if      (d.type == "number")          m_varStore[d.name] = BPValue::fromNumber(0);
        else if (d.type == "bool")            m_varStore[d.name] = BPValue::fromBool(false);
        else if (d.type.startsWith("array:")) m_varStore[d.name] = BPValue::fromArray({});
        else                                  m_varStore[d.name] = BPValue::fromString("");
    }

    // 工程根：关卡在 {project}/Levels/x.level，上溯一级
    QString projectRoot;
    if (!doc->filePath().isEmpty()) {
        QDir d(QFileInfo(doc->filePath()).absolutePath());
        d.cdUp();
        projectRoot = d.absolutePath();
    }
    flattenMacros(projectRoot);

    m_tickTimer = new QTimer(this);
    m_tickTimer->setInterval(16);
    connect(m_tickTimer, &QTimer::timeout, this, &BPRuntime::tick);
    m_tickTimer->start();
    m_elapsedTimer.start();
}

void BPRuntime::flattenMacros(const QString& projectRoot) {
    const QList<BPMacro> libMacros =
        projectRoot.isEmpty() ? QList<BPMacro>{} : BPMacro::listAll(projectRoot);

    auto removeNodeAndConns = [&](const QString& id) {
        m_nodes.removeIf([&](const BPNode& n){ return n.id == id; });
        m_connections.removeIf([&](const BPConnection& c){
            return c.fromNode == id || c.toNode == id; });
    };

    // 反复展开，直到没有 Macro:: 调用节点（支持嵌套）；上限防环
    for (int guard = 0; guard < 2000; ++guard) {
        int idx = -1;
        for (int i = 0; i < m_nodes.size(); ++i)
            if (m_nodes[i].type.startsWith("Macro::")) { idx = i; break; }
        if (idx < 0) break;

        const BPNode call = m_nodes[idx];

        // 解析宏：本地折叠取自 params；库宏按 id 查
        BPMacro macro; bool okMacro = false;
        if (call.type == "Macro::local") {
            const QString sub = call.params.value("subgraph");
            if (!sub.isEmpty()) {
                macro = BPMacro::fromJson(QJsonDocument::fromJson(sub.toUtf8()).object());
                okMacro = true;
            }
        } else {
            const QString id = call.type.mid(7);
            for (const BPMacro& m : libMacros)
                if (m.id == id) { macro = m; okMacro = true; break; }
        }
        if (!okMacro) { removeNodeAndConns(call.id); continue; }

        // 入口/出口 + 接口映射
        QString entryId, exitId;
        for (const BPNode& n : macro.nodes) {
            if (n.type == "Macro.Entry") entryId = n.id;
            else if (n.type == "Macro.Exit") exitId = n.id;
        }
        QMap<QString, QPair<QString,QString>> inMap, outMap;  // pinKey → (insideNode, insidePin)
        for (const BPConnection& c : macro.connections) {
            if (c.fromNode == entryId) inMap[c.fromPin] = {c.toNode, c.toPin};
            if (c.toNode   == exitId)  outMap[c.toPin]  = {c.fromNode, c.fromPin};
        }

        // 内部节点 id 重映射（排除入口/出口）
        QMap<QString,QString> idMap;
        for (const BPNode& n : macro.nodes)
            if (n.id != entryId && n.id != exitId)
                idMap[n.id] = QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto mapId = [&](const QString& id){ return idMap.value(id, id); };

        // 插入内部节点
        for (const BPNode& n : macro.nodes) {
            if (n.id == entryId || n.id == exitId) continue;
            BPNode nn = n; nn.id = idMap[n.id];
            m_nodes.append(nn);
        }
        // 插入内部连线（排除连入口/出口的）
        for (const BPConnection& c : macro.connections) {
            if (c.fromNode == entryId || c.toNode == exitId) continue;
            if (!idMap.contains(c.fromNode) || !idMap.contains(c.toNode)) continue;
            m_connections.append({QUuid::createUuid().toString(QUuid::WithoutBraces),
                                  mapId(c.fromNode), c.fromPin, mapId(c.toNode), c.toPin});
        }
        // 外部连线重定向到内部端点
        for (BPConnection& c : m_connections) {
            if (c.toNode == call.id) {
                auto it = inMap.find(c.toPin);
                if (it != inMap.end()) { c.toNode = mapId(it.value().first); c.toPin = it.value().second; }
            }
            if (c.fromNode == call.id) {
                auto it = outMap.find(c.fromPin);
                if (it != outMap.end()) { c.fromNode = mapId(it.value().first); c.fromPin = it.value().second; }
            }
        }
        // 移除 call 及其残余连线（未映射的引脚）
        removeNodeAndConns(call.id);
    }
}

void BPRuntime::tick() {
    m_lastDt = m_elapsedTimer.restart() / 1000.0f;
    const float dt = m_lastDt;
    // 捕获本帧移动前的位置，供碰撞 pass 分轴回退（移动发生在 triggerTick 与各 Actor 蓝图 tick）
    for (ActorData& a : m_actors) { a.prevX = a.x; a.prevY = a.y; }
    tickComponents(dt);
    triggerTick(dt);
    advanceAnimations(dt);
    emit stateChanged();
    // 注：runCollisionPass() 由 EditorWindow 在各 Actor 蓝图 tick 之后调用
}

// AABB 相交（中心 + 半宽半高）
static inline bool aabbHit(float ax, float ay, float ahw, float ahh,
                           float bx, float by, float bhw, float bhh) {
    return std::abs(ax - bx) < (ahw + bhw) && std::abs(ay - by) < (ahh + bhh);
}

void BPRuntime::runCollisionPass() {
    auto enabled = [](const ActorData& a) {
        return a.colliderEnabled && a.components.contains("碰撞盒");
    };
    auto targetsMatch = [](const ActorData& A, const ActorData& B) {
        const QString t = A.colliderTargets.trimmed();
        if (t.isEmpty()) return B.tag != A.tag;                 // 空 = 除自身标签外所有
        for (const QString& tag : t.split(',', Qt::SkipEmptyParts))
            if (tag.trimmed() == B.tag) return true;
        return false;
    };

    // —— 分轴阻挡解析：X、Y 各独立回退 ——
    for (ActorData& A : m_actors) {
        if (!enabled(A)) continue;
        const float hw = A.colliderW * 0.5f, hh = A.colliderH * 0.5f;
        auto hitsBlocker = [&](float testX, float testY) -> bool {
            const float acx = testX + A.colliderOffsetX, acy = testY + A.colliderOffsetY;
            for (const ActorData& B : m_actors) {
                if (&B == &A || !enabled(B) || B.colliderResponse != "阻挡" || !targetsMatch(A, B)) continue;
                if (aabbHit(acx, acy, hw, hh,
                            B.x + B.colliderOffsetX, B.y + B.colliderOffsetY,
                            B.colliderW * 0.5f, B.colliderH * 0.5f))
                    return true;
            }
            return false;
        };
        float rx = hitsBlocker(A.x, A.prevY) ? A.prevX : A.x;   // 先解 X（Y 用旧值）
        float ry = hitsBlocker(rx,  A.y)     ? A.prevY : A.y;   // 再解 Y（X 用新值）
        A.x = rx; A.y = ry;
    }

    // —— 接触事件：按目标标签检测（不分响应类型），仅「刚接触」那帧触发一次 ——
    for (ActorData& A : m_actors) {
        if (!enabled(A)) continue;
        const float hw = A.colliderW * 0.5f, hh = A.colliderH * 0.5f;
        const float acx = A.x + A.colliderOffsetX, acy = A.y + A.colliderOffsetY;
        QSet<QString> cur;
        for (const ActorData& B : m_actors) {
            if (&B == &A || !enabled(B) || !targetsMatch(A, B)) continue;
            if (aabbHit(acx, acy, hw, hh,
                        B.x + B.colliderOffsetX, B.y + B.colliderOffsetY,
                        B.colliderW * 0.5f, B.colliderH * 0.5f)) {
                cur.insert(B.id);
                if (!m_overlapState[A.id].contains(B.id)) {  // 新接触 → 触发一次
                    triggerCollision(A.id, B.id, B.tag);     // 关卡蓝图「碰撞时」
                    emit overlapDetected(A.id, B.id, B.tag); // 转发给各 Actor 蓝图
                }
            }
        }
        m_overlapState[A.id] = cur;
    }
}

void BPRuntime::triggerCollision(const QString& selfId, const QString& otherId, const QString& otherTag) {
    m_collSelf = selfId; m_collOther = otherId; m_collTag = otherTag;
    for (const BPNode& node : m_nodes) {
        if (node.type != "Event.OnCollision") continue;
        QSet<QString> v1; executeChain(node.id, "case_" + otherTag, &v1);  // 按对方标签分路
        QSet<QString> v2; executeChain(node.id, "exec_out",        &v2);  // 无目标标签时的通用出口
    }
}

const AnimationAsset& BPRuntime::animAssetFor(const QString& path) {
    auto it = m_animCache.find(path);
    if (it == m_animCache.end()) {
        AnimationAsset as;
        as.load(path);
        it = m_animCache.insert(path, as);
    }
    return it.value();
}

// 把 Actor 的当前片段 + 帧序号解析为可绘制的精灵表 + 源矩形
static void applyAnimFrame(ActorData& a, const AnimationAsset& as, const AnimClip* clip) {
    if (!clip || as.sheet.isEmpty()) { a.animSheetPath.clear(); return; }
    a.animFrameIndex = qBound(0, a.animFrameIndex, qMax(0, clip->frameCount - 1));
    a.animSheetPath  = as.sheet;
    a.animSrc        = as.frameRect(*clip, a.animFrameIndex);
}

void BPRuntime::initAnimations() {
    for (ActorData& a : m_actors) {
        if (!a.components.contains("动画器") || a.animAsset.isEmpty()
            || a.animDefaultClip.isEmpty()) continue;
        a.animCurClip    = a.animDefaultClip;
        a.animFrameIndex = 0;
        a.animTimeAccum  = 0.0;
        a.animPlaying    = a.animAutoPlay;
        const AnimationAsset& as = animAssetFor(a.animAsset);
        applyAnimFrame(a, as, as.findClip(a.animCurClip));
    }
}

void BPRuntime::reapplyAnimFrame(ActorData& a) {
    if (a.animAsset.isEmpty() || a.animCurClip.isEmpty()) { a.animSheetPath.clear(); return; }
    const AnimationAsset& as = animAssetFor(a.animAsset);
    applyAnimFrame(a, as, as.findClip(a.animCurClip));   // 切素材后立即刷新可见帧（站立不动时也生效）
}

void BPRuntime::advanceAnimations(float dt) {
    for (ActorData& a : m_actors) {
        if (!a.animPlaying || a.animAsset.isEmpty() || a.animCurClip.isEmpty()) continue;
        const AnimationAsset& as = animAssetFor(a.animAsset);
        const AnimClip* clip = as.findClip(a.animCurClip);
        if (!clip || clip->frameCount <= 0 || as.sheet.isEmpty()) continue;

        const double frameDur = clip->fps > 0.0 ? 1.0 / clip->fps : 0.1;
        a.animTimeAccum += dt;
        while (a.animTimeAccum >= frameDur) {
            a.animTimeAccum -= frameDur;
            a.animFrameIndex++;
            if (a.animFrameIndex >= clip->frameCount) {
                if (clip->loop) {
                    a.animFrameIndex = 0;
                } else {
                    a.animFrameIndex = clip->frameCount - 1;
                    a.animPlaying = false;
                    break;
                }
            }
        }
        applyAnimFrame(a, as, clip);
    }
}

void BPRuntime::tickComponents(float dt) {
    // Pass 1：跟随控制组件 — 摄像机向目标插值
    for (ActorData& a : m_actors) {
        if (!a.components.contains("跟随控制组件")) continue;
        if (a.followTarget.isEmpty()) continue;
        const ActorData* target = findActorByName(a.followTarget);
        if (!target) continue;

        const float destX = target->x + a.followOffsetX;
        const float destY = target->y + a.followOffsetY;
        const float t     = 1.0f - std::exp(-a.followLerpSpeed * dt);
        a.x += (destX - a.x) * t;
        a.y += (destY - a.y) * t;
    }

    // Pass 2：边界限制组件 — clamp 摄像机位置
    for (ActorData& a : m_actors) {
        if (!a.components.contains("边界限制组件")) continue;
        if (!a.confinerEnabled) continue;

        float minX = a.confinerMinX, maxX = a.confinerMaxX;
        float minY = a.confinerMinY, maxY = a.confinerMaxY;

        // 优先从绑定的 Trigger Actor 推算边界矩形
        if (!a.confinerActor.isEmpty()) {
            const ActorData* t = findActorByName(a.confinerActor);
            if (t) {
                minX = t->x - t->scaleX * 0.5f;
                maxX = t->x + t->scaleX * 0.5f;
                minY = t->y - t->scaleY * 0.5f;
                maxY = t->y + t->scaleY * 0.5f;
            }
        }

        a.x = std::clamp(a.x, minX, maxX);
        a.y = std::clamp(a.y, minY, maxY);
    }
}

void BPRuntime::triggerTick(float dt) {
    m_deltaTick = dt;
    for (const BPNode& node : m_nodes) {
        if (node.type == "Event.Tick") {
            QSet<QString> visited;
            executeChain(node.id, "exec_out", &visited);
        }
    }
    // 持续按住：对每个被按住的键，每帧驱动其按键节点的 held 链
    for (const QString& key : m_heldKeys) {
        const QString typeId = "Event.Key." + key;
        for (const BPNode& node : m_nodes) {
            if (node.type == typeId) {
                QSet<QString> visited;
                executeChain(node.id, "held", &visited);
            }
        }
    }
}

void BPRuntime::triggerBeginPlay() {
    initAnimations();
    for (const BPNode& node : m_nodes) {
        if (node.type == "Event.BeginPlay") {
            QSet<QString> visited;
            executeChain(node.id, "exec_out", &visited);
        }
    }
    emit stateChanged();
}

void BPRuntime::triggerKeyDown(const QString& key) {
    m_heldKeys.insert(key);
    const QString typeId = "Event.Key." + key;
    for (const BPNode& node : m_nodes) {
        if (node.type == typeId) {
            QSet<QString> visited;
            executeChain(node.id, "pressed", &visited);
        }
    }
    emit stateChanged();
}

void BPRuntime::triggerKeyUp(const QString& key) {
    m_heldKeys.remove(key);
    const QString typeId = "Event.Key." + key;
    for (const BPNode& node : m_nodes) {
        if (node.type == typeId) {
            QSet<QString> visited;
            executeChain(node.id, "released", &visited);
        }
    }
    emit stateChanged();
}

void BPRuntime::executeChain(const QString& fromNodeId, const QString& fromPin,
                              QSet<QString>* visited) {
    // 用 "nodeId:pin" 作为访问键，防止循环蓝图导致无限递归
    const QString key = fromNodeId + QLatin1Char(':') + fromPin;
    if (visited && visited->contains(key)) return;
    if (visited) visited->insert(key);

    // exec 输出支持扇出：跟随该出口的所有连线，依次执行
    const QList<BPConnection> conns = m_connections;   // 拷贝，防执行中修改
    for (const BPConnection& c : conns) {
        if (c.fromNode == fromNodeId && c.fromPin == fromPin) {
            QString nextPin = executeNode(c.toNode);
            if (!nextPin.isEmpty())
                executeChain(c.toNode, nextPin, visited);
        }
    }
}

QString BPRuntime::executeNode(const QString& nodeId) {
    const BPNode* node = findNode(nodeId);
    if (!node) return {};

    if (node->type == "Action.Print") {
        appendPrintLog(resolveDataPin(nodeId, "text"));
        return "exec_out";
    }

    if (node->type == "Global.Set") {
        if (m_globalVars) {
            const QString name = node->params.value("varName");
            if (!name.isEmpty()) (*m_globalVars)[name] = resolveDataPin(nodeId, "value");
        }
        return "exec_out";
    }

    if (node->type == "Local.Set") {
        const QString name = node->params.value("varName");
        if (!name.isEmpty()) m_varStore[name] = resolveDataPin(nodeId, "value");
        return "exec_out";
    }

    if (node->type == "Var.SetNumber" || node->type == "Var.SetBool" || node->type == "Var.SetString") {
        QString name  = resolveDataPin(nodeId, "name");
        QString value = resolveDataPin(nodeId, "value");
        if (!name.isEmpty()) m_varStore[name] = value;
        return "exec_out";
    }

    // 数组变量操作（exec）：读全局数组 → 改 → 写回
    if (node->type == "Array.Add"       || node->type == "Array.RemoveAt"
     || node->type == "Array.RemoveValue" || node->type == "Array.SetAt"
     || node->type == "Array.Clear") {
        const QString name  = node->params.value("varName");
        const bool    local = node->params.value("scope") == "local";  // 否则全局
        if (!name.isEmpty() && (local || m_globalVars)) {
            BPValue cur = local ? m_varStore.value(name) : m_globalVars->value(name);
            QList<BPValue>& arr = cur.arrayRef();
            if (node->type == "Array.Add") {
                arr.append(resolveDataPin(nodeId, "value"));
            } else if (node->type == "Array.Clear") {
                arr.clear();
            } else if (node->type == "Array.RemoveAt") {
                const int i = (int)resolveDataPin(nodeId, "index").toNumber();
                if (i >= 0 && i < arr.size()) arr.removeAt(i);
            } else if (node->type == "Array.RemoveValue") {
                const BPValue v = resolveDataPin(nodeId, "value");
                for (int i = 0; i < arr.size(); ++i)
                    if (arr[i].typedEquals(v)) { arr.removeAt(i); break; }
            } else if (node->type == "Array.SetAt") {
                const int i = (int)resolveDataPin(nodeId, "index").toNumber();
                if (i >= 0 && i < arr.size()) arr[i] = resolveDataPin(nodeId, "value");
            }
            if (local) m_varStore[name] = cur;
            else       (*m_globalVars)[name] = cur;
        }
        return "exec_out";
    }

    // 遍历(ForEach)：对数组快照逐元素执行循环体（每次迭代独立 visited），完成后走 completed
    if (node->type == "Flow.ForEach") {
        const QList<BPValue> arr = resolveDataPin(nodeId, "array").toArray();  // 快照
        for (int i = 0; i < arr.size(); ++i) {
            m_loopState[nodeId] = {arr[i], i};
            QSet<QString> bodyVisited;                  // 每次迭代独立上下文，循环体可重入
            executeChain(nodeId, "loop_body", &bodyVisited);
        }
        m_loopState.remove(nodeId);
        return "completed";
    }

    if (node->type == "Action.MoveActor") {
        QString actorId = resolveDataPin(nodeId, "actorId");
        float dx = resolveDataPin(nodeId, "dx").toString().toFloat();
        float dy = resolveDataPin(nodeId, "dy").toString().toFloat();
        for (ActorData& a : m_actors) {
            if (a.id == actorId) {
                a.x += dx;
                a.y += dy;
                break;
            }
        }
        return "exec_out";
    }

    if (node->type == "Action.LoadLevel") {
        const QString levelName = resolveDataPin(nodeId, "levelName");
        if (!levelName.isEmpty())
            emit loadLevelRequested(levelName);
        return {};
    }

    if (node->type == "Action.BackLevel") {
        emit backLevelRequested();
        return {};
    }

    if (node->type == "Action.SetActive") {
        QString actorId = resolveDataPin(nodeId, "actorId");
        QString val     = resolveDataPin(nodeId, "active").toString().toLower();
        bool active = (val == "true" || val == "1");
        for (ActorData& a : m_actors) {
            if (a.id == actorId) { a.active = active; break; }
        }
        return "exec_out";
    }

    if (node->type == "Battle.Create") {
        if (!m_battle) m_battle = std::make_unique<BattleRuntime>();
        const int playerHp  = qMax(1, (int)resolveDataPin(nodeId, "playerHp").toNumber());
        const int enemyHp   = qMax(1, (int)resolveDataPin(nodeId, "enemyHp").toNumber());
        const int energy    = qMax(0, (int)resolveDataPin(nodeId, "energy").toNumber());
        const int maxEnergy = qMax(1, (int)resolveDataPin(nodeId, "maxEnergy").toNumber());
        m_battle->startDefault(playerHp, enemyHp, energy, maxEnergy);
        m_lastBattleMessage = "战斗开始";
        m_battleEndNotified = false;
        return "exec_out";
    }

    if (node->type == "Battle.UseCard") {
        if (!m_battle) return "exec_out";
        const int cardIndex = (int)resolveDataPin(nodeId, "cardIndex").toNumber();
        const CardEffectResult r = m_battle->useCard(cardIndex);
        m_lastBattleMessage = r.message;
        if (m_battle->ended()) triggerBattleEnded();
        return "exec_out";
    }

    if (node->type == "Battle.EndTurn") {
        if (!m_battle) return "exec_out";
        const CardEffectResult r = m_battle->endTurn();
        m_lastBattleMessage = r.message;
        if (m_battle->ended()) triggerBattleEnded();
        return "exec_out";
    }

    if (node->type == "Flow.Branch") {
        QString cond = resolveDataPin(nodeId, "condition").toString().toLower();
        bool truthy = !cond.isEmpty() && cond != "0" && cond != "false";
        return truthy ? "true" : "false";
    }

    if (node->type == "Flow.Switch") {
        // 取「值」输入，逐个分支比较；每分支比较值可连变量(caseval_<id>)或用字面量
        const QString v = resolveDataPin(nodeId, "value");
        const QJsonDocument d = QJsonDocument::fromJson(node->params.value("branches").toUtf8());
        if (d.isArray()) {
            for (const QJsonValue& bv : d.array()) {
                const QJsonObject o = bv.toObject();
                const QString id = o.value("id").toString();
                if (id.isEmpty()) continue;
                QString cmp = resolveDataPin(nodeId, "caseval_" + id);     // 连线 or params 字面量
                if (cmp.isEmpty()) cmp = o.value("value").toString();      // 兼容旧数据
                if (cmp == v) return "case_" + id;
            }
        }
        const QString hd = node->params.value("hasDefault").toLower();
        if (hd == "true" || hd == "1") return "default";
        return {};
    }

    auto uiRef = [&](const QString& pinKey) -> QString {
        QString v = resolveDataPin(nodeId, pinKey);
        if (v.isEmpty()) v = node->params.value("uiName");
        return v;
    };
    if (node->type == "UI.Create") {
        if (!m_uiRuntime) return "exec_out";
        m_uiRefs[nodeId] = m_uiRuntime->createInstance(splitWidgetRef(uiRef("widgetRef")).first);
        return "exec_out";
    }
    if (node->type == "UI.Show") {
        if (m_uiRuntime) {
            auto [uiName, widgetName] = splitWidgetRef(uiRef("widgetRef"));
            m_uiRuntime->showWidgetByName(uiName, widgetName);
        }
        return "exec_out";
    }
    if (node->type == "UI.Hide") {
        if (m_uiRuntime) {
            auto [uiName, widgetName] = splitWidgetRef(uiRef("widgetRef"));
            m_uiRuntime->hideWidgetByName(uiName, widgetName);
        }
        return "exec_out";
    }
    if (node->type == "UI.Destroy") {
        if (m_uiRuntime) {
            m_uiRuntime->destroyByName(splitWidgetRef(uiRef("widgetRef")).first);
            m_uiRefs.remove(nodeId);
        }
        return "exec_out";
    }
    if (node->type == "UI.SetText") {
        if (m_uiRuntime) {
            auto [ui, widget] = splitWidgetRef(resolveDataPin(nodeId, "widgetRef"));
            m_uiRuntime->setTextByName(ui, widget, resolveDataPin(nodeId, "text"));
        }
        return "exec_out";
    }
    if (node->type == "UI.SetValue") {
        if (m_uiRuntime) {
            auto [ui, widget] = splitWidgetRef(resolveDataPin(nodeId, "widgetRef"));
            m_uiRuntime->setValueByName(ui, widget, resolveDataPin(nodeId, "value").toString().toFloat());
        }
        return "exec_out";
    }
    if (node->type == "UI.SetPosition") {
        if (m_uiRuntime)
            m_uiRuntime->setPositionByName(splitWidgetRef(resolveDataPin(nodeId, "widgetRef")).first,
                                           resolveDataPin(nodeId, "x").toString().toFloat(),
                                           resolveDataPin(nodeId, "y").toString().toFloat());
        return "exec_out";
    }
    if (node->type == "UI.SetVisible") {
        if (m_uiRuntime) {
            auto [ui, widget] = splitWidgetRef(resolveDataPin(nodeId, "widgetRef"));
            const QString val = resolveDataPin(nodeId, "visible").toString().toLower();
            m_uiRuntime->setWidgetVisibleByName(ui, widget,
                                                !val.isEmpty() && val != "0" && val != "false");
        }
        return "exec_out";
    }
    if (node->type == "UI.SetScroll" || node->type == "UI.ScrollTo") {
        if (m_uiRuntime) {
            auto [ui, widget] = splitWidgetRef(resolveDataPin(nodeId, "widgetRef"));
            m_uiRuntime->setScrollByName(ui, widget,
                                         resolveDataPin(nodeId, "x").toNumber(),
                                         resolveDataPin(nodeId, "y").toNumber());
        }
        return "exec_out";
    }
    if (node->type == "UI.SetColor") {
        if (m_uiRuntime) {
            auto [ui, widget] = splitWidgetRef(resolveDataPin(nodeId, "widgetRef"));
            const int r = qBound(0, (int)resolveDataPin(nodeId, "r").toNumber(), 255);
            const int g = qBound(0, (int)resolveDataPin(nodeId, "g").toNumber(), 255);
            const int b = qBound(0, (int)resolveDataPin(nodeId, "b").toNumber(), 255);
            const int a = qBound(0, (int)resolveDataPin(nodeId, "a").toNumber(), 255);
            m_uiRuntime->setWidgetColorByName(ui, widget, QColor(r, g, b, a));
        }
        return "exec_out";
    }
    if (node->type == "UI.SetAlpha") {
        if (m_uiRuntime) {
            auto [ui, widget] = splitWidgetRef(resolveDataPin(nodeId, "widgetRef"));
            m_uiRuntime->setWidgetAlphaByName(ui, widget, resolveDataPin(nodeId, "alpha").toNumber());
        }
        return "exec_out";
    }
    if (node->type == "UI.SetSize") {
        if (m_uiRuntime) {
            auto [ui, widget] = splitWidgetRef(resolveDataPin(nodeId, "widgetRef"));
            m_uiRuntime->setWidgetSizeByName(ui, widget,
                                             resolveDataPin(nodeId, "width").toNumber(),
                                             resolveDataPin(nodeId, "height").toNumber());
        }
        return "exec_out";
    }
    if (node->type == "UI.Follow") {
        if (m_uiRuntime) {
            const QString ref     = splitWidgetRef(resolveDataPin(nodeId, "widgetRef")).first;
            const QString actorId = resolveDataPin(nodeId, "actorId");
            const float   up      = resolveDataPin(nodeId, "offsetUp").toString().toFloat();
            m_uiRuntime->setFollowActorRef(ref, actorId, 0.0f, -up);
        }
        return "exec_out";
    }

    return {};
}

BPValue BPRuntime::resolveDataPin(const QString& nodeId, const QString& pinKey) {
    for (const BPConnection& c : m_connections) {
        if (c.toNode == nodeId && c.toPin == pinKey)
            return resolveOutputPin(c.fromNode, c.fromPin);
    }
    const BPNode* node = findNode(nodeId);
    return node ? node->params.value(pinKey) : QString();
}

BPValue BPRuntime::resolveOutputPin(const QString& nodeId, const QString& pinKey) {
    const BPNode* node = findNode(nodeId);
    if (!node) return {};

    if (node->type == "Var.ActorRef")
        return node->params.value("actorId");

    if (node->type == "UI.Create")
        return m_uiRefs.value(nodeId);
    if (node->type == "UI.Ref") {
        const QString uiName = node->params.value("uiName");
        return uiName + "::" + pinKey;  // 所有控件引脚返回 "uiName::widgetName"
    }

    // 全局变量读取
    if (node->type == "Global.Get")
        return m_globalVars ? m_globalVars->value(node->params.value("varName")) : BPValue();

    if (node->type == "Local.Get")
        return m_varStore.value(node->params.value("varName"));

    // 运行时变量读取（数值/布尔/字符串共用同一张表）
    if (node->type == "Var.GetNumber" || node->type == "Var.GetBool" || node->type == "Var.GetString") {
        QString name = resolveDataPin(nodeId, "name");
        return m_varStore.value(name);
    }

    // ── 纯数据节点（数学/比较/逻辑/数组/转换）：共享求值器，两套运行时一致 ──
    {
        BPValue out;
        if (evalPureDataNode(node->type, pinKey,
                [&](const QString& pk){ return resolveDataPin(nodeId, pk); }, out))
            return out;
    }

    // 遍历(ForEach) 的循环体输出：当前元素 / 当前索引（仅迭代期间有效）
    if (node->type == "Flow.ForEach") {
        auto it = m_loopState.constFind(nodeId);
        if (it != m_loopState.constEnd()) {
            if (pinKey == "current") return it->element;
            if (pinKey == "index")   return BPValue::fromNumber(it->index);
        }
        return BPValue();
    }

    if (node->type == "Var.GetActorPos") {
        QString actorId = resolveDataPin(nodeId, "actorId");
        for (const ActorData& a : m_actors) {
            if (a.id == actorId) {
                if (pinKey == "x") return QString::number(a.x);
                if (pinKey == "y") return QString::number(a.y);
            }
        }
    }

    if (node->type == "Event.Tick")
        return (pinKey == "delta_time") ? QString::number(m_deltaTick) : QString();

    if (node->type == "Event.OnCollision") {
        if (pinKey == "self")  return m_collSelf;
        if (pinKey == "other") return m_collOther;
        if (pinKey == "tag")   return m_collTag;
    }

    if (node->type == "Event.MouseDown" || node->type == "Event.MouseUp"
     || node->type == "Event.MouseMove" || node->type == "Event.MouseDrag"
     || node->type == "Event.MouseWheel") {
        const MouseState s = m_mouseState.value(nodeId);
        if (pinKey == "screenX") return BPValue::fromNumber(s.screenX);
        if (pinKey == "screenY") return BPValue::fromNumber(s.screenY);
        if (pinKey == "worldX") return BPValue::fromNumber(s.worldX);
        if (pinKey == "worldY") return BPValue::fromNumber(s.worldY);
        if (pinKey == "deltaX") return BPValue::fromNumber(s.deltaX);
        if (pinKey == "deltaY") return BPValue::fromNumber(s.deltaY);
        if (pinKey == "button") return s.button;
    }

    if (node->type == "Event.BattleEnded") {
        if (!m_battle) return {};
        if (pinKey == "result") return m_battle->result();
        if (pinKey == "reason") return m_battle->reason();
    }

    if (node->type == "Battle.GetUnitStatus") {
        const BattleUnit* unit = battleUnitByKey(resolveDataPin(nodeId, "unit").toString());
        if (!unit) return {};
        if (pinKey == "hp") return BPValue::fromNumber(unit->hp);
        if (pinKey == "maxHp") return BPValue::fromNumber(unit->maxHp);
        if (pinKey == "shield") return BPValue::fromNumber(unit->shield);
        if (pinKey == "energy") return BPValue::fromNumber(unit->energy);
        if (pinKey == "maxEnergy") return BPValue::fromNumber(unit->maxEnergy);
        if (pinKey == "alive") return BPValue::fromBool(unit->alive());
    }

    if (node->type == "Battle.GetHand") {
        if (!m_battle || !m_battle->active()) return {};
        if (pinKey == "count") return BPValue::fromNumber(m_battle->hand().size());
        if (pinKey == "text") return m_battle->handText();
    }

    if (node->type == "Battle.GetResult") {
        if (!m_battle) return {};
        if (pinKey == "ended") return BPValue::fromBool(m_battle->ended());
        if (pinKey == "result") return m_battle->result();
        if (pinKey == "reason") return m_battle->reason();
        if (pinKey == "message") return m_lastBattleMessage;
        if (pinKey == "turn") return m_battle->activeTeam();
    }

    if (node->type == "Battle.CheckRange") {
        if (pinKey == "valid") {
            return BPValue::fromBool(BattleRuntime::isPointInRange(
                resolveDataPin(nodeId, "shape").toString(),
                resolveDataPin(nodeId, "originX").toNumber(),
                resolveDataPin(nodeId, "originY").toNumber(),
                resolveDataPin(nodeId, "targetX").toNumber(),
                resolveDataPin(nodeId, "targetY").toNumber(),
                resolveDataPin(nodeId, "radius").toNumber(),
                resolveDataPin(nodeId, "width").toNumber(),
                resolveDataPin(nodeId, "height").toNumber(),
                resolveDataPin(nodeId, "dirX").toNumber(),
                resolveDataPin(nodeId, "dirY").toNumber(),
                resolveDataPin(nodeId, "angle").toNumber()));
        }
    }

    if (node->type == "Battle.CheckTarget") {
        if (pinKey == "legal") {
            const BattleUnit* target = battleUnitByKey(resolveDataPin(nodeId, "targetUnit").toString());
            const QString requiredTeam = resolveDataPin(nodeId, "requiredTeam").toString().trimmed();
            const bool teamOk = !target || requiredTeam.isEmpty() || requiredTeam == "任意"
                             || target->team == requiredTeam || target->name == requiredTeam
                             || target->id == requiredTeam;
            const bool rangeOk = BattleRuntime::isPointInRange(
                resolveDataPin(nodeId, "shape").toString(),
                resolveDataPin(nodeId, "originX").toNumber(),
                resolveDataPin(nodeId, "originY").toNumber(),
                resolveDataPin(nodeId, "targetX").toNumber(),
                resolveDataPin(nodeId, "targetY").toNumber(),
                resolveDataPin(nodeId, "radius").toNumber(),
                resolveDataPin(nodeId, "width").toNumber(),
                resolveDataPin(nodeId, "height").toNumber(),
                resolveDataPin(nodeId, "dirX").toNumber(),
                resolveDataPin(nodeId, "dirY").toNumber(),
                resolveDataPin(nodeId, "angle").toNumber());
            return BPValue::fromBool(teamOk && rangeOk);
        }
    }

    if (node->type == "UI.OnDropdownChanged" && pinKey == "index")
        return QString::number(m_dropdownIndex.value(nodeId, 0));
    if ((node->type == "UI.OnDragStart" || node->type == "UI.OnDragMove"
      || node->type == "UI.OnDrop" || node->type == "UI.OnDragCancel")) {
        const UIDragState s = m_uiDragState.value(nodeId);
        if (pinKey == "x") return BPValue::fromNumber(s.x);
        if (pinKey == "y") return BPValue::fromNumber(s.y);
        if (pinKey == "widgetName") return s.widgetName;
    }

    return {};
}

const BPNode* BPRuntime::findNode(const QString& id) const {
    for (const BPNode& n : m_nodes)
        if (n.id == id) return &n;
    return nullptr;
}

const ActorData* BPRuntime::findActorByName(const QString& name) const {
    for (const ActorData& a : m_actors)
        if (a.name == name) return &a;
    return nullptr;
}

const BattleUnit* BPRuntime::battleUnitByKey(const QString& key) const {
    if (!m_battle || !m_battle->active()) return nullptr;
    const QString k = key.trimmed().toLower();
    if (k == "enemy" || k == "敌人" || k == "敌方") return &m_battle->enemy();
    return &m_battle->player();
}

void BPRuntime::triggerBattleEnded() {
    if (!m_battle || !m_battle->ended() || m_battleEndNotified) return;
    m_battleEndNotified = true;
    for (const BPNode& node : m_nodes) {
        if (node.type != "Event.BattleEnded") continue;
        QSet<QString> visited;
        executeChain(node.id, "exec_out", &visited);
    }
    emit stateChanged();
}

void BPRuntime::triggerMouseEvent(const QString& type, const MouseState& payload) {
    for (const BPNode& node : m_nodes) {
        if (node.type != type) continue;
        m_mouseState[node.id] = payload;
        QSet<QString> visited;
        executeChain(node.id, "exec_out", &visited);
    }
}

void BPRuntime::triggerMousePressed(float screenX, float screenY, float worldX, float worldY, const QString& button) {
    triggerMouseEvent("Event.MouseDown", {screenX, screenY, worldX, worldY, 0.0f, 0.0f, button});
    emit stateChanged();
}

void BPRuntime::triggerMouseReleased(float screenX, float screenY, float worldX, float worldY, const QString& button) {
    triggerMouseEvent("Event.MouseUp", {screenX, screenY, worldX, worldY, 0.0f, 0.0f, button});
    emit stateChanged();
}

void BPRuntime::triggerMouseMoved(float screenX, float screenY, float worldX, float worldY) {
    triggerMouseEvent("Event.MouseMove", {screenX, screenY, worldX, worldY, 0.0f, 0.0f, {}});
    emit stateChanged();
}

void BPRuntime::triggerMouseDragged(float screenX, float screenY, float worldX, float worldY, const QString& button) {
    triggerMouseEvent("Event.MouseDrag", {screenX, screenY, worldX, worldY, 0.0f, 0.0f, button});
    emit stateChanged();
}

void BPRuntime::triggerMouseWheeled(float screenX, float screenY, float worldX, float worldY, float deltaX, float deltaY) {
    triggerMouseEvent("Event.MouseWheel", {screenX, screenY, worldX, worldY, deltaX, deltaY, {}});
    emit stateChanged();
}

void BPRuntime::triggerButtonClick(const QString& instanceId, const QString& widgetName) {
    QString uiName;
    if (m_uiRuntime) {
        for (const UIInstance* inst : m_uiRuntime->shownInstances())
            if (inst->instanceId == instanceId) { uiName = inst->uiName; break; }
    }
    for (const BPNode& node : m_nodes) {
        if (node.type != "UI.OnButtonClick") continue;
        auto [refUi, refWidget] = splitWidgetRef(resolveDataPin(node.id, "widgetRef"));
        if ((refUi == instanceId || refUi == uiName) && refWidget == widgetName) {
            QSet<QString> visited;
            executeChain(node.id, "exec_out", &visited);
        }
    }
    emit stateChanged();
}

static bool bpDragEventMatches(const QString& nodeType, const QString& wantedType) {
    return nodeType == wantedType;
}

void BPRuntime::triggerUIDragStarted(const QString& instanceId, const QString& widgetName, float x, float y) {
    QString uiName;
    if (m_uiRuntime) {
        for (const UIInstance* inst : m_uiRuntime->shownInstances())
            if (inst->instanceId == instanceId) { uiName = inst->uiName; break; }
    }
    for (const BPNode& node : m_nodes) {
        if (!bpDragEventMatches(node.type, "UI.OnDragStart")) continue;
        auto [refUi, refWidget] = splitWidgetRef(resolveDataPin(node.id, "widgetRef"));
        if ((refUi == instanceId || refUi == uiName) && (refWidget.isEmpty() || refWidget == widgetName)) {
            m_uiDragState[node.id] = {widgetName, x, y};
            QSet<QString> visited;
            executeChain(node.id, "exec_out", &visited);
        }
    }
    emit stateChanged();
}

void BPRuntime::triggerUIDragMoved(const QString& instanceId, const QString& widgetName, float x, float y) {
    QString uiName;
    if (m_uiRuntime) {
        for (const UIInstance* inst : m_uiRuntime->shownInstances())
            if (inst->instanceId == instanceId) { uiName = inst->uiName; break; }
    }
    for (const BPNode& node : m_nodes) {
        if (!bpDragEventMatches(node.type, "UI.OnDragMove")) continue;
        auto [refUi, refWidget] = splitWidgetRef(resolveDataPin(node.id, "widgetRef"));
        if ((refUi == instanceId || refUi == uiName) && (refWidget.isEmpty() || refWidget == widgetName)) {
            m_uiDragState[node.id] = {widgetName, x, y};
            QSet<QString> visited;
            executeChain(node.id, "exec_out", &visited);
        }
    }
    emit stateChanged();
}

void BPRuntime::triggerUIDropped(const QString& instanceId, const QString& widgetName, float x, float y) {
    QString uiName;
    if (m_uiRuntime) {
        for (const UIInstance* inst : m_uiRuntime->shownInstances())
            if (inst->instanceId == instanceId) { uiName = inst->uiName; break; }
    }
    for (const BPNode& node : m_nodes) {
        if (!bpDragEventMatches(node.type, "UI.OnDrop")) continue;
        auto [refUi, refWidget] = splitWidgetRef(resolveDataPin(node.id, "widgetRef"));
        if ((refUi == instanceId || refUi == uiName) && (refWidget.isEmpty() || refWidget == widgetName)) {
            m_uiDragState[node.id] = {widgetName, x, y};
            QSet<QString> visited;
            executeChain(node.id, "exec_out", &visited);
        }
    }
    emit stateChanged();
}

void BPRuntime::triggerUIDragCanceled(const QString& instanceId, const QString& widgetName, float x, float y) {
    QString uiName;
    if (m_uiRuntime) {
        for (const UIInstance* inst : m_uiRuntime->shownInstances())
            if (inst->instanceId == instanceId) { uiName = inst->uiName; break; }
    }
    for (const BPNode& node : m_nodes) {
        if (!bpDragEventMatches(node.type, "UI.OnDragCancel")) continue;
        auto [refUi, refWidget] = splitWidgetRef(resolveDataPin(node.id, "widgetRef"));
        if ((refUi == instanceId || refUi == uiName) && (refWidget.isEmpty() || refWidget == widgetName)) {
            m_uiDragState[node.id] = {widgetName, x, y};
            QSet<QString> visited;
            executeChain(node.id, "exec_out", &visited);
        }
    }
    emit stateChanged();
}

void BPRuntime::triggerDropdownChanged(const QString& instanceId,
                                        const QString& widgetName, int index) {
    QString uiName;
    if (m_uiRuntime) {
        for (const UIInstance* inst : m_uiRuntime->shownInstances())
            if (inst->instanceId == instanceId) { uiName = inst->uiName; break; }
    }
    for (const BPNode& node : m_nodes) {
        if (node.type != "UI.OnDropdownChanged") continue;
        auto [refUi, refWidget] = splitWidgetRef(resolveDataPin(node.id, "widgetRef"));
        if ((refUi == instanceId || refUi == uiName) && refWidget == widgetName) {
            m_dropdownIndex[node.id] = index;
            QSet<QString> visited;
            executeChain(node.id, "exec_out", &visited);
        }
    }
    emit stateChanged();
}
