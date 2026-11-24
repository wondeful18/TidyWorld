#include "Engine/Renderer/GltfMaterials.h"

#include "Engine/Renderer/WebGpuContext.h"
#include "Engine/Scene/GltfAsset.h"

#include <array>
#include <stdexcept>

namespace tinyworld {
namespace {

//————————————————————————————————————————————————————————————————————————————————
// 上传一层 RGBA8，并由 format 区分颜色纹理和数据纹理。
//————————————————————————————————————————————————————————————————————————————————
TextureViewHandle UploadImage(WebGpuContext& gpu, const GltfImage& image, WGPUTextureFormat format) {
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.size = {image.width, image.height, 1};
    descriptor.format = format;
    descriptor.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    TextureHandle texture(wgpuDeviceCreateTexture(gpu.Device(), &descriptor));
    if (!texture) throw std::runtime_error("Cannot create glTF texture.");

    WGPUTexelCopyTextureInfo target = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    target.texture = texture.get();
    WGPUTexelCopyBufferLayout source = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
    source.bytesPerRow = image.width * 4;
    source.rowsPerImage = image.height;
    wgpuQueueWriteTexture(gpu.Queue(), &target, image.pixels.data(), image.pixels.size(), &source, &descriptor.size);
    TextureViewHandle view(wgpuTextureCreateView(texture.get(), nullptr));
    if (!view) throw std::runtime_error("Cannot create glTF texture view.");
    return view;
}

}  // namespace

//————————————————————————————————————————————————————————————————————————————————
// 三张二维纹理共用采样器。绑定号 4–8 与 world.wgsl 对应。
//————————————————————————————————————————————————————————————————————————————————
GltfMaterials::GltfMaterials(WebGpuContext& gpu) : mGpu(gpu) {
    std::array<WGPUBindGroupLayoutEntry, 5> entries;
    for (std::uint32_t i = 0; i < entries.size(); ++i) {
        entries[i] = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        entries[i].binding = i + 4;
        entries[i].visibility = WGPUShaderStage_Fragment;
    }
    for (std::size_t i = 0; i < 2; ++i) {
        entries[i].texture.sampleType = WGPUTextureSampleType_Float;
        entries[i].texture.viewDimension = WGPUTextureViewDimension_2D;
    }
    entries[4].texture = entries[0].texture;
    entries[2].sampler.type = WGPUSamplerBindingType_Filtering;
    entries[3].buffer.type = WGPUBufferBindingType_Uniform;
    entries[3].buffer.minBindingSize = 8 * sizeof(float);
    WGPUBindGroupLayoutDescriptor layout = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    layout.entries = entries.data();
    layout.entryCount = entries.size();

    WGPUSamplerDescriptor sampler = WGPU_SAMPLER_DESCRIPTOR_INIT;
    sampler.addressModeU = WGPUAddressMode_Repeat;
    sampler.addressModeV = WGPUAddressMode_Repeat;
    sampler.magFilter = WGPUFilterMode_Linear;
    sampler.minFilter = WGPUFilterMode_Linear;
    mLayout.reset(wgpuDeviceCreateBindGroupLayout(gpu.Device(), &layout));
    mSampler.reset(wgpuDeviceCreateSampler(gpu.Device(), &sampler));
    if (!mLayout || !mSampler) throw std::runtime_error("Cannot create glTF material layout.");
    mBlackLightmap = UploadLightmap({1, 1, {0, 0, 0, 0}});
}

//————————————————————————————————————————————————————————————————————————————————
// 保持图集数据的线性解释，采样后由 shader 解回辐照度。
//————————————————————————————————————————————————————————————————————————————————
TextureViewHandle GltfMaterials::UploadLightmap(const GltfImage& source) const {
    return UploadImage(mGpu, source, WGPUTextureFormat_RGBA8Unorm);
}

//————————————————————————————————————————————————————————————————————————————————
// 底色的 RGB 由 GPU 转为线性。金属粗糙度系数放在同一个 uniform。
//————————————————————————————————————————————————————————————————————————————————
BindGroupHandle GltfMaterials::Upload(const GltfMaterial& source, WGPUTextureView lightmap) const {
    const auto base = UploadImage(mGpu, source.baseColor, WGPUTextureFormat_RGBA8UnormSrgb);
    const auto metalRough = UploadImage(mGpu, source.metalRough, WGPUTextureFormat_RGBA8Unorm);
    const std::array values{source.baseFactor.x, source.baseFactor.y, source.baseFactor.z, source.metallicFactor,
                            source.roughnessFactor, 0.0f, 0.0f, 0.0f};
    WGPUBufferDescriptor buffer = WGPU_BUFFER_DESCRIPTOR_INIT;
    buffer.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    buffer.size = sizeof(values);
    BufferHandle factors(wgpuDeviceCreateBuffer(mGpu.Device(), &buffer));
    if (!factors) throw std::runtime_error("Cannot create glTF material uniform.");
    wgpuQueueWriteBuffer(mGpu.Queue(), factors.get(), 0, values.data(), sizeof(values));

    std::array<WGPUBindGroupEntry, 5> entries;
    for (std::uint32_t i = 0; i < entries.size(); ++i) {
        entries[i] = WGPU_BIND_GROUP_ENTRY_INIT;
        entries[i].binding = i + 4;
    }
    entries[0].textureView = base.get();
    entries[1].textureView = metalRough.get();
    entries[2].sampler = mSampler.get();
    entries[3].buffer = factors.get();
    entries[3].size = sizeof(values);
    entries[4].textureView = lightmap ? lightmap : mBlackLightmap.get();
    WGPUBindGroupDescriptor binding = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    binding.layout = mLayout.get();
    binding.entryCount = entries.size();
    binding.entries = entries.data();
    BindGroupHandle result(wgpuDeviceCreateBindGroup(mGpu.Device(), &binding));
    if (!result) throw std::runtime_error("Cannot bind glTF material.");
    return result;
}

}  // namespace tinyworld
