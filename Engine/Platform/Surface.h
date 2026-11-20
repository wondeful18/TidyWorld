#pragma once

#include <webgpu/webgpu.h>

struct GLFWwindow;

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 将现有 GLFW 窗口绑定到 WebGPU Instance。
// 返回给调用方一个拥有的 Surface 引用，窗口须比 Surface 长寿。
//————————————————————————————————————————————————————————————————————————————————
[[nodiscard]] WGPUSurface CreateWindowSurface(WGPUInstance instance, GLFWwindow* window);

}  // namespace tinyworld
