#include "Engine/Renderer/RenderPass.h"

#include <stdexcept>
#include <string>

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 创建有单个颜色附件的 WebGPU Render Pass，可绑定同尺寸的深度附件。
// Store 操作保留绘制结果，供后续采样或窗口呈现。
//————————————————————————————————————————————————————————————————————————————————
PassHandle BeginColorPass(WGPUCommandEncoder encoder, WGPUTextureView target,
                          WGPUColor clearColor, const char* label, WGPUTextureView depthTarget) {
    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view = target;
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;
    color.clearValue = clearColor;

    WGPURenderPassDescriptor descriptor = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    descriptor.label = {label, WGPU_STRLEN};
    descriptor.colorAttachmentCount = 1;
    descriptor.colorAttachments = &color;
    WGPURenderPassDepthStencilAttachment depth = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    if (depthTarget) {
        depth.view = depthTarget;
        depth.depthLoadOp = WGPULoadOp_Clear;
        depth.depthStoreOp = WGPUStoreOp_Discard;
        depth.depthClearValue = 1.0f;
        descriptor.depthStencilAttachment = &depth;
    }
    PassHandle pass(wgpuCommandEncoderBeginRenderPass(encoder, &descriptor));
    if (!pass) {
        throw std::runtime_error(std::string("Cannot begin render pass: ") + label);
    }
    return pass;
}

}  // namespace tinyworld
