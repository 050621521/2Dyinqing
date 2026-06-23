#pragma once
#include <QString>
#include <QStringList>
#include <QElapsedTimer>
#include <QList>
#include "models/ProjectInfo.h"

class LevelDocument;
class BPRuntime;

// 复现录制器（单例）：手动开始/停止，收集语义化操作时间线 + 状态快照 +
// 控制台日志，停止时写出一份 AI 可直接读取的 Markdown 文件。
// 设计文档：docs/superpowers/specs/2026-06-19-reproduction-recording-design.md
class Recorder {
public:
    static Recorder& instance();

    bool isRecording() const { return m_recording; }

    // 开始录制：记录环境（关卡 / PPU / 当前工具），接管 qDebug，清空缓冲。
    void startRecording(const ProjectInfo& project,
                        const QString& levelName, int ppu, const QString& tool);

    // 停止录制：还原 message handler，写出文件，返回文件绝对路径（失败返回空）。
    QString stopRecording();

    // 追加一条时间线条目；连续相同（类别+文本）自动合并为 “×N”，防帧级刷屏。
    void log(const QString& category, const QString& text);

    // 在关键节点拍状态快照。runtime 非空则取运行时快照，否则取静态 doc。
    void snapshot(LevelDocument* doc, BPRuntime* runtime, const QString& label);

private:
    Recorder() = default;

    struct Entry { qint64 ms; QString category; QString text; int repeat = 1; };

    QString fmtTime(qint64 ms) const;          // ms → "mm:ss.mmm"
    QString buildMarkdown() const;

    static void messageHandler(QtMsgType, const QMessageLogContext&, const QString&);

    bool          m_recording = false;
    QElapsedTimer m_clock;
    ProjectInfo   m_project;
    QString       m_envLine;                   // 文件头“环境”行
    QString       m_startStamp;                // 文件名 / 标题时间戳
    QList<Entry>  m_timeline;
    QStringList   m_snapshots;
    QStringList   m_console;

    void* m_prevHandler = nullptr;             // 还原用（实为 QtMessageHandler）
};
