#include "Recorder.h"
#include "models/LevelDocument.h"
#include "models/ActorTypeUtils.h"
#include "BPRuntime.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTextStream>

Recorder& Recorder::instance() {
    static Recorder s;
    return s;
}

QString Recorder::fmtTime(qint64 ms) const {
    qint64 totalMs = ms;
    int m = int(totalMs / 60000);
    int s = int((totalMs % 60000) / 1000);
    int milli = int(totalMs % 1000);
    return QString("%1:%2.%3")
        .arg(m, 2, 10, QChar('0'))
        .arg(s, 2, 10, QChar('0'))
        .arg(milli, 3, 10, QChar('0'));
}

// ── 录制控制 ─────────────────────────────────────────────────────────
void Recorder::startRecording(const ProjectInfo& project,
                              const QString& levelName, int ppu,
                              const QString& tool) {
    m_project = project;
    m_timeline.clear();
    m_snapshots.clear();
    m_console.clear();
    m_startStamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    m_envLine = QString("关卡 %1 | PPU %2 | 工具 %3")
                    .arg(levelName.isEmpty() ? "(无)" : levelName)
                    .arg(ppu)
                    .arg(tool);
    m_clock.start();
    m_recording = true;
    // 接管 qDebug（链式保存旧 handler）
    m_prevHandler = reinterpret_cast<void*>(qInstallMessageHandler(messageHandler));
}

QString Recorder::stopRecording() {
    if (!m_recording) return {};
    m_recording = false;
    qInstallMessageHandler(reinterpret_cast<QtMessageHandler>(m_prevHandler));
    m_prevHandler = nullptr;

    if (m_project.path.isEmpty()) return {};
    QDir dir(m_project.path + "/.recordings");
    if (!dir.exists() && !dir.mkpath(".")) return {};

    const QString fileStamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    const QString md = buildMarkdown();

    auto writeFile = [&](const QString& path) -> bool {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
            return false;
        QTextStream ts(&f);
        ts.setEncoding(QStringConverter::Utf8);
        ts << md;
        return true;
    };

    const QString stamped = dir.filePath(QString("recording-%1.md").arg(fileStamp));
    writeFile(stamped);
    writeFile(dir.filePath("latest.md"));   // 覆盖最新副本，AI 取这份
    return stamped;
}

// ── 收集 ─────────────────────────────────────────────────────────────
void Recorder::log(const QString& category, const QString& text) {
    if (!m_recording) return;
    if (!m_timeline.isEmpty()) {
        Entry& last = m_timeline.last();
        if (last.category == category && last.text == text) { ++last.repeat; return; }
    }
    m_timeline.append({ m_clock.elapsed(), category, text, 1 });
}

void Recorder::snapshot(LevelDocument* doc, BPRuntime* runtime, const QString& label) {
    if (!m_recording) return;
    static const QList<ActorData> kEmpty;
    const QList<ActorData>& actors =
        runtime ? runtime->actors() : (doc ? doc->sortedActors() : kEmpty);

    QString block = QString("## 状态快照 @%1 %2\n").arg(fmtTime(m_clock.elapsed()), label);
    if (actors.isEmpty()) {
        block += "- (无 Actor)\n";
    }
    for (const ActorData& a : actors) {
        block += QString("- %1 (%2) pos(%3,%4) rot%5 scale(%6,%7) 层=%8 order=%9%10\n")
                     .arg(a.name)
                     .arg(bpClassLabel(a.bpClass))
                     .arg(a.x).arg(a.y)
                     .arg(a.rotation)
                     .arg(a.scaleX).arg(a.scaleY)
                     .arg(a.sortingLayer)
                     .arg(a.orderInLayer)
                     .arg(a.cameraIsMain ? " main=true" : "");
    }
    if (runtime && !runtime->printLog().isEmpty()) {
        block += QString("  打印累计: %1\n").arg(runtime->printLog().join(" / "));
    }
    m_snapshots.append(block);
}

// ── 输出 ─────────────────────────────────────────────────────────────
QString Recorder::buildMarkdown() const {
    QString out;
    out += QString("# 复现录制 %1\n").arg(m_startStamp);
    out += QString("环境: %1\n\n").arg(m_envLine);

    out += "## 时间线\n";
    if (m_timeline.isEmpty()) out += "(无操作)\n";
    for (const Entry& e : m_timeline) {
        out += QString("[%1] %2 %3").arg(fmtTime(e.ms), e.category, e.text);
        if (e.repeat > 1) out += QString(" ×%1").arg(e.repeat);
        out += "\n";
    }
    out += "\n";

    for (const QString& snap : m_snapshots) out += snap + "\n";

    out += "## 控制台日志\n";
    if (m_console.isEmpty()) out += "(无)\n";
    else for (const QString& line : m_console) out += line + "\n";

    return out;
}

// ── qDebug 接管 ──────────────────────────────────────────────────────
void Recorder::messageHandler(QtMsgType type, const QMessageLogContext& ctx,
                              const QString& msg) {
    Recorder& r = instance();
    if (r.m_recording) {
        const char* tag = "qDebug";
        switch (type) {
            case QtWarningMsg:  tag = "qWarning";  break;
            case QtCriticalMsg: tag = "qCritical"; break;
            case QtFatalMsg:    tag = "qFatal";    break;
            default: break;
        }
        r.m_console.append(QString("[%1] %2").arg(tag, msg));
    }
    // 链式调用旧 handler，保持正常打印
    if (r.m_prevHandler)
        reinterpret_cast<QtMessageHandler>(r.m_prevHandler)(type, ctx, msg);
    else
        fprintf(stderr, "%s\n", msg.toLocal8Bit().constData());
}
