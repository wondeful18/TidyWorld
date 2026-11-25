#include "Engine/Renderer/SunShadowMap.h"

#include "Engine/Renderer/VegetationRenderer.h"
#include "Engine/Renderer/WebGpuContext.h"
#include "Engine/Renderer/WorldVertex.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/trigonometric.hpp>
#include <glm/geometric.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace tinyworld {
namespace {

constexpr auto kShadowFormat = WGPUTextureFormat_Depth32Float;
constexpr std::uint32_t kShadowSize = 1024;

//————————————————————————————————————————————————————————————————————————————————
// 只声明位置属性。深度图使用与颜色通道相同的顶点缓冲和物体 uniform。
//————————————————————————————————————————————————————————————————————————————————
PipelineHandle BuildDepthPipeline(WebGpuContext& gpu, WGPUShaderModule shader, WGPUBindGroupLayout objectLayout,
                                  WGPUBindGroupLayout depthLayout, bool vegetation) {
    const std::array layouts{objectLayout, depthLayout};
    WGPUPipelineLayoutDescriptor layoutDescription = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    layoutDescription.bindGroupLayoutCount = layouts.size();
    layoutDescription.bindGroupLayouts = layouts.data();
    PipelineLayoutHandle pipelineLayout(wgpuDeviceCreatePipelineLayout(gpu.Device(), &layoutDescription));
    if (!pipelineLayout) {
        return PipelineHandle{};
    }

    WGPUVertexAttribute position = WGPU_VERTEX_ATTRIBUTE_INIT;
    position.format = WGPUVertexFormat_Float32x3;
    position.offset = offsetof(WorldVertex, geometry) + offsetof(SceneVertex, position);
    WGPUVertexBufferLayout vertices = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
    vertices.arrayStride = sizeof(WorldVertex);
    vertices.stepMode = WGPUVertexStepMode_Vertex;
    vertices.attributeCount = 1;
    vertices.attributes = &position;

    WGPUDepthStencilState depth = WGPU_DEPTH_STENCIL_STATE_INIT;
    depth.format = kShadowFormat;
    depth.depthCompare = WGPUCompareFunction_Less;
    depth.depthWriteEnabled = WGPUOptionalBool_True;

    WGPURenderPipelineDescriptor description = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    description.label = {"Chapter 03 sun depth pipeline", WGPU_STRLEN};
    description.layout = pipelineLayout.get();
    description.vertex.module = shader;
    description.vertex.entryPoint = {"vsSunDepth", WGPU_STRLEN};
    description.vertex.bufferCount = 1;
    description.vertex.buffers = &vertices;
    description.primitive.cullMode = WGPUCullMode_Back;
    const VegetationVertexLayout vegetationLayout;
    if (vegetation) {
        description.label = {"Vegetation sun depth pipeline", WGPU_STRLEN};
        description.vertex.entryPoint = {"vsVegetationDepth", WGPU_STRLEN};
        description.vertex.bufferCount = vegetationLayout.buffers.size();
        description.vertex.buffers = vegetationLayout.buffers.data();
    }
    description.depthStencil = &depth;
    return PipelineHandle(wgpuDeviceCreateRenderPipeline(gpu.Device(), &description));
}

}  // namespace

