#pragma once

#include "BlueprintExecutionContext.h"
#include "BlueprintVariableScope.h"

#include <QString>
#include <QHash>
#include <functional>

struct BPNode;

struct BlueprintLoopState {
    BPValue element;
    int index = 0;
};

class BlueprintNodeExecutor {
public:
    using PinResolver = std::function<BPValue(const QString&)>;
    using PrintCallback = std::function<void(const QString&)>;
    using DiagnosticCallback = std::function<void(const QString&)>;
    using LoadLevelCallback = std::function<void(const QString&)>;
    using BackLevelCallback = std::function<void()>;
    using ChainRunner = std::function<void(const QString&, const QString&)>;

    struct ExecState {
        BlueprintExecutionContext* context = nullptr;
        BlueprintVariableScope* localScope = nullptr;
        BlueprintVariableScope* globalScope = nullptr;
        QHash<QString, BlueprintLoopState>* loopState = nullptr;
        PrintCallback print;
        DiagnosticCallback diagnostic;
        LoadLevelCallback loadLevel;
        BackLevelCallback backLevel;
        ChainRunner runFromPin;
    };

    static bool executeSharedNode(const BPNode& node,
                                  const PinResolver& pin,
                                  ExecState& state,
                                  QString* nextPin);
    static bool resolveSharedOutput(const BPNode& node,
                                    const QString& pinKey,
                                    const PinResolver& pin,
                                    ExecState& state,
                                    BPValue* out);
};
