#include "Engine/Platform/Surface.h"

#define WIN32_LEAN_AND_MEAN
#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 使用 GLFW 的 HWND 和它所属的模块创建 Win32 Surface。
// 平台句柄由 GLFW 与 Windows 管理。
//————————————————————————————————————————————————————————————————————————————————
WGPUSurface CreateWindowSurface(WGPUInstance instance, GLFWwindow* window) {
    const HWND hwnd = glfwGetWin32Window(window);
    WGPUSurfaceSourceWindowsHWND source = WGPU_SURFACE_SOURCE_WINDOWS_HWND_INIT;
    source.hwnd = hwnd;
    source.hinstance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
    WGPUSurfaceDescriptor descriptor = WGPU_SURFACE_DESCRIPTOR_INIT;
    descriptor.nextInChain = &source.chain;
    return wgpuInstanceCreateSurface(instance, &descriptor);
}

}  // namespace tinyworld