//————————————————————————————————————————————————————————————————————————————————
// 深度纹理在太阳通道中写入，在之后的颜色通道中作为比较采样的输入。
//————————————————————————————————————————————————————————————————————————————————
SunShadowMap::SunShadowMap(WebGpuContext& gpu, WGPUShaderModule shader, WGPUBindGroupLayout objectLayout,
                           bool vegetation, bool cascades) {
    const unsigned layers = cascades ? 4 : 1;
    wgpuDevicePushErrorScope(gpu.Device(), WGPUErrorFilter_Validation);
    WGPUTextureDescriptor texture = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texture.label = {"Chapter 03 shadow map", WGPU_STRLEN};
    texture.size = {kShadowSize, kShadowSize, layers};
    texture.format = kShadowFormat;
    texture.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
    mTexture.reset(wgpuDeviceCreateTexture(gpu.Device(), &texture));
    if (mTexture) {
        WGPUTextureViewDescriptor view = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        view.dimension = WGPUTextureViewDimension_2DArray;
        view.arrayLayerCount = layers;
        mView.reset(wgpuTextureCreateView(mTexture.get(), &view));
        view.dimension = WGPUTextureViewDimension_2D;
        view.arrayLayerCount = 1;
        for (unsigned layer = 0; layer < layers; ++layer) {
            view.baseArrayLayer = layer;
            mLayerViews[layer].reset(wgpuTextureCreateView(mTexture.get(), &view));
        }
    }

    WGPUSamplerDescriptor sampler = WGPU_SAMPLER_DESCRIPTOR_INIT;
    sampler.compare = WGPUCompareFunction_LessEqual;
    sampler.minFilter = WGPUFilterMode_Nearest;
    sampler.magFilter = WGPUFilterMode_Nearest;
    mComparisonSampler.reset(wgpuDeviceCreateSampler(gpu.Device(), &sampler));

    std::array<WGPUBindGroupLayoutEntry, 3> layoutEntries{{
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT, WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT, WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
    }};
    layoutEntries[0].visibility = WGPUShaderStage_Fragment;
    layoutEntries[0].texture.sampleType = WGPUTextureSampleType_Depth;
    layoutEntries[0].texture.viewDimension = WGPUTextureViewDimension_2DArray;
    layoutEntries[1].binding = 1;
    layoutEntries[1].visibility = WGPUShaderStage_Fragment;
    layoutEntries[1].sampler.type = WGPUSamplerBindingType_Comparison;
    layoutEntries[2].binding = 2;
    layoutEntries[2].visibility = WGPUShaderStage_Fragment;
    layoutEntries[2].buffer.type = WGPUBufferBindingType_Uniform;
    layoutEntries[2].buffer.minBindingSize = sizeof(SunShadowFrame);
    WGPUBindGroupLayoutDescriptor layout = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    layout.entryCount = layoutEntries.size();
    layout.entries = layoutEntries.data();
    mSamplingLayout.reset(wgpuDeviceCreateBindGroupLayout(gpu.Device(), &layout));
    gpu.CheckValidation("chapter 03 shadow texture and sampling layout");
    if (!mView || !mComparisonSampler || !mSamplingLayout) {
        throw std::runtime_error("Cannot allocate the sun shadow map");
    }

    const auto makeUniform = [&](std::size_t size) {
        WGPUBufferDescriptor description = WGPU_BUFFER_DESCRIPTOR_INIT;
        description.size = size;
        description.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        return BufferHandle(wgpuDeviceCreateBuffer(gpu.Device(), &description));
    };
    mFrameUniform = makeUniform(sizeof(SunShadowFrame));
    std::array<WGPUBindGroupEntry, 3> bindings{{
        WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT,
    }};
    bindings[0].textureView = mView.get();
    bindings[1].binding = 1;
    bindings[1].sampler = mComparisonSampler.get();
    bindings[2].binding = 2;
    bindings[2].buffer = mFrameUniform.get();
    bindings[2].size = sizeof(SunShadowFrame);
    WGPUBindGroupDescriptor sampling = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    sampling.layout = mSamplingLayout.get();
    sampling.entryCount = bindings.size();
    sampling.entries = bindings.data();
    wgpuDevicePushErrorScope(gpu.Device(), WGPUErrorFilter_Validation);
    mSamplingBinding.reset(wgpuDeviceCreateBindGroup(gpu.Device(), &sampling));
    auto matrixLayout = layoutEntries[2];
    matrixLayout.binding = 3;
    matrixLayout.visibility = WGPUShaderStage_Vertex;
    matrixLayout.buffer.minBindingSize = sizeof(glm::mat4);
    layout.entryCount = 1;
    layout.entries = &matrixLayout;
    mDepthLayout.reset(wgpuDeviceCreateBindGroupLayout(gpu.Device(), &layout));
    for (unsigned layer = 0; layer < layers; ++layer) {
        mLayerMatrices[layer] = makeUniform(sizeof(glm::mat4));
        WGPUBindGroupEntry matrix = WGPU_BIND_GROUP_ENTRY_INIT;
        matrix.binding = 3;
        matrix.buffer = mLayerMatrices[layer].get();
        matrix.size = sizeof(glm::mat4);
        WGPUBindGroupDescriptor binding = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        binding.layout = mDepthLayout.get();
        binding.entryCount = 1;
        binding.entries = &matrix;
        mLayerBindings[layer].reset(wgpuDeviceCreateBindGroup(gpu.Device(), &binding));
    }
    mDepthPipeline = BuildDepthPipeline(gpu, shader, objectLayout, mDepthLayout.get(), false);
    if (vegetation) mVegetationPipeline = BuildDepthPipeline(gpu, shader, objectLayout, mDepthLayout.get(), true);
    gpu.CheckValidation("chapter 03 shadow binding and depth pipeline");
    if (!mSamplingBinding || !mDepthPipeline || (vegetation && !mVegetationPipeline)) {
        throw std::runtime_error("Cannot bind the sun shadow map or create its depth pipeline");
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 平行阳光使用正交投影。由太阳方位求右向量，仰角到 90 度时视图仍有效。
// 投影范围由场景尺寸决定，游览相机移动时阴影图仍固定在世界里。
//————————————————————————————————————————————————————————————————————————————————
glm::mat4 SunShadowMap::ViewProjection(glm::vec3 towardSun, float azimuth, float rangeScale) {
    const glm::vec3 center{0.0f, 1.5f, 0.0f};
    const float yaw = glm::radians(azimuth);
    const glm::vec3 right{std::cos(yaw), 0.0f, -std::sin(yaw)};
    const glm::vec3 up = glm::cross(towardSun, right);
    const auto view = glm::lookAtRH(center + (16.0f * rangeScale) * towardSun, center, up);
    const float halfWidth = 8.0f * rangeScale;
    return glm::orthoRH_ZO(-halfWidth, halfWidth, -halfWidth, halfWidth, 4.0f * rangeScale, 28.0f * rangeScale) * view;
}

//————————————————————————————————————————————————————————————————————————————————
// 只绑定深度附件并保留结果，让之后的颜色通道读取同一张图。
//————————————————————————————————————————————————————————————————————————————————
PassHandle SunShadowMap::Begin(WGPUCommandEncoder encoder, unsigned layer) const {
    WGPURenderPassDepthStencilAttachment depth = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    depth.view = mLayerViews[layer].get();
    depth.depthLoadOp = WGPULoadOp_Clear;
    depth.depthStoreOp = WGPUStoreOp_Store;
    depth.depthClearValue = 1.0f;
    WGPURenderPassDescriptor description = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    description.label = {"Chapter 03 sun depth pass", WGPU_STRLEN};
    description.depthStencilAttachment = &depth;
    PassHandle pass(wgpuCommandEncoderBeginRenderPass(encoder, &description));
    if (!pass) {
        throw std::runtime_error("Cannot begin the sun depth pass");
    }
    wgpuRenderPassEncoderSetPipeline(pass.get(), mDepthPipeline.get());
    wgpuRenderPassEncoderSetBindGroup(pass.get(), 1, mLayerBindings[layer].get(), 0, nullptr);
    return pass;
}

//————————————————————————————————————————————————————————————————————————————————
// 将每段视锥放入稳定半径的球，给位于视锥上游的遮挡物额外留出深度。
//————————————————————————————————————————————————————————————————————————————————
SunShadowFrame SunShadowMap::FitCascades(glm::vec3 eye, glm::vec3 forward, const glm::mat4& projection,
                                         glm::vec3 towardSun, float azimuth) {
    SunShadowFrame frame;
    frame.splits = {15.0f, 50.0f, 160.0f, 350.0f};
    frame.eyeAndCount = {eye, 4.0f};
    frame.forward = {forward, 0.0f};
    const float yaw = glm::radians(azimuth);
    const glm::vec3 right{std::cos(yaw), 0.0f, -std::sin(yaw)};
    const glm::vec3 up = glm::normalize(glm::cross(towardSun, right));
    float near = 0.1f;
    for (unsigned i = 0; i < frame.matrices.size(); ++i) {
        const float far = frame.splits[i];
        const float depth = (far - near) * 0.5f;
        const float width = far / projection[0][0];
        const float height = far / projection[1][1];
        const float radius = std::ceil(std::sqrt(width * width + height * height + depth * depth) + 2.0f);
        auto center = eye + (near + depth) * forward;
        const float texel = 2.0f * radius / kShadowSize;
        const auto snap = [texel](float value) { return std::round(value / texel) * texel - value; };
        center += right * snap(glm::dot(center, right)) + up * snap(glm::dot(center, up));
        const auto view = glm::lookAtRH(center + (radius + 48.0f) * towardSun, center, up);
        frame.matrices[i] = glm::orthoRH_ZO(-radius, radius, -radius, radius, 1.0f, 2.0f * radius + 96.0f) * view;
        near = far;
    }
    return frame;
}

//————————————————————————————————————————————————————————————————————————————————
// 每级绑定独立的矩阵缓冲。编码多条深度通道时，队列写入不会覆盖前一条通道的数据。
//————————————————————————————————————————————————————————————————————————————————
void SunShadowMap::Upload(WGPUQueue queue, const SunShadowFrame& frame) {
    wgpuQueueWriteBuffer(queue, mFrameUniform.get(), 0, &frame, sizeof(frame));
    for (unsigned i = 0; i < static_cast<unsigned>(frame.eyeAndCount.w); ++i) {
        wgpuQueueWriteBuffer(queue, mLayerMatrices[i].get(), 0, &frame.matrices[i], sizeof(glm::mat4));
    }
}

}  // namespace tinyworld
