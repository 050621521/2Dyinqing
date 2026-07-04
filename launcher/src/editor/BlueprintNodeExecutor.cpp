#include "BlueprintNodeExecutor.h"

#include "BPEval.h"
#include "models/BPClass.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
bool isArrayMutation(const QString& type) {
    return type == "Array.Add" || type == "Array.RemoveAt"
        || type == "Array.RemoveValue" || type == "Array.SetAt"
        || type == "Array.Clear";
}

BlueprintVariableScope* scopeForArrayNode(const BPNode& node, BlueprintNodeExecutor::ExecState& state) {
    return node.params.value("scope") == "local" ? state.localScope : state.globalScope;
}

void report(BlueprintNodeExecutor::ExecState& state, const QString& message) {
    if (state.diagnostic && !message.isEmpty())
        state.diagnostic(state.context ? state.context->formatDiagnostic(message) : message);
}
}

bool BlueprintNodeExecutor::executeSharedNode(const BPNode& node,
                                              const PinResolver& pin,
                                              ExecState& state,
                                              QString* nextPin) {
    if (!nextPin) return false;

    if (node.type == "Action.Print") {
        if (state.print) state.print(pin("text").toString());
        *nextPin = "exec_out";
        return true;
    }

    if (node.type == "Flow.Branch") {
        *nextPin = pin("condition").toBool() ? "true" : "false";
        return true;
    }

    if (node.type == "Flow.Switch") {
        const QString v = pin("value").toString();
        const QJsonDocument d = QJsonDocument::fromJson(node.params.value("branches").toUtf8());
        if (d.isArray()) {
            for (const QJsonValue& bv : d.array()) {
                const QJsonObject o = bv.toObject();
                const QString id = o.value("id").toString();
                if (id.isEmpty()) continue;
                QString cmp = pin("caseval_" + id).toString();
                if (cmp.isEmpty()) cmp = o.value("value").toString();
                if (cmp == v) {
                    *nextPin = "case_" + id;
                    return true;
                }
            }
        }
        const QString hd = node.params.value("hasDefault").toLower();
        *nextPin = (hd == "true" || hd == "1") ? "default" : QString();
        return true;
    }

    if (node.type == "Flow.ForEach") {
        const QList<BPValue> arr = pin("array").toArray();
        if (state.loopState && state.runFromPin) {
            for (int i = 0; i < arr.size(); ++i) {
                (*state.loopState)[node.id] = {arr[i], i};
                state.runFromPin(node.id, "loop_body");
            }
            state.loopState->remove(node.id);
        }
        *nextPin = "completed";
        return true;
    }

    if (node.type == "Action.LoadLevel") {
        const QString levelName = pin("levelName").toString();
        if (!levelName.isEmpty() && state.loadLevel) state.loadLevel(levelName);
        *nextPin = {};
        return true;
    }

    if (node.type == "Action.BackLevel") {
        if (state.backLevel) state.backLevel();
        *nextPin = {};
        return true;
    }

    if (node.type == "Global.Set") {
        const QString name = node.params.value("varName").trimmed();
        if (name.isEmpty()) {
            report(state, "全局变量写入缺少变量名");
        } else if (state.globalScope) {
            state.globalScope->setLocal(name, pin("value"));
        } else {
            report(state, QString("没有可写入的全局变量作用域：%1").arg(name));
        }
        *nextPin = "exec_out";
        return true;
    }

    if (node.type == "Local.Set") {
        const QString name = node.params.value("varName").trimmed();
        if (name.isEmpty()) {
            report(state, "局部变量写入缺少变量名");
        } else if (state.localScope) {
            state.localScope->setLocal(name, pin("value"));
        } else {
            report(state, QString("没有可写入的局部变量作用域：%1").arg(name));
        }
        *nextPin = "exec_out";
        return true;
    }

    if (node.type == "Var.SetNumber" || node.type == "Var.SetBool" || node.type == "Var.SetString") {
        const QString name = pin("name").toString().trimmed();
        if (name.isEmpty()) {
            report(state, "变量写入缺少变量名");
        } else if (state.localScope) {
            state.localScope->setLocal(name, pin("value"));
        } else {
            report(state, QString("没有可写入的变量作用域：%1").arg(name));
        }
        *nextPin = "exec_out";
        return true;
    }

    if (isArrayMutation(node.type)) {
        const QString name = node.params.value("varName").trimmed();
        BlueprintVariableScope* scope = scopeForArrayNode(node, state);
        if (!name.isEmpty() && scope) {
            BPValue cur = scope->get(name);
            QList<BPValue>& arr = cur.arrayRef();
            if (node.type == "Array.Add") {
                arr.append(pin("value"));
            } else if (node.type == "Array.Clear") {
                arr.clear();
            } else if (node.type == "Array.RemoveAt") {
                const int i = static_cast<int>(pin("index").toNumber());
                if (i >= 0 && i < arr.size()) arr.removeAt(i);
            } else if (node.type == "Array.RemoveValue") {
                const BPValue v = pin("value");
                for (int i = 0; i < arr.size(); ++i) {
                    if (arr[i].typedEquals(v)) {
                        arr.removeAt(i);
                        break;
                    }
                }
            } else if (node.type == "Array.SetAt") {
                const int i = static_cast<int>(pin("index").toNumber());
                if (i >= 0 && i < arr.size()) arr[i] = pin("value");
            }
            scope->setLocal(name, cur);
        } else if (name.isEmpty()) {
            report(state, "数组变量操作缺少变量名");
        } else {
            report(state, QString("没有可写入的数组变量作用域：%1").arg(name));
        }
        *nextPin = "exec_out";
        return true;
    }

    return false;
}

bool BlueprintNodeExecutor::resolveSharedOutput(const BPNode& node,
                                                const QString& pinKey,
                                                const PinResolver& pin,
                                                ExecState& state,
                                                BPValue* out) {
    if (!out) return false;

    if (node.type == "Global.Get") {
        *out = state.globalScope ? state.globalScope->get(node.params.value("varName")) : BPValue();
        return true;
    }

    if (node.type == "Local.Get") {
        *out = state.localScope ? state.localScope->get(node.params.value("varName")) : BPValue();
        return true;
    }

    if (node.type == "Var.GetNumber" || node.type == "Var.GetBool" || node.type == "Var.GetString") {
        const QString name = pin("name").toString().trimmed();
        *out = state.localScope ? state.localScope->get(name) : BPValue();
        return true;
    }

    if (node.type == "Flow.ForEach") {
        const BlueprintLoopState s = state.loopState ? state.loopState->value(node.id) : BlueprintLoopState{};
        if (pinKey == "current") *out = s.element;
        else if (pinKey == "index") *out = BPValue::fromNumber(s.index);
        else *out = BPValue();
        return true;
    }

    return evalPureDataNode(node.type, pinKey, pin, *out);
}
