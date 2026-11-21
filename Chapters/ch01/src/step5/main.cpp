#include "DirectFrame.h"

#include "Engine/Core/Log.h"
#include "Engine/Platform/ExecutablePath.h"
#include "Engine/Platform/Window.h"
#include "Engine/Renderer/TriangleRenderer.h"
#include "Engine/Renderer/WebGpuContext.h"

#include <cstdlib>
#include <exception>
#include <iostream>

//————————————————————————————————————————————————————————————————————————————————
// 在上一步的帧循环里加入顶点、WGSL 和 Pipeline，直接呈现彩色三角形。
//————————————————————————————————————————————————————————————————————————————————
int main() try {
    using namespace tinyworld;
    Log log;
    Window window("TinyWorld - First Triangle");
    WebGpuContext gpu(window, log);

    // 初始化时上传顶点和编译 WGSL，每帧复用同一组资源。
    TriangleRenderer triangle(gpu, gpu.SurfaceFormat(), ExecutableDirectory() / "triangle.wgsl");
    RunDirectFrames(window, gpu, log, [&](WGPUCommandEncoder encoder, WGPUTextureView target) {
        triangle.Encode(encoder, target);
    });
    return EXIT_SUCCESS;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
}
