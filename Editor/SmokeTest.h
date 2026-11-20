#pragma once

#include "Engine/Platform/Window.h"

#include <chrono>

namespace tinyworld {

class Log;

//————————————————————————————————————————————————————————————————————————————————
// 通过真实窗口执行一轮第一章的自动回归操作。
// 分别观察初次呈现、缩放、最小化与恢复后的帧。
//————————————————————————————————————————————————————————————————————————————————
class SmokeTest {
public:
    //————————————————————————————————————————————————————————————————————————————————
    // 设定本次验证的截止时间并记录需要验证的呈现路径。
    //————————————————————————————————————————————————————————————————————————————————
    explicit SmokeTest(bool direct);

    //————————————————————————————————————————————————————————————————————————————————
    // 检查当前观察结果，必要时安排下一次窗口操作。
    // true 表示所有阶段通过，调用方可以在完成 GPU 工作后退出。
    //————————————————————————————————————————————————————————————————————————————————
    bool Observe(Window& window, Log& log, PixelSize surface, PixelSize scene, bool presented);

private:
    enum class Stage { Initial, Resized, Minimized, Restored };  // 等待确认的窗口状态

    std::chrono::steady_clock::time_point mDeadline;  // 验证截止时间，使用单调时钟
    Stage mStage = Stage::Initial;                   // 当前需要观测的事件
    PixelSize mInitialSurface{};                     // 缩放前用于对照的 Surface 像素尺寸
    PixelSize mInitialScene{};                       // 缩放前用于对照的场景纹理像素尺寸
    unsigned mPresented{};                           // 当前阶段累计的成功呈现次数
    bool mDirect;                                    // true 时只检查 Surface，无场景纹理
};

}  // namespace tinyworld
