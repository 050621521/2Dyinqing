#include "LayoutManager.h"
#include <DockManager.h>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

LayoutManager::LayoutManager(ads::CDockManager* dock,
                              const QString& projectPath,
                              QObject* parent)
    : QObject(parent), m_dock(dock)
    , m_filePath(projectPath + "/editor_layouts.json")
{
    readFile();
}

void LayoutManager::captureDefault() {
    m_defaultState = m_dock->saveState();
    if (!m_layouts.contains("默认布局")) {
        m_layouts["默认布局"] = QString::fromLatin1(m_defaultState.toBase64());
        if (m_current.isEmpty()) m_current = "默认布局";
        writeFile();
        emit layoutListChanged();
    } else {
        loadLayout(m_current.isEmpty() ? "默认布局" : m_current);
    }
}

QStringList LayoutManager::layoutNames() const {
    return m_layouts.keys();
}

QString LayoutManager::currentLayout() const {
    return m_current;
}

void LayoutManager::saveLayout(const QString& name) {
    m_layouts[name] = QString::fromLatin1(m_dock->saveState().toBase64());
    m_current = name;
    writeFile();
    emit layoutListChanged();
    emit currentLayoutChanged(name);
}

bool LayoutManager::loadLayout(const QString& name) {
    if (!m_layouts.contains(name)) return false;
    const QByteArray state =
        QByteArray::fromBase64(m_layouts.value(name).toLatin1());
    if (!m_dock->restoreState(state)) return false;
    m_current = name;
    writeFile();
    emit currentLayoutChanged(name);
    return true;
}

void LayoutManager::deleteLayout(const QString& name) {
    if (name == "默认布局") return;
    m_layouts.remove(name);
    if (m_current == name) {
        m_current = "默认布局";
        loadLayout("默认布局");
    }
    writeFile();
    emit layoutListChanged();
}

void LayoutManager::resetDefault() {
    loadLayout("默认布局");
}

void LayoutManager::readFile() {
    QFile f(m_filePath);
    if (!f.open(QIODevice::ReadOnly)) return;
    const auto obj = QJsonDocument::fromJson(f.readAll()).object();
    m_current = obj["current"].toString();
    const auto layouts = obj["layouts"].toObject();
    for (auto it = layouts.begin(); it != layouts.end(); ++it)
        m_layouts[it.key()] = it.value().toString();
}

void LayoutManager::writeFile() {
    QJsonObject layouts;
    for (auto it = m_layouts.begin(); it != m_layouts.end(); ++it)
        layouts[it.key()] = it.value();
    QJsonObject root;
    root["current"] = m_current;
    root["layouts"] = layouts;
    QFile f(m_filePath);
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(root).toJson());
}
