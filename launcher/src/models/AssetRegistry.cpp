#include "AssetRegistry.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>

AssetRegistry::AssetRegistry(QString projectRoot)
    : m_projectRoot(std::move(projectRoot)) {}

void AssetRegistry::setProjectRoot(const QString& projectRoot) {
    m_projectRoot = projectRoot;
    m_records.clear();
    m_byId.clear();
    m_byPath.clear();
    m_redirects.clear();
}

QString AssetRegistry::registryPath() const {
    return QDir(m_projectRoot).filePath("asset_registry.json");
}

QString AssetRegistry::normalizePath(const QString& absOrRelPath) const {
    if (absOrRelPath.trimmed().isEmpty()) return {};
    QFileInfo info(absOrRelPath);
    QString rel = info.isAbsolute()
        ? QDir(m_projectRoot).relativeFilePath(info.absoluteFilePath())
        : absOrRelPath;
    rel = QDir::cleanPath(rel);
    while (rel.startsWith("./")) rel.remove(0, 2);
    return rel;
}

QString AssetRegistry::absolutePath(const QString& projectRelativePath) const {
    QFileInfo info(projectRelativePath);
    if (info.isAbsolute()) return info.absoluteFilePath();
    return QDir(m_projectRoot).filePath(projectRelativePath);
}

bool AssetRegistry::load() {
    m_records.clear();
    m_redirects.clear();
    QFile f(registryPath());
    if (!f.open(QIODevice::ReadOnly)) {
        rebuildIndexes();
        return false;
    }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    for (const QJsonValue& v : root.value("assets").toArray()) {
        const QJsonObject o = v.toObject();
        AssetRecord r;
        r.id = o.value("id").toString();
        r.path = normalizePath(o.value("path").toString());
        r.type = o.value("type").toString();
        r.name = o.value("name").toString(QFileInfo(r.path).completeBaseName());
        r.suffix = o.value("suffix").toString(QFileInfo(r.path).suffix());
        if (!r.path.isEmpty()) m_records << r;
    }
    const QJsonObject redirects = root.value("redirects").toObject();
    for (auto it = redirects.constBegin(); it != redirects.constEnd(); ++it) {
        const QString oldPath = normalizePath(it.key());
        const QString targetId = it.value().toString();
        if (!oldPath.isEmpty() && !targetId.isEmpty())
            m_redirects.insert(oldPath, targetId);
    }
    rebuildIndexes();
    return true;
}

bool AssetRegistry::save() const {
    QJsonObject root;
    root["version"] = 1;
    QJsonArray assets;
    for (const AssetRecord& r : m_records) {
        QJsonObject o;
        o["id"] = r.id;
        o["path"] = r.path;
        o["type"] = r.type;
        o["name"] = r.name;
        o["suffix"] = r.suffix;
        assets.append(o);
    }
    root["assets"] = assets;
    QJsonObject redirects;
    for (auto it = m_redirects.constBegin(); it != m_redirects.constEnd(); ++it)
        redirects[it.key()] = it.value();
    root["redirects"] = redirects;

    QSaveFile f(registryPath());
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return f.commit();
}

bool AssetRegistry::rebuild(bool writeBackIds) {
    const QHash<QString, CachedRecord> cachedByPath = loadCachedByPath();
    const QHash<QString, AssetRecord> cachedById = loadCachedById();
    m_redirects = loadRedirects();
    m_records.clear();

    const QStringList filters = {
        "*.level", "*.bp", "*.ui", "*.enum", "*.anim", "*.datatable",
        "*.png", "*.jpg", "*.jpeg", "*.bmp", "*.svg", "*.webp"
    };
    QDirIterator it(m_projectRoot, filters, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString absPath = it.next();
        if (QFileInfo(absPath).fileName() == "asset_registry.json") continue;
        const AssetRecord record = inspectFile(absPath, cachedByPath, writeBackIds);
        if (!record.path.isEmpty() && !record.type.isEmpty()) m_records << record;
    }

    rebuildIndexes();
    for (const AssetRecord& record : m_records) {
        const AssetRecord old = cachedById.value(record.id);
        if (!old.id.isEmpty() && !old.path.isEmpty() && old.path != record.path)
            m_redirects.insert(old.path, record.id);
    }
    pruneRedirects();
    return save();
}

AssetRecord AssetRegistry::findById(const QString& assetId) const {
    return m_byId.value(assetId.trimmed());
}

AssetRecord AssetRegistry::findByPath(const QString& path) const {
    const QString normalized = normalizePath(path);
    const AssetRecord direct = m_byPath.value(normalized);
    if (!direct.path.isEmpty()) return direct;
    const QString redirectedId = m_redirects.value(normalized);
    return redirectedId.isEmpty() ? AssetRecord{} : findById(redirectedId);
}

AssetRecord AssetRegistry::recordForFile(const QString& absOrRelPath) const {
    return findByPath(absOrRelPath);
}

QString AssetRegistry::redirectTargetId(const QString& oldPath) const {
    return m_redirects.value(normalizePath(oldPath));
}

QStringList AssetRegistry::redirectPathsForAsset(const QString& assetId) const {
    QStringList paths;
    if (assetId.isEmpty()) return paths;
    for (auto it = m_redirects.constBegin(); it != m_redirects.constEnd(); ++it) {
        if (it.value() == assetId)
            paths << it.key();
    }
    paths.sort();
    return paths;
}

