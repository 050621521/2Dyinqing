#pragma once
#include <QString>
#include <QList>
#include <QRect>
#include <QJsonObject>

// 动画片段：精灵表中某一行的一段连续帧
struct AnimClip {
    QString name;            // 逻辑名，蓝图按此名播放（如 "walk_down"）
    QString label;           // 中文显示名（如 "下"）
    int     row        = 0;  // 在精灵表中的行号
    int     startCol   = 0;  // 起始列
    int     frameCount = 1;  // 帧数
    double  fps        = 12.0;
    bool    loop       = true;
};

// 动画资源：一张精灵表 + 网格 + 一组片段。对应 .anim 文件。
struct AnimationAsset {
    QString sheet;           // 精灵表路径（绝对路径，与 spritePath 约定一致）
    int frameW = 0, frameH = 0;
    int cols   = 1, rows   = 1;
    QList<AnimClip> clips;

    bool load(const QString& path);
    bool save(const QString& path) const;

    const AnimClip* findClip(const QString& name) const;

    // 计算片段第 frameIndex 帧在精灵表中的源矩形
    QRect frameRect(const AnimClip& clip, int frameIndex) const;

    // 从素材自带的「元数据 JSON」（含 frame_size / sheet_grid / directions）构建。
    // sheetAbsPath 为精灵表在项目内的绝对路径。
    static AnimationAsset fromMetadataJson(const QJsonObject& meta, const QString& sheetAbsPath);
};
