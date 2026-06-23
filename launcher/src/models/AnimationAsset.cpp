#include "AnimationAsset.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>

bool AnimationAsset::load(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();

    sheet  = obj["sheet"].toString();
    frameW = obj["frameW"].toInt();
    frameH = obj["frameH"].toInt();
    cols   = obj["cols"].toInt(1);
    rows   = obj["rows"].toInt(1);

    clips.clear();
    const QJsonArray arr = obj["clips"].toArray();
    for (const QJsonValue& v : arr) {
        const QJsonObject c = v.toObject();
        AnimClip clip;
        clip.name       = c["name"].toString();
        clip.label      = c["label"].toString();
        clip.row        = c["row"].toInt();
        clip.startCol   = c["startCol"].toInt(0);
        clip.frameCount = c["frameCount"].toInt(1);
        clip.fps        = c["fps"].toDouble(12.0);
        clip.loop       = c["loop"].toBool(true);
        clips << clip;
    }
    return true;
}

bool AnimationAsset::save(const QString& path) const {
    QJsonObject obj;
    obj["sheet"]  = sheet;
    obj["frameW"] = frameW;
    obj["frameH"] = frameH;
    obj["cols"]   = cols;
    obj["rows"]   = rows;

    QJsonArray arr;
    for (const AnimClip& c : clips) {
        QJsonObject o;
        o["name"]       = c.name;
        o["label"]      = c.label;
        o["row"]        = c.row;
        o["startCol"]   = c.startCol;
        o["frameCount"] = c.frameCount;
        o["fps"]        = c.fps;
        o["loop"]       = c.loop;
        arr.append(o);
    }
    obj["clips"] = arr;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    return true;
}

const AnimClip* AnimationAsset::findClip(const QString& name) const {
    for (const AnimClip& c : clips)
        if (c.name == name) return &c;
    return nullptr;
}

QRect AnimationAsset::frameRect(const AnimClip& clip, int frameIndex) const {
    const int col = clip.startCol + frameIndex;
    return QRect(col * frameW, clip.row * frameH, frameW, frameH);
}

AnimationAsset AnimationAsset::fromMetadataJson(const QJsonObject& meta, const QString& sheetAbsPath) {
    AnimationAsset a;
    a.sheet = sheetAbsPath;

    const QJsonObject fs = meta["frame_size"].toObject();
    a.frameW = fs["width"].toInt();
    a.frameH = fs["height"].toInt();

    const QJsonObject grid = meta["sheet_grid"].toObject();
    a.cols = grid["columns"].toInt(1);
    a.rows = grid["rows"].toInt(1);

    const QJsonArray dirs = meta["directions"].toArray();
    for (const QJsonValue& v : dirs) {
        const QJsonObject d = v.toObject();
        AnimClip clip;
        const QString key = d["key"].toString();
        clip.name       = "walk_" + key;          // 对齐素材的 Godot 动画名建议
        clip.label      = d["label"].toString();
        clip.row        = d["row"].toInt();
        clip.startCol   = 0;
        clip.frameCount = d["frames"].toInt(1);
        clip.fps        = d["fps"].toDouble(12.0);
        clip.loop       = d["loop"].toBool(true);
        a.clips << clip;
    }
    return a;
}
