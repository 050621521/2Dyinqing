#pragma once

#include "models/BPValue.h"

#include <QMap>
#include <QString>

class BlueprintVariableScope {
public:
    explicit BlueprintVariableScope(QMap<QString, BPValue>* store = nullptr,
                                    BlueprintVariableScope* parent = nullptr);

    void setStore(QMap<QString, BPValue>* store) { m_store = store; }
    void setParent(BlueprintVariableScope* parent) { m_parent = parent; }

    bool hasLocal(const QString& name) const;
    bool has(const QString& name) const;
    BPValue get(const QString& name) const;

    void setLocal(const QString& name, const BPValue& value);
    void set(const QString& name, const BPValue& value);

    QMap<QString, BPValue>* store() const { return m_store; }

private:
    QMap<QString, BPValue>* m_store = nullptr;
    BlueprintVariableScope* m_parent = nullptr;
};
