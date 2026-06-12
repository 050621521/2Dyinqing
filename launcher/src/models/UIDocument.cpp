// src/models/UIDocument.cpp
#include "UIDocument.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFileInfo>

// ── UIWidget JSON ─────────────────────────────────────────────────────────

QJsonObject UIWidget::toJson() const {
    QJsonObject o;
    o["id"]       = id;
    o["name"]     = name;
    o["type"]     = type;
    o["parentId"] = parentId;
    o["visible"]  = visible;
    o["alpha"]    = alpha;
    o["x"]        = x;  o["y"]     = y;
    o["width"]    = width; o["height"] = height;
    o["anchor"]   = anchor;
    o["text"]     = text;
    o["fontSize"] = fontSize;
    o["color"]    = color.name(QColor::HexArgb);
    o["bgColor"]  = bgColor.name(QColor::HexArgb);
    o["imagePath"]= imagePath;
    o["nineSlice"]= nineSlice;
    o["value"]    = value;
    o["fillColor"]= fillColor.name(QColor::HexArgb);
    o["spacing"]  = spacing;
    o["columns"]  = columns;
    o["cellW"]    = cellW; o["cellH"] = cellH;
    o["selectedIndex"] = selectedIndex;
    return o;
}

UIWidget UIWidget::fromJson(const QJsonObject& o) {
    UIWidget w;
    w.id       = o["id"].toString();
    w.name     = o["name"].toString();
    w.type     = o["type"].toString();
    w.parentId = o["parentId"].toString();
    w.visible  = o["visible"].toBool(true);
    w.alpha    = (float)o["alpha"].toDouble(1.0);
    w.x        = (float)o["x"].toDouble();
    w.y        = (float)o["y"].toDouble();
    w.width    = (float)o["width"].toDouble(100);
    w.height   = (float)o["height"].toDouble(30);
    w.anchor   = o["anchor"].toString("左上");
    w.text     = o["text"].toString();
    w.fontSize = o["fontSize"].toInt(16);
    w.color    = QColor(o["color"].toString("#ffffffff"));
    w.bgColor  = QColor(o["bgColor"].toString("#00000000"));
    w.imagePath= o["imagePath"].toString();
    w.nineSlice= o["nineSlice"].toBool(false);
    w.value    = (float)o["value"].toDouble(1.0);
    w.fillColor= QColor(o["fillColor"].toString("#ff50c864"));
    w.spacing  = o["spacing"].toInt(4);
    w.columns  = o["columns"].toInt(4);
    w.cellW    = o["cellW"].toInt(50);
    w.cellH    = o["cellH"].toInt(50);
    w.selectedIndex = o["selectedIndex"].toInt(0);
    return w;
}

// ── UIDocument ───────────────────────────────────────────────────────────

bool UIDocument::load(const QString& filePath) {
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    m_filePath = filePath;
    m_name     = root["name"].toString(QFileInfo(filePath).baseName());
    m_widgets.clear();
    for (const QJsonValue& v : root["widgets"].toArray())
        m_widgets << UIWidget::fromJson(v.toObject());
    m_dirty = false;
    return true;
}

bool UIDocument::save() const {
    QJsonArray arr;
    for (const UIWidget& w : m_widgets)
        arr << w.toJson();
    QJsonObject root;
    root["name"]    = m_name;
    root["version"] = "0.1";
    root["widgets"] = arr;
    QFile f(m_filePath);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

QList<UIWidget> UIDocument::rootWidgets() const {
    QList<UIWidget> result;
    for (const UIWidget& w : m_widgets)
        if (w.parentId.isEmpty()) result << w;
    return result;
}

QList<UIWidget> UIDocument::childrenOf(const QString& parentId) const {
    QList<UIWidget> result;
    for (const UIWidget& w : m_widgets)
        if (w.parentId == parentId) result << w;
    return result;
}

void UIDocument::addWidget(const UIWidget& w) {
    m_widgets << w;
    m_dirty = true;
}

void UIDocument::removeWidget(const QString& id) {
    QStringList toRemove = {id};
    bool found = true;
    while (found) {
        found = false;
        for (const UIWidget& w : m_widgets) {
            if (toRemove.contains(w.parentId) && !toRemove.contains(w.id)) {
                toRemove << w.id;
                found = true;
            }
        }
    }
    m_widgets.erase(std::remove_if(m_widgets.begin(), m_widgets.end(),
        [&](const UIWidget& w){ return toRemove.contains(w.id); }),
        m_widgets.end());
    m_dirty = true;
}

void UIDocument::updateWidget(const UIWidget& w) {
    for (UIWidget& existing : m_widgets) {
        if (existing.id == w.id) { existing = w; m_dirty = true; return; }
    }
}
