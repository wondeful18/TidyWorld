#include "DirectFrame.h"

#include "Engine/Core/Log.h"
#include "Engine/Platform/Window.h"
#include "Engine/Renderer/RenderPass.h"
#include "Engine/Renderer/WebGpuContext.h"

#include <cstdlib>
#include <exception>
#include <iostream>

//————————————————————————————————————————————————————————————————————————————————
// 建立窗口的 WebGPU 上下文，只用 Clear 和 Store 填满颜色目标。
//————————————————————————————————————————————————————————————————————————————————
int main() try {
    using namespace tinyworld;
    Log log;
    Window window("TinyWorld - First Clear");
    WebGpuContext gpu(window, log);
    RunDirectFrames(window, gpu, log, [](WGPUCommandEncoder encoder, WGPUTextureView target) {
        // 只开始和结束通道，Clear 操作已经给窗口纹理的每个像素着色。
        auto pass = BeginColorPass(encoder, target, {0.0, 0.0, 1.0, 1.0}, "First clear");
        wgpuRenderPassEncoderEnd(pass.get());
    });
    return EXIT_SUCCESS;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
}
