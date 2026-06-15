#pragma once
#include "models/LevelDocument.h"
#include "models/BPClass.h"
#include "models/UIDocument.h"
#include <QUndoCommand>
#include <functional>

using UndoRefresh = std::function<void()>;

// ── 蓝图辅助（与 BlueprintEditor.cpp 中的 static helper 对应）────────

inline void bpAddNode(BPClass* bc, LevelDocument* doc, const BPNode& n) {
    if (bc) bc->nodes.append(n);
    else if (doc) doc->addBPNode(n);
}
inline void bpRemoveNode(BPClass* bc, LevelDocument* doc, const QString& id) {
    if (bc) bc->nodes.removeIf([&](const BPNode& n){ return n.id == id; });
    else if (doc) doc->removeBPNode(id);
}
inline void bpUpdateNode(BPClass* bc, LevelDocument* doc, const BPNode& n) {
    if (bc) { for (BPNode& x : bc->nodes) if (x.id == n.id) { x = n; return; } }
    else if (doc) doc->updateBPNode(n);
}
inline void bpAddConn(BPClass* bc, LevelDocument* doc, const BPConnection& c) {
    if (bc) bc->connections.append(c);
    else if (doc) doc->addBPConnection(c);
}
inline void bpRemoveConn(BPClass* bc, LevelDocument* doc, const QString& id) {
    if (bc) bc->connections.removeIf([&](const BPConnection& c){ return c.id == id; });
    else if (doc) doc->removeBPConnection(id);
}

// ────────────────────────────────────────────────────────────────────
// Actor 命令
// ────────────────────────────────────────────────────────────────────

class ActorAddCmd : public QUndoCommand {
    LevelDocument* m_doc;
    ActorData      m_data;
    UndoRefresh    m_refresh;
public:
    ActorAddCmd(LevelDocument* doc, ActorData data, UndoRefresh refresh, QUndoCommand* p = nullptr)
        : QUndoCommand("添加 Actor", p), m_doc(doc), m_data(std::move(data)), m_refresh(std::move(refresh)) {}
    void redo() override { m_doc->addActor(m_data);       m_refresh(); }
    void undo() override { m_doc->removeActor(m_data.id); m_refresh(); }
};

class ActorRemoveCmd : public QUndoCommand {
    LevelDocument* m_doc;
    ActorData      m_data;
    UndoRefresh    m_refresh;
public:
    ActorRemoveCmd(LevelDocument* doc, ActorData data, UndoRefresh refresh, QUndoCommand* p = nullptr)
        : QUndoCommand("删除 Actor", p), m_doc(doc), m_data(std::move(data)), m_refresh(std::move(refresh)) {}
    void redo() override { m_doc->removeActor(m_data.id); m_refresh(); }
    void undo() override { m_doc->addActor(m_data);       m_refresh(); }
};

class ActorTransformCmd : public QUndoCommand {
    LevelDocument*   m_doc;
    QList<ActorData> m_before;
    QList<ActorData> m_after;
    UndoRefresh      m_refresh;
public:
    ActorTransformCmd(LevelDocument* doc, QList<ActorData> before, QList<ActorData> after,
                      UndoRefresh refresh, QUndoCommand* p = nullptr)
        : QUndoCommand("变换 Actor", p), m_doc(doc),
          m_before(std::move(before)), m_after(std::move(after)), m_refresh(std::move(refresh)) {}
    void redo() override { for (const auto& a : m_after)  m_doc->updateActor(a); m_refresh(); }
    void undo() override { for (const auto& a : m_before) m_doc->updateActor(a); m_refresh(); }
};

class ActorModifyCmd : public QUndoCommand {
    LevelDocument* m_doc;
    ActorData      m_before;
    ActorData      m_after;
    UndoRefresh    m_refresh;
public:
    ActorModifyCmd(LevelDocument* doc, ActorData before, ActorData after,
                   UndoRefresh refresh, QUndoCommand* p = nullptr)
        : QUndoCommand("修改 Actor", p), m_doc(doc),
          m_before(std::move(before)), m_after(std::move(after)), m_refresh(std::move(refresh)) {}
    void redo() override { m_doc->updateActor(m_after);  m_refresh(); }
    void undo() override { m_doc->updateActor(m_before); m_refresh(); }
};

// ────────────────────────────────────────────────────────────────────
// 蓝图命令
// ────────────────────────────────────────────────────────────────────

