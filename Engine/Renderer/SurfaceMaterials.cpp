#include "Engine/Renderer/SurfaceMaterials.h"

#include "Engine/Renderer/Color.h"
#include "Engine/Renderer/WebGpuContext.h"

#include <stb_image.h>

#include <glm/gtc/constants.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>

namespace tinyworld {
namespace {

constexpr std::uint32_t kTileSize = 256;
constexpr std::uint32_t kMipCount = 9;
constexpr std::array<const char*, 4> kMaterials{"grass", "gravel", "rock", "sand"};
using Pixel = std::array<float, 4>;

//————————————————————————————————————————————————————————————————————————————————
// 创建包含全部材料与 mip 层级的二维数组。
//————————————————————————————————————————————————————————————————————————————————
TextureHandle MakeArray(WGPUDevice device, WGPUTextureFormat format) {
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.size = {kTileSize, kTileSize, static_cast<std::uint32_t>(kMaterials.size())};
    descriptor.format = format;
    constexpr auto displayFormat = WGPUTextureFormat_RGBA8Unorm;
    if (format == WGPUTextureFormat_RGBA8UnormSrgb) {
        // ImGui 纹理输入使用显示空间的底色值。
        descriptor.viewFormatCount = 1;
        descriptor.viewFormats = &displayFormat;
    }
    descriptor.mipLevelCount = kMipCount;
    descriptor.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    TextureHandle texture(wgpuDeviceCreateTexture(device, &descriptor));
    if (!texture) {
        throw std::runtime_error("Cannot create terrain texture array.");
    }
    return texture;
}

//————————————————————————————————————————————————————————————————————————————————
// 将线性颜色编码为 8 位 sRGB，数据贴图可直接量化。
//————————————————————————————————————————————————————————————————————————————————
std::uint8_t ToByte(float linear, bool srgb) {
    const float value = srgb
        ? (linear <= 0.0031308f ? 12.92f * linear : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f)
        : linear;
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

//————————————————————————————————————————————————————————————————————————————————
// 将一层图片转成线性数值，在线性空间下采样，再按格式写回 mip。
//————————————————————————————————————————————————————————————————————————————————
void UploadLayer(WGPUQueue queue, WGPUTexture texture, const std::filesystem::path& path,
                 std::uint32_t layer, bool srgb) {
    int width{}, height{}, channels{};
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Cannot open terrain material: " + path.filename().string());
    }
    const std::vector<stbi_uc> file(std::istreambuf_iterator<char>(input), {});
    const std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> image(
        stbi_load_from_memory(file.data(), static_cast<int>(file.size()), &width, &height, &channels, 4),
        stbi_image_free);
    if (!image || width != static_cast<int>(kTileSize) || height != static_cast<int>(kTileSize)) {
        throw std::runtime_error("Cannot read 256 x 256 terrain material: " + path.filename().string());
    }
    std::vector<Pixel> linear(kTileSize * kTileSize);
    for (std::size_t i = 0; i < linear.size(); ++i) {
        for (std::size_t c = 0; c < 4; ++c) {
            const float value = image.get()[i * 4 + c] / 255.0f;
            linear[i][c] = srgb && c < 3 ? static_cast<float>(DisplayToLinear(value)) : value;
        }
    }
    for (std::uint32_t mip = 0, side = kTileSize; mip < kMipCount; ++mip, side /= 2) {
        std::vector<std::uint8_t> bytes(linear.size() * 4);
        for (std::size_t i = 0; i < linear.size(); ++i) {
            for (std::size_t c = 0; c < 4; ++c) {
                bytes[i * 4 + c] = ToByte(linear[i][c], srgb && c < 3);
            }
        }
        WGPUTexelCopyTextureInfo target = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        target.texture = texture;
        target.mipLevel = mip;
        target.origin.z = layer;
        WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
        layout.bytesPerRow = side * 4;
        layout.rowsPerImage = side;
        const WGPUExtent3D size{side, side, 1};
        wgpuQueueWriteTexture(queue, &target, bytes.data(), bytes.size(), &layout, &size);

        const auto half = side / 2;
        if (half == 0) {
            break;
        }
        std::vector<Pixel> smaller(half * half);
        for (std::uint32_t y = 0; y < half; ++y) {
            for (std::uint32_t x = 0; x < half; ++x) {
                const auto i = y * 2 * side + x * 2;
                for (std::size_t c = 0; c < 4; ++c) {
                    smaller[y * half + x][c] = 0.25f * (linear[i][c] + linear[i + 1][c] +
                                                         linear[i + side][c] + linear[i + side + 1][c]);
                }
            }
        }
        linear.swap(smaller);
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 建立覆盖三层纹理及全部 mip 的二维数组视图。
//————————————————————————————————————————————————————————————————————————————————
TextureViewHandle ArrayView(WGPUTexture texture) {
    WGPUTextureViewDescriptor view = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    view.dimension = WGPUTextureViewDimension_2DArray;
    view.arrayLayerCount = static_cast<std::uint32_t>(kMaterials.size());
    view.mipLevelCount = kMipCount;
    return TextureViewHandle(wgpuTextureCreateView(texture, &view));
}

}  // namespace

//————————————————————————————————————————————————————————————————————————————————
// 底色和粗糙度的层序相同，着色器用同一组权重与坐标读取两者。
//————————————————————————————————————————————————————————————————————————————————
SurfaceMaterials::SurfaceMaterials(WebGpuContext& gpu, const std::filesystem::path& assetRoot) {
    wgpuDevicePushErrorScope(gpu.Device(), WGPUErrorFilter_Validation);
    mBaseColor = MakeArray(gpu.Device(), WGPUTextureFormat_RGBA8UnormSrgb);
    mRoughness = MakeArray(gpu.Device(), WGPUTextureFormat_RGBA8Unorm);
    const auto folder = assetRoot / "materials" / "ch08";
    for (std::uint32_t layer = 0; layer < kMaterials.size(); ++layer) {
        const std::string name = kMaterials[layer];
        UploadLayer(gpu.Queue(), mBaseColor.get(), folder / (name + "-color.png"), layer, true);
        UploadLayer(gpu.Queue(), mRoughness.get(), folder / (name + "-rough.png"), layer, false);
    }
    mColorView = ArrayView(mBaseColor.get());
    mRoughView = ArrayView(mRoughness.get());
    for (std::uint32_t layer = 0; layer < mPreviews.size(); ++layer) {
        WGPUTextureViewDescriptor view = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        view.dimension = WGPUTextureViewDimension_2D;
        view.format = WGPUTextureFormat_RGBA8Unorm;
        view.baseArrayLayer = layer;
        view.arrayLayerCount = 1;
        view.mipLevelCount = kMipCount;
        mPreviews[layer].reset(wgpuTextureCreateView(mBaseColor.get(), &view));
        if (!mPreviews[layer]) throw std::runtime_error("Cannot create terrain texture preview.");
    }
    WGPUTextureDescriptor weights = WGPU_TEXTURE_DESCRIPTOR_INIT;
    weights.size = {MaterialSplat::kSide, MaterialSplat::kSide, 1};
    weights.format = WGPUTextureFormat_RGBA8Unorm;
    weights.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    mWeightTexture.reset(wgpuDeviceCreateTexture(gpu.Device(), &weights));
    mWeightView.reset(wgpuTextureCreateView(mWeightTexture.get(), nullptr));
    WGPUSamplerDescriptor sampler = WGPU_SAMPLER_DESCRIPTOR_INIT;
    sampler.addressModeU = WGPUAddressMode_Repeat;
    sampler.addressModeV = WGPUAddressMode_Repeat;
    sampler.magFilter = WGPUFilterMode_Linear;
    sampler.minFilter = WGPUFilterMode_Linear;
    sampler.mipmapFilter = WGPUMipmapFilterMode_Linear;
    mSampler.reset(wgpuDeviceCreateSampler(gpu.Device(), &sampler));

    std::array<WGPUBindGroupLayoutEntry, 4> layout{};
    for (std::uint32_t i = 0; i < layout.size(); ++i) {
        layout[i] = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        layout[i].binding = i;
        layout[i].visibility = WGPUShaderStage_Fragment;
        if (i != 2) {
            layout[i].texture.sampleType = WGPUTextureSampleType_Float;
            layout[i].texture.viewDimension = i == 3 ? WGPUTextureViewDimension_2D : WGPUTextureViewDimension_2DArray;
        } else {
            layout[i].sampler.type = WGPUSamplerBindingType_Filtering;
        }
    }
    WGPUBindGroupLayoutDescriptor description = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    description.entries = layout.data();
    description.entryCount = layout.size();
    mLayout.reset(wgpuDeviceCreateBindGroupLayout(gpu.Device(), &description));
    if (!mLayout || !mSampler || !mColorView || !mRoughView || !mWeightView) {
        throw std::runtime_error("Cannot initialize terrain material resources.");
    }
    std::array<WGPUBindGroupEntry, 4> bindings{};
    for (std::uint32_t i = 0; i < bindings.size(); ++i) {
        bindings[i] = WGPU_BIND_GROUP_ENTRY_INIT;
        bindings[i].binding = i;
    }
    bindings[0].textureView = mColorView.get();
    bindings[1].textureView = mRoughView.get();
    bindings[2].sampler = mSampler.get();
    bindings[3].textureView = mWeightView.get();
    WGPUBindGroupDescriptor binding = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    binding.layout = mLayout.get();
    binding.entryCount = bindings.size();
    binding.entries = bindings.data();
    mBinding.reset(wgpuDeviceCreateBindGroup(gpu.Device(), &binding));
    gpu.CheckValidation("surface material textures");
    if (!mBinding) {
        throw std::runtime_error("Cannot bind surface materials.");
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 借用同一底色数组的四个二维切片作为编辑器预览。
//————————————————————————————————————————————————————————————————————————————————
MaterialPreviews SurfaceMaterials::PreviewViews() const {
    MaterialPreviews views{};
    for (std::size_t i = 0; i < views.size(); ++i) {
        views[i] = mPreviews[i].get();
    }
    return views;
}

//————————————————————————————————————————————————————————————————————————————————
// 权重变化时一次上传全部线性通道。
//————————————————————————————————————————————————————————————————————————————————
void SurfaceMaterials::SyncPaint(WGPUQueue queue, const MaterialSplat& paint) {
    if (mPaintRevision == paint.Revision()) {
        return;
    }
    WGPUTexelCopyTextureInfo target = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    target.texture = mWeightTexture.get();
    WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
    layout.bytesPerRow = MaterialSplat::kSide * 4;
    layout.rowsPerImage = MaterialSplat::kSide;
    const WGPUExtent3D size{MaterialSplat::kSide, MaterialSplat::kSide, 1};
    const auto& pixels = paint.Values();
    wgpuQueueWriteTexture(queue, &target, pixels.data(), pixels.size() * sizeof(MaterialSplat::Pixel), &layout, &size);
    mPaintRevision = paint.Revision();
}
//————————————————————————————————————————————————————————————————————————————————
// 沿经纬线生成球，避免在极点提交面积为零的三角形。
//————————————————————————————————————————————————————————————————————————————————
std::vector<SceneVertex> MakeMaterialSphere() {
    constexpr int rings = 32;
    constexpr int slices = 64;
    const auto vertex = [](int ring, int slice) {
        const float theta = glm::pi<float>() * ring / rings;
        const float phi = glm::two_pi<float>() * slice / slices;
        const std::array point{std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi)};
        return SceneVertex{point, point, {0.56f, 0.23f, 0.095f}};
    };
    std::vector<SceneVertex> mesh;
    for (int y = 0; y < rings; ++y) {
        for (int x = 0; x < slices; ++x) {
            const auto a = vertex(y, x);
            const auto b = vertex(y + 1, x);
            const auto c = vertex(y, x + 1);
            const auto d = vertex(y + 1, x + 1);
            if (y != 0) {
                mesh.insert(mesh.end(), {a, c, b});
            }
            if (y != rings - 1) {
                mesh.insert(mesh.end(), {c, d, b});
            }
        }
    }
    return mesh;
}

}  // namespace tinyworld
