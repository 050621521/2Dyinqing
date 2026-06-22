#pragma once
#include <QString>
#include <QList>
#include <QJsonValue>

// 蓝图运行时「类型化值」：数据引脚与变量统一用它承载真实类型。
// 设计：docs/superpowers/specs/2026-06-22-blueprint-typed-values-and-operators-design.md
//
// 与 QString 的隐式互转是刻意保留的：现有 93 处求值调用点靠它平滑过渡，
// 迁移期几乎无需逐点手改（详见对应实现计划 Phase 1）。
class BPValue {
public:
    enum class Type { Null, Number, Bool, String, Array };

    BPValue() = default;                                    // Null
    BPValue(const QString& s) : m_type(Type::String), m_str(s) {}        // 隐式
    BPValue(const char* s) : m_type(Type::String), m_str(QString::fromUtf8(s)) {} // 隐式

    static BPValue fromNumber(double n);
    static BPValue fromBool(bool b);
    static BPValue fromString(const QString& s);
    static BPValue fromArray(const QList<BPValue>& a);
    static BPValue null() { return BPValue(); }

    Type type()    const { return m_type; }
    bool isNull()  const { return m_type == Type::Null; }
    bool isArray() const { return m_type == Type::Array; }

    // 强制转换（规则集中定义于 .cpp，全引擎一致）
    QString        toString() const;
    double         toNumber() const;
    bool           toBool()   const;
    QList<BPValue> toArray()  const;   // 非数组 → 空列表

    // 隐式转 QString（压缩迁移面，见类注释）
    operator QString() const { return toString(); }

    // 类型感知相等：类型不同即不等（3 ≠ "3"）。供 =/≠ 与「包含」使用。
    bool typedEquals(const BPValue& o) const;

    // 可变数组访问：若当前非数组，则原地转为空数组后返回引用（数组变量操作节点用）。
    QList<BPValue>& arrayRef();

    // 序列化（变量默认值 / 调试）
    QJsonValue     toJsonValue() const;
    static BPValue fromJsonValue(const QJsonValue& v);

private:
    Type           m_type = Type::Null;
    double         m_num  = 0.0;
    bool           m_bool = false;
    QString        m_str;
    QList<BPValue> m_arr;
};
