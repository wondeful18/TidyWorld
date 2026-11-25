#pragma once

#include "Engine/Platform/Window.h"
#include "Engine/Renderer/WgpuHandle.h"

#include <filesystem>

namespace tinyworld {

class WebGpuContext;

class ToneMapping {
public:
    static constexpr auto kSceneFormat = WGPUTextureFormat_RGBA16Float;

    ToneMapping(WebGpuContext& gpu, WGPUTextureFormat displayFormat, const std::filesystem::path& shaders);

    // 颜色附件与采样绑定始终指向同一张同尺寸的线性纹理。
    WGPUTextureView SceneTarget(PixelSize size);
    void Encode(WGPUCommandEncoder encoder, WGPUTextureView display, bool enabled, float exposureEv);

private:
    WebGpuContext& mGpu;
    PipelineHandle mPipeline;
    BufferHandle mSettings;
    TextureHandle mSceneTexture;
    TextureViewHandle mSceneView;
    BindGroupHandle mBinding;
    PixelSize mSize{};
};

}  // namespace tinyworld
