#include "Engine/Renderer/SunShadowMap.h"

#include "Engine/Renderer/WebGpuContext.h"
#include "Engine/Renderer/WorldVertex.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

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
PipelineHandle BuildDepthPipeline(WebGpuContext& gpu, WGPUShaderModule shader, WGPUBindGroupLayout objectLayout) {
    WGPUPipelineLayoutDescriptor layoutDescription = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    layoutDescription.bindGroupLayoutCount = 1;
    layoutDescription.bindGroupLayouts = &objectLayout;
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
    description.depthStencil = &depth;
    return PipelineHandle(wgpuDeviceCreateRenderPipeline(gpu.Device(), &description));
}

}  // namespace

//————————————————————————————————————————————————————————————————————————————————
// 深度纹理在太阳通道中写入，在之后的颜色通道中作为比较采样的输入。
//————————————————————————————————————————————————————————————————————————————————
SunShadowMap::SunShadowMap(WebGpuContext& gpu, WGPUShaderModule shader, WGPUBindGroupLayout objectLayout) {
    wgpuDevicePushErrorScope(gpu.Device(), WGPUErrorFilter_Validation);
    WGPUTextureDescriptor texture = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texture.label = {"Chapter 03 shadow map", WGPU_STRLEN};
    texture.size = {kShadowSize, kShadowSize, 1};
    texture.format = kShadowFormat;
    texture.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
    mTexture.reset(wgpuDeviceCreateTexture(gpu.Device(), &texture));
    if (mTexture) {
        mView.reset(wgpuTextureCreateView(mTexture.get(), nullptr));
    }

    WGPUSamplerDescriptor sampler = WGPU_SAMPLER_DESCRIPTOR_INIT;
    sampler.compare = WGPUCompareFunction_LessEqual;
    sampler.minFilter = WGPUFilterMode_Nearest;
    sampler.magFilter = WGPUFilterMode_Nearest;
    mComparisonSampler.reset(wgpuDeviceCreateSampler(gpu.Device(), &sampler));

    std::array<WGPUBindGroupLayoutEntry, 2> layoutEntries{{
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT, WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
    }};
    layoutEntries[0].visibility = WGPUShaderStage_Fragment;
    layoutEntries[0].texture.sampleType = WGPUTextureSampleType_Depth;
    layoutEntries[0].texture.viewDimension = WGPUTextureViewDimension_2D;
    layoutEntries[1].binding = 1;
    layoutEntries[1].visibility = WGPUShaderStage_Fragment;
    layoutEntries[1].sampler.type = WGPUSamplerBindingType_Comparison;
    WGPUBindGroupLayoutDescriptor layout = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    layout.entryCount = layoutEntries.size();
    layout.entries = layoutEntries.data();
    mSamplingLayout.reset(wgpuDeviceCreateBindGroupLayout(gpu.Device(), &layout));
    gpu.CheckValidation("chapter 03 shadow texture and sampling layout");
    if (!mView || !mComparisonSampler || !mSamplingLayout) {
        throw std::runtime_error("Cannot allocate the sun shadow map");
    }

    std::array<WGPUBindGroupEntry, 2> bindings{{WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT}};
    bindings[0].textureView = mView.get();
    bindings[1].binding = 1;
    bindings[1].sampler = mComparisonSampler.get();
    WGPUBindGroupDescriptor sampling = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    sampling.layout = mSamplingLayout.get();
    sampling.entryCount = bindings.size();
    sampling.entries = bindings.data();
    wgpuDevicePushErrorScope(gpu.Device(), WGPUErrorFilter_Validation);
    mSamplingBinding.reset(wgpuDeviceCreateBindGroup(gpu.Device(), &sampling));
    mDepthPipeline = BuildDepthPipeline(gpu, shader, objectLayout);
    gpu.CheckValidation("chapter 03 shadow binding and depth pipeline");
    if (!mSamplingBinding || !mDepthPipeline) {
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
PassHandle SunShadowMap::Begin(WGPUCommandEncoder encoder) const {
    WGPURenderPassDepthStencilAttachment depth = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    depth.view = mView.get();
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
    return pass;
}

}  // namespace tinyworld
