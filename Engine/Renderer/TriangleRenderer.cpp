#include "Engine/Renderer/TriangleRenderer.h"

#include "Engine/Renderer/Color.h"
#include "Engine/Renderer/RenderPass.h"
#include "Engine/Renderer/ShaderModule.h"
#include "Engine/Renderer/WebGpuContext.h"

#include <array>
#include <cstddef>
#include <stdexcept>

namespace tinyworld {
namespace {

//————————————————————————————————————————————————————————————————————————————————
// 定义 GPU 顶点缓冲的紧密排列。
// 用 offsetof 将实际 C++ 字节偏移交给 Pipeline。
//————————————————————————————————————————————————————————————————————————————————
struct Vertex {
    std::array<float, 2> position;  // WebGPU 裁剪空间的 XY 位置
    std::array<float, 3> color;     // 供本章显示的 SDR RGB 颜色
};

constexpr std::array kVertices{
    Vertex{{0.0f, 0.64f}, {1.0f, 0.30f, 0.32f}},
    Vertex{{-0.58f, -0.58f}, {0.17f, 0.92f, 0.75f}},
    Vertex{{0.58f, -0.58f}, {0.31f, 0.44f, 1.0f}},
};

//————————————————————————————————————————————————————————————————————————————————
// 按实际颜色目标验证 WGSL 并创建顶点布局与 Pipeline。
// sRGB 目标会执行硬件编码，因此片元入口先把显示值转换为线性值。
//————————————————————————————————————————————————————————————————————————————————
PipelineHandle CreatePipeline(WebGpuContext& gpu, WGPUTextureFormat format,
                              const std::filesystem::path& path, bool srgb) {
    const auto shader = LoadWgslModule(gpu, path);

    std::array<WGPUVertexAttribute, 2> attributes{{WGPU_VERTEX_ATTRIBUTE_INIT, WGPU_VERTEX_ATTRIBUTE_INIT}};
    attributes[0].format = WGPUVertexFormat_Float32x2;
    attributes[0].offset = offsetof(Vertex, position);
    attributes[0].shaderLocation = 0;
    attributes[1].format = WGPUVertexFormat_Float32x3;
    attributes[1].offset = offsetof(Vertex, color);
    attributes[1].shaderLocation = 1;
    WGPUVertexBufferLayout layout = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
    layout.arrayStride = sizeof(Vertex);
    layout.stepMode = WGPUVertexStepMode_Vertex;
    layout.attributeCount = attributes.size();
    layout.attributes = attributes.data();

    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = format;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = shader.get();
    fragment.entryPoint = {srgb ? "fsLinear" : "fsDisplay", WGPU_STRLEN};
    fragment.targetCount = 1;
    fragment.targets = &target;

    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.label = {"TinyWorld triangle pipeline", WGPU_STRLEN};
    descriptor.vertex.module = shader.get();
    descriptor.vertex.entryPoint = {"vsMain", WGPU_STRLEN};
    descriptor.vertex.bufferCount = 1;
    descriptor.vertex.buffers = &layout;
    descriptor.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    descriptor.fragment = &fragment;

    wgpuDevicePushErrorScope(gpu.Device(), WGPUErrorFilter_Validation);
    PipelineHandle pipeline(wgpuDeviceCreateRenderPipeline(gpu.Device(), &descriptor));
    gpu.CheckValidation("triangle render pipeline");
    if (!pipeline) {
        throw std::runtime_error("Cannot create triangle render pipeline");
    }
    return pipeline;
}

}  // namespace

//————————————————————————————————————————————————————————————————————————————————
// 初始化三角形的颜色约定、Pipeline 与静态顶点数据。
// 顶点缓冲只在启动时写入，帧循环只绑定它。
//————————————————————————————————————————————————————————————————————————————————
TriangleRenderer::TriangleRenderer(WebGpuContext& gpu, WGPUTextureFormat colorFormat,
                                   const std::filesystem::path& shaderPath) {
    const bool srgb = IsSrgbFormat(colorFormat);
    mClearColor = {0.055, 0.078, 0.11, 1.0};
    if (srgb) {
        mClearColor.r = DisplayToLinear(mClearColor.r);
        mClearColor.g = DisplayToLinear(mClearColor.g);
        mClearColor.b = DisplayToLinear(mClearColor.b);
    }
    mPipeline = CreatePipeline(gpu, colorFormat, shaderPath, srgb);

    WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
    descriptor.label = {"Triangle vertices", WGPU_STRLEN};
    descriptor.size = sizeof(kVertices);
    descriptor.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    mVertexBuffer.reset(wgpuDeviceCreateBuffer(gpu.Device(), &descriptor));
    if (!mVertexBuffer) {
        throw std::runtime_error("Cannot create triangle vertex buffer");
    }
    wgpuQueueWriteBuffer(gpu.Queue(), mVertexBuffer.get(), 0, kVertices.data(), sizeof(kVertices));
}

//————————————————————————————————————————————————————————————————————————————————
// 绑定顶点缓冲与 Pipeline，然后绘制三个顶点。
// 通道结束后离屏纹理可作为下一通道的输入。
//————————————————————————————————————————————————————————————————————————————————
void TriangleRenderer::Encode(WGPUCommandEncoder encoder, WGPUTextureView target) const {
    auto pass = BeginColorPass(encoder, target, mClearColor, "Triangle pass");
    wgpuRenderPassEncoderSetPipeline(pass.get(), mPipeline.get());
    wgpuRenderPassEncoderSetVertexBuffer(pass.get(), 0, mVertexBuffer.get(), 0, sizeof(kVertices));
    wgpuRenderPassEncoderDraw(pass.get(), static_cast<std::uint32_t>(kVertices.size()), 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass.get());
}

}  // namespace tinyworld
