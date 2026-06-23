#pragma once
#include "models/BPValue.h"
#include <QString>
#include <QRegularExpression>
#include <QRandomGenerator>
#include <functional>
#include <algorithm>
#include <cmath>

// 蓝图「纯数据节点」共享求值器。
//
// 这些节点的输出只取决于其输入引脚（与运行时状态无关）：数学、比较、逻辑、
// 数组、类型转换。关卡蓝图(BPRuntime)与 Actor 蓝图(ActorBPRuntime)都复用本函数，
// 避免重复实现、保证两套运行时行为一致（Actor 蓝图「一等公民」化的基础）。
//
// `pin` 为已绑定当前节点的输入解析器：给引脚名，返回该引脚的输入值。
// 命中则写 out 并返回 true；非纯数据节点返回 false（由各运行时自行处理）。
inline bool evalPureDataNode(const QString& type, const QString& pinKey,
                             const std::function<BPValue(const QString&)>& pin,
                             BPValue& out)
{
    // 数值转字符串（去掉多余的小数零）
    if (type == "Var.NumberToString" && pinKey == "text") {
        const float v = pin("number").toString().toFloat();
        out = QString::number(v, 'f', 6)
                  .remove(QRegularExpression("0+$"))
                  .remove(QRegularExpression("\\.$"));
        return true;
    }

    // ── 数学运算（一符一节点）─────────────────────────────────────────────
    if (pinKey == "result"
        && (type == "Math.Add" || type == "Math.Sub"
         || type == "Math.Mul" || type == "Math.Div" || type == "Math.Mod")) {
        const double a = pin("a").toNumber();
        const double b = pin("b").toNumber();
        double r = 0.0;
        if      (type == "Math.Add") r = a + b;
        else if (type == "Math.Sub") r = a - b;
        else if (type == "Math.Mul") r = a * b;
        else if (type == "Math.Div") r = (b != 0.0) ? a / b : 0.0;           // 除 0 → 0
        else                         r = (b != 0.0) ? std::fmod(a, b) : 0.0;  // 取余 0 → 0
        out = BPValue::fromNumber(r);
        return true;
    }
    if (type == "Math.Clamp" && pinKey == "result") {
        const double v  = pin("value").toNumber();
        const double mn = pin("min").toNumber();
        const double mx = pin("max").toNumber();
        out = BPValue::fromNumber(std::clamp(v, mn, mx));
        return true;
    }
    if (type == "Math.Random" && pinKey == "result") {
        const double mn = pin("min").toNumber();
        const double mx = pin("max").toNumber();
        const double lo = std::min(mn, mx), hi = std::max(mn, mx);
        out = BPValue::fromNumber(lo + QRandomGenerator::global()->generateDouble() * (hi - lo));
        return true;
    }

    // ── 比较运算（一符一节点）─────────────────────────────────────────────
    if (pinKey == "result"
        && (type == "Cmp.GT" || type == "Cmp.GE" || type == "Cmp.LT"
         || type == "Cmp.LE" || type == "Cmp.EQ" || type == "Cmp.NE")) {
        const BPValue va = pin("a");
        const BPValue vb = pin("b");
        bool res = false;
        if      (type == "Cmp.EQ") res =  va.typedEquals(vb);   // 类型感知相等
        else if (type == "Cmp.NE") res = !va.typedEquals(vb);
        else {                                                  // 大小比较按数值
            const double a = va.toNumber(), b = vb.toNumber();
            if      (type == "Cmp.GT") res = (a >  b);
            else if (type == "Cmp.GE") res = (a >= b);
            else if (type == "Cmp.LT") res = (a <  b);
            else                       res = (a <= b);
        }
        out = BPValue::fromBool(res);
        return true;
    }

    // ── 逻辑运算（一符一节点）─────────────────────────────────────────────
    if (type == "Logic.And" && pinKey == "result") {
        out = BPValue::fromBool(pin("a").toBool() && pin("b").toBool());
        return true;
    }
    if (type == "Logic.Or" && pinKey == "result") {
        out = BPValue::fromBool(pin("a").toBool() || pin("b").toBool());
        return true;
    }
    if (type == "Logic.Not" && pinKey == "result") {
        out = BPValue::fromBool(!pin("value").toBool());
        return true;
    }

    // ── 数组数据节点（纯数据）─────────────────────────────────────────────
    if (type == "Array.Make" && pinKey == "result") {
        out = BPValue::fromArray({});
        return true;
    }
    if (type == "Array.Length" && pinKey == "result") {
        out = BPValue::fromNumber(pin("array").toArray().size());
        return true;
    }
    if (type == "Array.Get" && pinKey == "result") {
        const QList<BPValue> arr = pin("array").toArray();
        const int i = (int)pin("index").toNumber();
        out = (i >= 0 && i < arr.size()) ? arr[i] : BPValue();   // 越界 → Null
        return true;
    }
    if (type == "Array.Contains" && pinKey == "result") {
        const QList<BPValue> arr = pin("array").toArray();
        const BPValue v = pin("value");
        out = BPValue::fromBool(false);
        for (const BPValue& e : arr) if (e.typedEquals(v)) { out = BPValue::fromBool(true); break; }
        return true;
    }

    // 旧「数值比较」(op 输入引脚式)：保留以兼容旧蓝图
    if (type == "Logic.Compare" && pinKey == "result") {
        const double a = pin("a").toNumber();
        const double b = pin("b").toNumber();
        const QString op = pin("op").toString().trimmed();
        bool res = false;
        if      (op == "==") res = (a == b);
        else if (op == "!=") res = (a != b);
        else if (op == "<")  res = (a <  b);
        else if (op == "<=") res = (a <= b);
        else if (op == ">")  res = (a >  b);
        else if (op == ">=") res = (a >= b);
        out = BPValue::fromBool(res);
        return true;
    }

    return false;
}
