#include "Engine/Renderer/ToneMapping.h"

#include "Engine/Renderer/Color.h"
#include "Engine/Renderer/RenderPass.h"
#include "Engine/Renderer/ShaderModule.h"
#include "Engine/Renderer/WebGpuContext.h"
#include "Engine/Renderer/WorldDraw.h"

#include <array>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace tinyworld {

ToneMapping::ToneMapping(WebGpuContext& gpu, WGPUTextureFormat displayFormat, const std::filesystem::path& shaders)
    : mGpu(gpu) {
    const auto shader = LoadWgslModule(gpu, shaders / "tone_mapping.wgsl", {"output_color.wgsl"});
    const auto encoding = UnormEncoding(displayFormat);
    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = displayFormat;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = shader.get();
    fragment.constantCount = 1;
    fragment.constants = &encoding;
    fragment.targetCount = 1;
    fragment.targets = &target;
    WGPURenderPipelineDescriptor pipeline = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    pipeline.vertex.module = shader.get();
    pipeline.fragment = &fragment;
    wgpuDevicePushErrorScope(gpu.Device(), WGPUErrorFilter_Validation);
    mPipeline.reset(wgpuDeviceCreateRenderPipeline(gpu.Device(), &pipeline));
    gpu.CheckValidation("tone mapping pipeline");
    if (!mPipeline) throw std::runtime_error("Cannot create tone mapping pipeline.");
    mSettings = MakeBuffer(gpu.Device(), WGPUBufferUsage_Uniform, sizeof(float) * 4, "Tone mapping settings");
}

WGPUTextureView ToneMapping::SceneTarget(PixelSize size) {
    if (mBinding && mSize.width == size.width && mSize.height == size.height) return mSceneView.get();

    WGPUTextureDescriptor texture = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texture.label = {"Linear HDR scene color", WGPU_STRLEN};
    texture.size = {size.width, size.height, 1};
    texture.format = kSceneFormat;
    texture.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
    TextureHandle sceneTexture(wgpuDeviceCreateTexture(mGpu.Device(), &texture));
    if (!sceneTexture) throw std::runtime_error("Cannot create HDR scene texture.");
    TextureViewHandle sceneView(wgpuTextureCreateView(sceneTexture.get(), nullptr));
    if (!sceneView) throw std::runtime_error("Cannot create HDR scene view.");

    BindGroupLayoutHandle layout(wgpuRenderPipelineGetBindGroupLayout(mPipeline.get(), 0));
    std::array<WGPUBindGroupEntry, 2> entries{{WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT}};
    entries[0].textureView = sceneView.get();
    entries[1].binding = 1;
    entries[1].buffer = mSettings.get();
    entries[1].size = sizeof(float) * 4;
    WGPUBindGroupDescriptor descriptor = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    descriptor.layout = layout.get();
    descriptor.entryCount = entries.size();
    descriptor.entries = entries.data();
    wgpuDevicePushErrorScope(mGpu.Device(), WGPUErrorFilter_Validation);
    BindGroupHandle binding(wgpuDeviceCreateBindGroup(mGpu.Device(), &descriptor));
    mGpu.CheckValidation("HDR scene binding");
    if (!binding) throw std::runtime_error("Cannot bind HDR scene texture.");

    mBinding = std::move(binding);
    mSceneView = std::move(sceneView);
    mSceneTexture = std::move(sceneTexture);
    mSize = size;
    return mSceneView.get();
}

void ToneMapping::Encode(WGPUCommandEncoder encoder, WGPUTextureView display, bool enabled, float exposureEv) {
    const std::array settings{std::exp2(exposureEv), enabled ? 1.0f : 0.0f, 0.0f, 0.0f};
    wgpuQueueWriteBuffer(mGpu.Queue(), mSettings.get(), 0, settings.data(), sizeof(settings));
    auto pass = BeginColorPass(encoder, display, {}, "Tone mapping and display output");
    wgpuRenderPassEncoderSetPipeline(pass.get(), mPipeline.get());
    wgpuRenderPassEncoderSetBindGroup(pass.get(), 0, mBinding.get(), 0, nullptr);
    wgpuRenderPassEncoderDraw(pass.get(), 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass.get());
}

}  // namespace tinyworld