bool AssetRegistry::noteAssetMoved(const QString& oldAbsOrRelPath, const QString& newAbsOrRelPath) {
    load();
    const QString oldRel = normalizePath(oldAbsOrRelPath);
    const QString newRel = normalizePath(newAbsOrRelPath);
    if (oldRel.isEmpty() || newRel.isEmpty() || oldRel == newRel) return false;

    bool changed = false;
    for (AssetRecord& record : m_records) {
        if (record.path == oldRel || record.path.startsWith(oldRel + "/")) {
            const QString suffix = record.path.mid(oldRel.length());
            m_redirects.insert(record.path, record.id);
            record.path = newRel + suffix;
            changed = true;
        }
    }
    if (!changed) {
        const QString id = QString("path:%1").arg(newRel);
        m_redirects.insert(oldRel, id);
        m_records << AssetRecord{id, newRel, {}, QFileInfo(newRel).completeBaseName(), QFileInfo(newRel).suffix()};
        changed = true;
    }
    rebuildIndexes();
    pruneRedirects();
    return changed && save();
}

QHash<QString, AssetRegistry::CachedRecord> AssetRegistry::loadCachedByPath() const {
    QHash<QString, CachedRecord> cached;
    QFile f(registryPath());
    if (!f.open(QIODevice::ReadOnly)) return cached;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    for (const QJsonValue& v : root.value("assets").toArray()) {
        const QJsonObject o = v.toObject();
        const QString path = normalizePath(o.value("path").toString());
        if (path.isEmpty()) continue;
        cached.insert(path, {o.value("id").toString(), o.value("type").toString()});
    }
    return cached;
}

QHash<QString, AssetRecord> AssetRegistry::loadCachedById() const {
    QHash<QString, AssetRecord> cached;
    QFile f(registryPath());
    if (!f.open(QIODevice::ReadOnly)) return cached;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    for (const QJsonValue& v : root.value("assets").toArray()) {
        const QJsonObject o = v.toObject();
        AssetRecord r;
        r.id = o.value("id").toString();
        r.path = normalizePath(o.value("path").toString());
        r.type = o.value("type").toString();
        r.name = o.value("name").toString(QFileInfo(r.path).completeBaseName());
        r.suffix = o.value("suffix").toString(QFileInfo(r.path).suffix());
        if (!r.id.isEmpty()) cached.insert(r.id, r);
    }
    return cached;
}

QHash<QString, QString> AssetRegistry::loadRedirects() const {
    QHash<QString, QString> redirects;
    QFile f(registryPath());
    if (!f.open(QIODevice::ReadOnly)) return redirects;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonObject obj = root.value("redirects").toObject();
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        const QString oldPath = normalizePath(it.key());
        const QString targetId = it.value().toString();
        if (!oldPath.isEmpty() && !targetId.isEmpty())
            redirects.insert(oldPath, targetId);
    }
    return redirects;
}

AssetRecord AssetRegistry::inspectFile(const QString& absPath, const QHash<QString, CachedRecord>& cachedByPath,
                                       bool writeBackIds) const {
    AssetRecord r;
    const QFileInfo info(absPath);
    r.path = normalizePath(absPath);
    r.suffix = info.suffix();

    QJsonObject obj;
    const bool jsonLike = QStringList{"level", "bp", "ui", "enum", "anim", "datatable"}.contains(info.suffix());
    if (jsonLike) {
        QFile f(absPath);
        if (f.open(QIODevice::ReadOnly)) {
            const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
            if (doc.isObject()) obj = doc.object();
        }
    }

    r.type = detectType(absPath, obj);
    r.name = displayNameFor(absPath, obj);
    const CachedRecord cached = cachedByPath.value(r.path);
    if (jsonLike && !obj.isEmpty())
        r.id = ensureJsonAssetId(absPath, obj, cached.id, writeBackIds);
    if (r.id.isEmpty() && !cached.id.isEmpty())
        r.id = cached.id;
    if (r.id.isEmpty() && r.type == "image")
        r.id = QString("path:%1").arg(r.path);
    return r;
}

QString AssetRegistry::detectType(const QString& absPath, const QJsonObject& obj) const {
    const QString suffix = QFileInfo(absPath).suffix().toLower();
    if (suffix == "level") return "level";
    if (suffix == "ui") return "ui";
    if (suffix == "enum") return "enum";
    if (suffix == "anim") return "anim";
    if (suffix == "datatable") return "datatable";
    if (suffix == "bp") {
        const QString blueprintType = obj.value("blueprintType").toString("Actor");
        if (blueprintType == "Effect") return "bp.effect";
        if (blueprintType == "Component") return "bp.component";
        return "bp";
    }
    if (QStringList{"png", "jpg", "jpeg", "bmp", "svg", "webp"}.contains(suffix)) return "image";
    return {};
}

QString AssetRegistry::displayNameFor(const QString& absPath, const QJsonObject& obj) const {
    const QString fallback = QFileInfo(absPath).completeBaseName();
    return obj.value("name").toString(fallback);
}

QString AssetRegistry::ensureJsonAssetId(const QString& absPath, QJsonObject& obj, const QString& cachedId,
                                         bool writeBackIds) const {
    QString id = obj.value("assetId").toString().trimmed();
    if (id.isEmpty()) id = cachedId.trimmed();
    if (id.isEmpty()) id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (writeBackIds && obj.value("assetId").toString() != id) {
        obj["assetId"] = id;
        QSaveFile f(absPath);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
            f.commit();
        }
    }
    return id;
}

void AssetRegistry::rebuildIndexes() {
    m_byId.clear();
    m_byPath.clear();
    for (const AssetRecord& r : m_records) {
        if (!r.id.isEmpty()) m_byId.insert(r.id, r);
        if (!r.path.isEmpty()) m_byPath.insert(normalizePath(r.path), r);
    }
}

void AssetRegistry::pruneRedirects() {
    for (auto it = m_redirects.begin(); it != m_redirects.end();) {
        const AssetRecord target = m_byId.value(it.value());
        if (target.id.isEmpty() || target.path == it.key())
            it = m_redirects.erase(it);
        else
            ++it;
    }
}
