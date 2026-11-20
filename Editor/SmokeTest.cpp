#include "Editor/SmokeTest.h"

#include "Engine/Core/Log.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <stdexcept>

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 在完成 GPU 初始化后开始计时。
// 窗口系统有充足时间完成异步调整与恢复。
//————————————————————————————————————————————————————————————————————————————————
SmokeTest::SmokeTest(bool direct)
    : mDeadline(std::chrono::steady_clock::now() + std::chrono::seconds(15)), mDirect(direct) {}

//————————————————————————————————————————————————————————————————————————————————
// 用实际呈现结果和设备像素尺寸验证每个阶段。
// GLFW 窗口请求与观察之间保留主循环，让平台事件自然到达。
//————————————————————————————————————————————————————————————————————————————————
bool SmokeTest::Observe(Window& window, Log& log, PixelSize surface, PixelSize scene, bool presented) {
    if (std::chrono::steady_clock::now() > mDeadline) {
        throw std::runtime_error("Smoke test timed out at window stage " + std::to_string(static_cast<int>(mStage)));
    }
    if (presented) {
        ++mPresented;
    }
    const bool sceneReady = mDirect || (scene.width > 0 && scene.height > 0);
    switch (mStage) {
    case Stage::Initial:
        if (presented && sceneReady && mPresented >= 3) {
            mInitialSurface = surface;
            mInitialScene = scene;
            log.Write("TEST", "initial frames presented");
            glfwSetWindowSize(window.Native(), 1040, 640);
            mStage = Stage::Resized;
        }
        break;
    case Stage::Resized:
        if (presented && surface != mInitialSurface && sceneReady && (mDirect || scene != mInitialScene)) {
            log.Write("TEST", "resize presented with updated target sizes");
            glfwIconifyWindow(window.Native());
            mStage = Stage::Minimized;
        }
        break;
    case Stage::Minimized:
        if (surface.width == 0 && surface.height == 0 && !presented) {
            log.Write("TEST", "minimized window skipped rendering");
            glfwRestoreWindow(window.Native());
            mPresented = 0;
            mStage = Stage::Restored;
        }
        break;
    case Stage::Restored:
        if (presented && sceneReady && mPresented >= 3) {
            log.Write("TEST", "restored window presented new frames");
            return true;
        }
        break;
    }
    return false;
}

}  // namespace tinyworld
