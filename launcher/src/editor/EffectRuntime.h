#pragma once

#include "BattleRuntime.h"
#include "models/BPValue.h"
#include "models/AssetRef.h"
#include "BlueprintExecutionContext.h"
#include "BlueprintVariableScope.h"

#include <QMap>
#include <QSet>
#include <QString>
#include <functional>

struct BPClass;
struct BPNode;

struct EffectContext {
    QString source = "player";
    QString target = "enemy";
    QString cardId;
    QString recordId;
    int value = 0;
};

class EffectRuntime {
public:
    using PrintCallback = std::function<void(const QString&)>;
    using DiagnosticCallback = std::function<void(const QString&)>;

    EffectRuntime(QString projectRoot = {}, BattleRuntime* battle = nullptr);

    void setProjectRoot(const QString& projectRoot) { m_projectRoot = projectRoot; }
    void setBattleRuntime(BattleRuntime* battle) { m_battle = battle; }
    void setPrintCallback(PrintCallback callback) { m_printCallback = std::move(callback); }
    void setDiagnosticCallback(DiagnosticCallback callback) { m_diagnosticCallback = std::move(callback); }

    CardEffectResult executeBlueprint(const QString& effectRef,
                                      const EffectContext& context,
                                      const QString& fallbackName = {});

private:
    QString resolveAssetPath(const QString& assetRef) const;
    void report(const QString& message) const;
    void executeChain(const BPClass& effect, const QString& fromNodeId, const QString& fromPin,
                      QSet<QString>* visited = nullptr);
    QString executeNode(const BPClass& effect, const QString& nodeId);
    BPValue resolveDataPin(const BPClass& effect, const QString& nodeId, const QString& pinKey);
    BPValue resolveOutputPin(const BPClass& effect, const QString& nodeId, const QString& pinKey);
    const BPNode* findNode(const BPClass& effect, const QString& id) const;
    void appendMessage(const CardEffectResult& result);

    QString m_projectRoot;
    BattleRuntime* m_battle = nullptr;
    PrintCallback m_printCallback;
    DiagnosticCallback m_diagnosticCallback;
    EffectContext m_context;
    BlueprintExecutionContext m_execContext;
    QMap<QString, BPValue> m_effectVars;
    BlueprintVariableScope m_effectScope;
    QSet<QString> m_reportedUnknownNodes;
    QString m_message;
};