class BPNodeAddCmd : public QUndoCommand {
    BPClass*       m_bc;
    LevelDocument* m_doc;
    BPNode         m_node;
    UndoRefresh    m_refresh;
public:
    BPNodeAddCmd(BPClass* bc, LevelDocument* doc, BPNode node, UndoRefresh refresh, QUndoCommand* p = nullptr)
        : QUndoCommand("添加节点", p), m_bc(bc), m_doc(doc), m_node(std::move(node)), m_refresh(std::move(refresh)) {}
    void redo() override { bpAddNode(m_bc, m_doc, m_node);       m_refresh(); }
    void undo() override { bpRemoveNode(m_bc, m_doc, m_node.id); m_refresh(); }
};

class BPNodeRemoveCmd : public QUndoCommand {
    BPClass*            m_bc;
    LevelDocument*      m_doc;
    BPNode              m_node;
    QList<BPConnection> m_relatedConns;
    UndoRefresh         m_refresh;
public:
    BPNodeRemoveCmd(BPClass* bc, LevelDocument* doc, BPNode node, QList<BPConnection> conns,
                    UndoRefresh refresh, QUndoCommand* p = nullptr)
        : QUndoCommand("删除节点", p), m_bc(bc), m_doc(doc),
          m_node(std::move(node)), m_relatedConns(std::move(conns)), m_refresh(std::move(refresh)) {}
    void redo() override {
        for (const auto& c : m_relatedConns) bpRemoveConn(m_bc, m_doc, c.id);
        bpRemoveNode(m_bc, m_doc, m_node.id);
        m_refresh();
    }
    void undo() override {
        bpAddNode(m_bc, m_doc, m_node);
        for (const auto& c : m_relatedConns) bpAddConn(m_bc, m_doc, c);
        m_refresh();
    }
};

class BPConnectionAddCmd : public QUndoCommand {
    BPClass*       m_bc;
    LevelDocument* m_doc;
    BPConnection   m_conn;
    BPConnection   m_displaced;
    bool           m_hasDisplaced;
    UndoRefresh    m_refresh;
public:
    BPConnectionAddCmd(BPClass* bc, LevelDocument* doc, BPConnection conn,
                       BPConnection displaced, bool hasDisplaced,
                       UndoRefresh refresh, QUndoCommand* p = nullptr)
        : QUndoCommand("连接引脚", p), m_bc(bc), m_doc(doc),
          m_conn(std::move(conn)), m_displaced(std::move(displaced)), m_hasDisplaced(hasDisplaced),
          m_refresh(std::move(refresh)) {}
    void redo() override {
        if (m_hasDisplaced) bpRemoveConn(m_bc, m_doc, m_displaced.id);
        bpAddConn(m_bc, m_doc, m_conn);
        m_refresh();
    }
    void undo() override {
        bpRemoveConn(m_bc, m_doc, m_conn.id);
        if (m_hasDisplaced) bpAddConn(m_bc, m_doc, m_displaced);
        m_refresh();
    }
};

class BPConnectionRemoveCmd : public QUndoCommand {
    BPClass*       m_bc;
    LevelDocument* m_doc;
    BPConnection   m_conn;
    UndoRefresh    m_refresh;
public:
    BPConnectionRemoveCmd(BPClass* bc, LevelDocument* doc, BPConnection conn,
                          UndoRefresh refresh, QUndoCommand* p = nullptr)
        : QUndoCommand("删除连线", p), m_bc(bc), m_doc(doc),
          m_conn(std::move(conn)), m_refresh(std::move(refresh)) {}
    void redo() override { bpRemoveConn(m_bc, m_doc, m_conn.id); m_refresh(); }
    void undo() override { bpAddConn(m_bc, m_doc, m_conn);       m_refresh(); }
};

// 修改节点（params 变化，如分支控制的分支增删/改值/默认开关）
class BPNodeModifyCmd : public QUndoCommand {
    BPClass*       m_bc;
    LevelDocument* m_doc;
    BPNode         m_before;
    BPNode         m_after;
    UndoRefresh    m_refresh;
public:
    BPNodeModifyCmd(BPClass* bc, LevelDocument* doc, BPNode before, BPNode after,
                    const QString& text, UndoRefresh refresh, QUndoCommand* p = nullptr)
        : QUndoCommand(text, p), m_bc(bc), m_doc(doc),
          m_before(std::move(before)), m_after(std::move(after)), m_refresh(std::move(refresh)) {}
    void redo() override { bpUpdateNode(m_bc, m_doc, m_after);  m_refresh(); }
    void undo() override { bpUpdateNode(m_bc, m_doc, m_before); m_refresh(); }
};

