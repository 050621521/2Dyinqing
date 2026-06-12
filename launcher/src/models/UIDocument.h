// src/models/UIDocument.h
#pragma once
#include <QString>
#include <QList>
#include <QColor>
#include <QJsonObject>

struct UIWidget {
    // ── 通用 ──────────────────────────────────────────────
    QString id;
    QString name;
    QString type;       // "UI.面板"、"UI.文本"、"UI.按钮" …
    QString parentId;   // 空 = 根节点
    bool    visible = true;
    float   alpha   = 1.0f;

    // ── 布局（屏幕空间，相对锚点偏移）───────────────────
    float   x = 0, y = 0;
    float   width = 100, height = 30;
    QString anchor = "左上"; // 左上/正上/右上/左中/居中/右中/左下/正下/右下

    // ── 类型专属 ─────────────────────────────────────────
    QString text;
    int     fontSize  = 16;
    QColor  color     = QColor(255, 255, 255, 255); // 前景色
    QColor  bgColor   = QColor(0, 0, 0, 0);         // 背景色
    QString imagePath;
    bool    nineSlice = false;
    float   value     = 1.0f;                        // 进度条 0~1
    QColor  fillColor = QColor(80, 200, 100, 255);   // 进度条填充色
    int     spacing   = 4;   // VBox/HBox 子间距
    int     columns   = 4;   // 网格列数
    int     cellW     = 50, cellH = 50;
    int     selectedIndex = 0; // 下拉菜单

    QJsonObject toJson() const;
    static UIWidget fromJson(const QJsonObject& obj);
};

class UIDocument {
public:
    bool load(const QString& filePath);
    bool save();

    bool    isDirty()   const { return m_dirty; }
    void    setDirty(bool v)  { m_dirty = v; }
    QString name()      const { return m_name; }
    QString filePath()  const { return m_filePath; }

    const QList<UIWidget>& widgets()                          const { return m_widgets; }
    QList<UIWidget>        rootWidgets()                      const;
    QList<UIWidget>        childrenOf(const QString& parentId) const;

    void addWidget   (const UIWidget& w);
    void removeWidget(const QString& id);   // 同时递归删除子孙
    void updateWidget(const UIWidget& w);

private:
    QString         m_filePath, m_name;
    QList<UIWidget> m_widgets;
    bool            m_dirty = false;
};
