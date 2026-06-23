#include "ProjectManager.h"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

ProjectManager& ProjectManager::instance() {
    static ProjectManager inst;
    return inst;
}

QString ProjectManager::configPath() const {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + "/projects.json";
}

void ProjectManager::load() {
    m_categories = {
        {"recent",    "最近打开的项目", ""},
        {"platform",  "平台游戏",       ""},
        {"rpg",       "RPG",            ""},
        {"shooter",   "射击游戏",       ""},
        {"puzzle",    "益智游戏",       ""},
        {"blank",     "空白项目",       ""},
    };

    m_recent.clear();

    QFile f(configPath());
    if (!f.open(QIODevice::ReadOnly)) return;

    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    for (const auto& val : doc.array()) {
        QJsonObject obj = val.toObject();
        ProjectInfo info;
        info.name          = obj["name"].toString();
        info.path          = obj["path"].toString();
        info.thumbnailPath = obj["thumbnail"].toString();
        info.lastOpened    = QDateTime::fromString(obj["lastOpened"].toString(), Qt::ISODate);
        info.engineVersion = obj["version"].toString("0.1");
        if (info.isValid()) m_recent.append(info);
    }
}

void ProjectManager::save() const {
    QJsonArray arr;
    for (const auto& p : m_recent) {
        QJsonObject obj;
        obj["name"]       = p.name;
        obj["path"]       = p.path;
        obj["thumbnail"]  = p.thumbnailPath;
        obj["lastOpened"] = p.lastOpened.toString(Qt::ISODate);
        obj["version"]    = p.engineVersion;
        arr.append(obj);
    }
    QFile f(configPath());
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(arr).toJson());
}

QList<ProjectInfo> ProjectManager::recentProjects() const {
    return m_recent;
}

void ProjectManager::addRecentProject(const ProjectInfo& info) {
    for (int i = 0; i < m_recent.size(); ++i) {
        if (m_recent[i].path == info.path) {
            m_recent.removeAt(i);
            break;
        }
    }
    m_recent.prepend(info);
    save();
}

void ProjectManager::removeProject(const QString& path) {
    m_recent.removeIf([&](const ProjectInfo& p){ return p.path == path; });
    save();
}

void ProjectManager::updateProjectPath(const QString& oldPath, const QString& newPath) {
    for (auto& p : m_recent) {
        if (p.path == oldPath) { p.path = newPath; break; }
    }
    save();
}

QList<TemplateCategory> ProjectManager::templateCategories() const {
    return m_categories;
}
