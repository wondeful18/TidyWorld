#include "DirectFrame.h"

#include "Engine/Core/Log.h"
#include "Engine/Platform/Window.h"
#include "Engine/Renderer/WebGpuContext.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <stdexcept>

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 每帧获取窗口纹理，调用该步的绘制代码，再把结果提交到窗口。
//————————————————————————————————————————————————————————————————————————————————
void RunDirectFrames(Window& window, WebGpuContext& gpu, Log& log,
                     const std::function<void(WGPUCommandEncoder, WGPUTextureView)>& encode) {
    bool firstFrame = true;
    while (!glfwWindowShouldClose(window.Native())) {
        glfwPollEvents();
        gpu.ProcessEvents();
        auto texture = gpu.AcquireFrame(window.FramebufferPixels());
        if (!texture) {
            // 最小化或暂时无法获取 Surface 纹理时暂停绘制。
            glfwWaitEventsTimeout(0.03);
            continue;
        }

        TextureViewHandle view(wgpuTextureCreateView(texture.get(), nullptr));
        EncoderHandle encoder(wgpuDeviceCreateCommandEncoder(gpu.Device(), nullptr));
        if (!view || !encoder) {
            throw std::runtime_error("Cannot create frame view or command encoder");
        }
        encode(encoder.get(), view.get());

        CommandHandle commands(wgpuCommandEncoderFinish(encoder.get(), nullptr));
        if (!commands) {
            throw std::runtime_error("Cannot finish the frame command buffer");
        }
        const auto command = commands.get();
        wgpuQueueSubmit(gpu.Queue(), 1, &command);
        gpu.Present();
        if (firstFrame) {
            log.Write("FRAME", "first frame presented");
            firstFrame = false;
        }
    }

    gpu.WaitForSubmittedWork();
}

}  // namespace tinyworld
