#include "Engine/Platform/Window.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 初始化 GLFW 并创建窗口。
// GLFW_NO_API 让渲染上下文完全由 WebGPU 管理。
//————————————————————————————————————————————————————————————————————————————————
Window::Window(const char* title, bool visible) {
    glfwSetErrorCallback([](int code, const char* message) {
        std::fprintf(stderr, "[GLFW %d] %s\n", code, message);
    });
    if (!glfwInit()) {
        throw std::runtime_error("GLFW initialization failed");
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, visible ? GLFW_TRUE : GLFW_FALSE);
    mHandle = glfwCreateWindow(1280, 720, title, nullptr, nullptr);
    if (!mHandle) {
        glfwTerminate();
        throw std::runtime_error("GLFW window creation failed");
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 按窗口、GLFW 会话的顺序释放平台资源。
// 应用在销毁本对象前先销毁 GPU 上下文。
//————————————————————————————————————————————————————————————————————————————————
Window::~Window() {
    glfwDestroyWindow(mHandle);
    glfwTerminate();
}

//————————————————————————————————————————————————————————————————————————————————
// 提供供平台桥接与 UI 后端借用的 GLFW 句柄。
// 生命周期由 Window 实例管理。
//————————————————————————————————————————————————————————————————————————————————
GLFWwindow* Window::Native() const {
    return mHandle;
}

//————————————————————————————————————————————————————————————————————————————————
// 将 GLFW 返回值转换成 WebGPU 使用的非负尺寸。
// 即使窗口系统在最小化时保留像素尺寸，也先暂停渲染。
//————————————————————————————————————————————————————————————————————————————————
PixelSize Window::FramebufferPixels() const {
    if (glfwGetWindowAttrib(mHandle, GLFW_ICONIFIED)) {
        return {};
    }
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(mHandle, &width, &height);
    return {static_cast<std::uint32_t>(std::max(width, 0)),
            static_cast<std::uint32_t>(std::max(height, 0))};
}

//————————————————————————————————————————————————————————————————————————————————
// 读取 GLFW 保存的逻辑坐标。
// 保留小数精度以便高 DPI 环境中观察坐标变化。
//————————————————————————————————————————————————————————————————————————————————
std::pair<double, double> Window::CursorPosition() const {
    double x = 0;
    double y = 0;
    glfwGetCursorPos(mHandle, &x, &y);
    return {x, y};
}

}  // namespace tinyworld