class BPNodeMoveCmd : public QUndoCommand {
    BPClass*       m_bc;
    LevelDocument* m_doc;
    BPNode         m_before;
    BPNode         m_after;
    UndoRefresh    m_refresh;
public:
    BPNodeMoveCmd(BPClass* bc, LevelDocument* doc, BPNode before, BPNode after,
                  UndoRefresh refresh, QUndoCommand* p = nullptr)
        : QUndoCommand("移动节点", p), m_bc(bc), m_doc(doc),
          m_before(std::move(before)), m_after(std::move(after)), m_refresh(std::move(refresh)) {}
    void redo() override { bpUpdateNode(m_bc, m_doc, m_after);  m_refresh(); }
    void undo() override { bpUpdateNode(m_bc, m_doc, m_before); m_refresh(); }
};

// ────────────────────────────────────────────────────────────────────
// UI 控件命令
// ────────────────────────────────────────────────────────────────────

class UIWidgetAddCmd : public QUndoCommand {
    UIDocument*     m_doc;
    QList<UIWidget> m_subtree;  // root 在 index 0，其余为子孙
    UndoRefresh     m_refresh;
public:
    UIWidgetAddCmd(UIDocument* doc, QList<UIWidget> subtree, UndoRefresh refresh, QUndoCommand* p = nullptr)
        : QUndoCommand("添加控件", p), m_doc(doc), m_subtree(std::move(subtree)), m_refresh(std::move(refresh)) {}
    void redo() override {
        for (const UIWidget& w : m_subtree) m_doc->addWidget(w);
        m_refresh();
    }
    void undo() override {
        m_doc->removeWidget(m_subtree[0].id);  // removeWidget 递归删子孙
        m_refresh();
    }
};

class UIWidgetRemoveCmd : public QUndoCommand {
    UIDocument*     m_doc;
    QList<UIWidget> m_subtree;  // 删除前完整子树，root 在 index 0
    UndoRefresh     m_refresh;
public:
    UIWidgetRemoveCmd(UIDocument* doc, QList<UIWidget> subtree, UndoRefresh refresh, QUndoCommand* p = nullptr)
        : QUndoCommand("删除控件", p), m_doc(doc), m_subtree(std::move(subtree)), m_refresh(std::move(refresh)) {}
    void redo() override {
        m_doc->removeWidget(m_subtree[0].id);
        m_refresh();
    }
    void undo() override {
        for (const UIWidget& w : m_subtree) m_doc->addWidget(w);
        m_refresh();
    }
};

class UIWidgetModifyCmd : public QUndoCommand {
    UIDocument* m_doc;
    UIWidget    m_before;
    UIWidget    m_after;
    UndoRefresh m_refresh;
public:
    UIWidgetModifyCmd(UIDocument* doc, UIWidget before, UIWidget after, UndoRefresh refresh, QUndoCommand* p = nullptr)
        : QUndoCommand("修改控件", p), m_doc(doc),
          m_before(std::move(before)), m_after(std::move(after)), m_refresh(std::move(refresh)) {}
    void redo() override { m_doc->updateWidget(m_after);  m_refresh(); }
    void undo() override { m_doc->updateWidget(m_before); m_refresh(); }
};

class UIWidgetMoveCmd : public QUndoCommand {
    UIDocument*     m_doc;
    QList<UIWidget> m_before;
    QList<UIWidget> m_after;
    UndoRefresh     m_refresh;
public:
    UIWidgetMoveCmd(UIDocument* doc, QList<UIWidget> before, QList<UIWidget> after,
                    UndoRefresh refresh, QUndoCommand* p = nullptr)
        : QUndoCommand("移动控件", p), m_doc(doc),
          m_before(std::move(before)), m_after(std::move(after)), m_refresh(std::move(refresh)) {}
    void redo() override { for (const auto& w : m_after)  m_doc->updateWidget(w); m_refresh(); }
    void undo() override { for (const auto& w : m_before) m_doc->updateWidget(w); m_refresh(); }
};

class UIWidgetBatchModifyCmd : public QUndoCommand {
    UIDocument*     m_doc;
    QList<UIWidget> m_before;
    QList<UIWidget> m_after;
    UndoRefresh     m_refresh;
public:
    UIWidgetBatchModifyCmd(UIDocument* doc, QList<UIWidget> before, QList<UIWidget> after,
                           const QString& text, UndoRefresh refresh, QUndoCommand* p = nullptr)
        : QUndoCommand(text, p), m_doc(doc),
          m_before(std::move(before)), m_after(std::move(after)), m_refresh(std::move(refresh)) {}
    void redo() override { for (const auto& w : m_after)  m_doc->updateWidget(w); m_refresh(); }
    void undo() override { for (const auto& w : m_before) m_doc->updateWidget(w); m_refresh(); }
};
