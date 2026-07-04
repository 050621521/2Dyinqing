#include "BlueprintVariableScope.h"

BlueprintVariableScope::BlueprintVariableScope(QMap<QString, BPValue>* store,
                                               BlueprintVariableScope* parent)
    : m_store(store), m_parent(parent) {}

bool BlueprintVariableScope::hasLocal(const QString& name) const {
    return m_store && m_store->contains(name);
}

bool BlueprintVariableScope::has(const QString& name) const {
    return hasLocal(name) || (m_parent && m_parent->has(name));
}

BPValue BlueprintVariableScope::get(const QString& name) const {
    if (m_store && m_store->contains(name))
        return m_store->value(name);
    return m_parent ? m_parent->get(name) : BPValue();
}

void BlueprintVariableScope::setLocal(const QString& name, const BPValue& value) {
    if (name.isEmpty() || !m_store) return;
    (*m_store)[name] = value;
}

void BlueprintVariableScope::set(const QString& name, const BPValue& value) {
    if (name.isEmpty()) return;
    if (m_store && m_store->contains(name)) {
        (*m_store)[name] = value;
        return;
    }
    if (m_parent && m_parent->has(name)) {
        m_parent->set(name, value);
        return;
    }
    setLocal(name, value);
}
