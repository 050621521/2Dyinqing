#include "BPValue.h"
#include <QJsonArray>
#include <QJsonDocument>

BPValue BPValue::fromNumber(double n) { BPValue v; v.m_type = Type::Number; v.m_num = n; return v; }
BPValue BPValue::fromBool(bool b)     { BPValue v; v.m_type = Type::Bool;   v.m_bool = b; return v; }
BPValue BPValue::fromString(const QString& s) { return BPValue(s); }
BPValue BPValue::fromArray(const QList<BPValue>& a) { BPValue v; v.m_type = Type::Array; v.m_arr = a; return v; }

// ── 强制转换 ──────────────────────────────────────────────────────────────

QString BPValue::toString() const {
    switch (m_type) {
        case Type::Null:   return QString();
        case Type::Bool:   return m_bool ? QStringLiteral("true") : QStringLiteral("false");
        case Type::String: return m_str;
        case Type::Number:
            // 'g' + 15 位有效数字：整数显示为整数、去掉多余尾零（3.0→"3"，0.1→"0.1"）
            return QString::number(m_num, 'g', 15);
        case Type::Array:
            return QString::fromUtf8(
                QJsonDocument(toJsonValue().toArray()).toJson(QJsonDocument::Compact));
    }
    return QString();
}

double BPValue::toNumber() const {
    switch (m_type) {
        case Type::Null:   return 0.0;
        case Type::Number: return m_num;
        case Type::Bool:   return m_bool ? 1.0 : 0.0;
        case Type::String: {
            bool ok = false;
            const double d = m_str.trimmed().toDouble(&ok);
            return ok ? d : 0.0;           // 解析失败 → 0
        }
        case Type::Array:  return 0.0;
    }
    return 0.0;
}

bool BPValue::toBool() const {
    switch (m_type) {
        case Type::Null:   return false;
        case Type::Bool:   return m_bool;
        case Type::Number: return m_num != 0.0;
        case Type::Array:  return !m_arr.isEmpty();
        case Type::String: {
            const QString s = m_str.trimmed().toLower();
            if (s == QLatin1String("true"))  return true;
            if (s == QLatin1String("false") || s.isEmpty()) return false;
            bool ok = false;
            const double d = s.toDouble(&ok);
            return ok ? (d != 0.0) : true; // 非空非"false"的文本视为真
        }
    }
    return false;
}

QList<BPValue> BPValue::toArray() const {
    return m_type == Type::Array ? m_arr : QList<BPValue>();
}

QList<BPValue>& BPValue::arrayRef() {
    if (m_type != Type::Array) { m_type = Type::Array; m_arr.clear(); }
    return m_arr;
}

// ── 类型感知相等 ──────────────────────────────────────────────────────────

bool BPValue::typedEquals(const BPValue& o) const {
    if (m_type != o.m_type) return false;
    switch (m_type) {
        case Type::Null:   return true;
        case Type::Number: return m_num == o.m_num;
        case Type::Bool:   return m_bool == o.m_bool;
        case Type::String: return m_str == o.m_str;
        case Type::Array:
            if (m_arr.size() != o.m_arr.size()) return false;
            for (int i = 0; i < m_arr.size(); ++i)
                if (!m_arr[i].typedEquals(o.m_arr[i])) return false;
            return true;
    }
    return false;
}

// ── 序列化 ────────────────────────────────────────────────────────────────

QJsonValue BPValue::toJsonValue() const {
    switch (m_type) {
        case Type::Null:   return QJsonValue();
        case Type::Number: return m_num;
        case Type::Bool:   return m_bool;
        case Type::String: return m_str;
        case Type::Array: {
            QJsonArray arr;
            for (const BPValue& e : m_arr) arr.append(e.toJsonValue());
            return arr;
        }
    }
    return QJsonValue();
}

BPValue BPValue::fromJsonValue(const QJsonValue& v) {
    switch (v.type()) {
        case QJsonValue::Bool:   return fromBool(v.toBool());
        case QJsonValue::Double: return fromNumber(v.toDouble());
        case QJsonValue::String: return fromString(v.toString());
        case QJsonValue::Array: {
            QList<BPValue> a;
            for (const QJsonValue& e : v.toArray()) a.append(fromJsonValue(e));
            return fromArray(a);
        }
        case QJsonValue::Null:
        case QJsonValue::Object:
        case QJsonValue::Undefined:
        default: return null();
    }
}
