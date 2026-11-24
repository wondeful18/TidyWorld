#include "Engine/Renderer/EnvironmentMap.h"

#include "Engine/Renderer/Color.h"
#include "Engine/Renderer/WebGpuContext.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 4096
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace tinyworld {
namespace {

constexpr std::uint32_t kFaceSize = 768;
constexpr auto kColorFormat = WGPUTextureFormat_RGBA8UnormSrgb;
using Pixel = std::array<std::uint8_t, 4>;
using DecodedPixels = std::unique_ptr<stbi_uc, decltype(&stbi_image_free)>;

//————————————————————————————————————————————————————————————————————————————————
// 已解码的 sRGB 全景照片。每个像素按 RGBA 顺序排列。
//————————————————————————————————————————————————————————————————————————————————
struct PanoramaImage {
    int width;             // 图像宽度，单位为像素
    int height;            // 图像高度，单位为像素
    DecodedPixels pixels; // 由 stb_image 分配，离开作用域时释放
};

//————————————————————————————————————————————————————————————————————————————————
// 通过文件流读取 JPEG，也支持资源目录中含有非 ASCII 字符的路径。
//————————————————————————————————————————————————————————————————————————————————
PanoramaImage LoadPanorama(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Cannot open the chapter 04 panorama");
    }
    const std::vector<stbi_uc> bytes(std::istreambuf_iterator<char>{input}, {});
    if (bytes.empty() || bytes.size() > std::numeric_limits<int>::max()) {
        throw std::runtime_error("Invalid chapter 04 panorama file size");
    }
    int width = 0;
    int height = 0;
    DecodedPixels pixels(stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                                               &width, &height, nullptr, STBI_rgb_alpha), stbi_image_free);
    if (!pixels || height <= 0 || width != 2 * height) {
        throw std::runtime_error("Chapter 04 panorama must be a valid 2:1 JPEG image");
    }
    return {width, height, std::move(pixels)};
}

//————————————————————————————————————————————————————————————————————————————————
// 将归一化面坐标换成世界方向，v 从图像顶端向下增加。
//————————————————————————————————————————————————————————————————————————————————
glm::vec3 CubeDirection(std::uint32_t face, float u, float v) {
    const std::array<glm::vec3, 6> directions{{
        {1.0f, -v, -u}, {-1.0f, -v, u},
        {u, 1.0f, v}, {u, -1.0f, -v},
        {u, -v, 1.0f}, {-u, -v, -1.0f},
    }};
    return glm::normalize(directions[face]);
}

//————————————————————————————————————————————————————————————————————————————————
// 将世界方向换成等距柱状 UV，与天空球着色器的中心经线和极点一致。
//————————————————————————————————————————————————————————————————————————————————
glm::vec2 PanoramaUv(glm::vec3 direction) {
    const float longitude = std::atan2(direction.x, direction.z);
    const float latitude = std::asin(glm::clamp(direction.y, -1.0f, 1.0f));
    return {0.5f + longitude / glm::two_pi<float>(), 0.5f - latitude / glm::pi<float>()};
}

//————————————————————————————————————————————————————————————————————————————————
// 将线性光值编码回 sRGB 字节，供 Cubemap 纹理存储。
//————————————————————————————————————————————————————————————————————————————————
std::uint8_t EncodeSrgb(float value) {
    const float display = value <= 0.0031308f ? 12.92f * value : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
    return static_cast<std::uint8_t>(glm::clamp(display, 0.0f, 1.0f) * 255.0f + 0.5f);
}

//————————————————————————————————————————————————————————————————————————————————
// 复用 8 位 sRGB 到线性 RGB 的转换表。
//————————————————————————————————————————————————————————————————————————————————
const std::array<float, 256>& LinearSamples() {
    static const auto values = [] {
        std::array<float, 256> linear{};
        for (std::size_t i = 0; i < linear.size(); ++i) {
            linear[i] = static_cast<float>(DisplayToLinear(i / 255.0));
        }
        return linear;
    }();
    return values;
}

//————————————————————————————————————————————————————————————————————————————————
// 在线性空间对全景图做双线性采样。水平方向连接接缝，上下钳位到极点。
//————————————————————————————————————————————————————————————————————————————————
glm::vec3 SamplePanorama(const PanoramaImage& image, glm::vec2 uv) {
    const auto& values = LinearSamples();
    const glm::vec2 pixel = uv * glm::vec2(image.width, image.height) - 0.5f;
    const int x0 = static_cast<int>(std::floor(pixel.x));
    const int y0 = static_cast<int>(std::floor(pixel.y));
    const float tx = pixel.x - x0;
    const float ty = pixel.y - y0;
    const auto read = [&](int x, int y) -> glm::vec3 {
        const int column = (x + image.width) % image.width;
        const int row = std::clamp(y, 0, image.height - 1);
        const auto offset = (static_cast<std::size_t>(row) * image.width + column) * sizeof(Pixel);
        const auto* rgba = image.pixels.get() + offset;
        return {values[rgba[0]], values[rgba[1]], values[rgba[2]]};
    };
    const auto north = glm::mix(read(x0, y0), read(x0 + 1, y0), tx);
    const auto south = glm::mix(read(x0, y0 + 1), read(x0 + 1, y0 + 1), tx);
    return glm::mix(north, south, ty);
}

//————————————————————————————————————————————————————————————————————————————————
// 创建供队列写入和着色器采样的二维纹理。Cubemap 需要六层等宽的正方形。
//————————————————————————————————————————————————————————————————————————————————
TextureHandle CreateTexture(WGPUDevice device, WGPUExtent3D size, const char* label) {
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.label = {label, WGPU_STRLEN};
    descriptor.size = size;
    descriptor.format = kColorFormat;
    descriptor.usage = WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding;
    return TextureHandle(wgpuDeviceCreateTexture(device, &descriptor));
}

//————————————————————————————————————————————————————————————————————————————————
// 将紧密排列的 RGBA 像素复制到指定纹理层。
//————————————————————————————————————————————————————————————————————————————————
void UploadLayer(WGPUQueue queue, WGPUTexture texture, WGPUExtent3D size,
                 std::uint32_t layer, const void* pixels) {
    WGPUTexelCopyTextureInfo target = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    target.texture = texture;
    target.origin.z = layer;
    WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
    layout.bytesPerRow = size.width * sizeof(Pixel);
    layout.rowsPerImage = size.height;
    const WGPUExtent3D layerSize{size.width, size.height, 1};
    wgpuQueueWriteTexture(queue, &target, pixels, static_cast<std::size_t>(layout.bytesPerRow) * size.height,
                          &layout, &layerSize);
}

//————————————————————————————————————————————————————————————————————————————————
// 用照片的线性亮度生成同方向的低照度蓝调夜景，保留山脊和云的结构。
//————————————————————————————————————————————————————————————————————————————————
glm::vec3 NightColor(glm::vec3 day) {
    const float luminance = glm::dot(day, glm::vec3{0.2126f, 0.7152f, 0.0722f});
    return glm::vec3{0.0018f, 0.0037f, 0.0090f} + luminance * glm::vec3{0.012f, 0.024f, 0.061f};
}

//————————————————————————————————————————————————————————————————————————————————
// 生成全景夜图的像素。纹理需要 sRGB 字节，混合在 shader 解码后执行。
//————————————————————————————————————————————————————————————————————————————————
std::vector<Pixel> MakeNightPanorama(const PanoramaImage& day) {
    const auto& linear = LinearSamples();
    std::vector<Pixel> night(static_cast<std::size_t>(day.width) * day.height);
    for (std::size_t i = 0; i < night.size(); ++i) {
        const auto* rgba = day.pixels.get() + 4 * i;
        const auto c = NightColor({linear[rgba[0]], linear[rgba[1]], linear[rgba[2]]});
        night[i] = {EncodeSrgb(c.r), EncodeSrgb(c.g), EncodeSrgb(c.b), 255};
    }
    return night;
}

//————————————————————————————————————————————————————————————————————————————————
// 将同一张全景照片按方向重投影到 Cubemap 的六个面。
//————————————————————————————————————————————————————————————————————————————————
void UploadCube(WGPUQueue queue, WGPUTexture texture, WGPUExtent3D size, const PanoramaImage& panorama, bool withNight, WGPUTexture nightCube) {
    std::vector<Pixel> pixels(static_cast<std::size_t>(size.width) * size.height);
    std::vector<Pixel> night(withNight ? pixels.size() : 0);
    for (std::uint32_t face = 0; face < size.depthOrArrayLayers; ++face) {
        for (std::uint32_t y = 0; y < size.height; ++y) {
            for (std::uint32_t x = 0; x < size.width; ++x) {
                const float u = 2.0f * (x + 0.5f) / size.width - 1.0f;
                const float v = 2.0f * (y + 0.5f) / size.height - 1.0f;
                const auto direction = CubeDirection(face, u, v);
                const auto rgb = SamplePanorama(panorama, PanoramaUv(direction));
                const auto index = static_cast<std::size_t>(y) * size.width + x;
                pixels[index] = {EncodeSrgb(rgb.r), EncodeSrgb(rgb.g), EncodeSrgb(rgb.b), 255};
                if (withNight) {
                    const auto c = NightColor(rgb);
                    night[index] = {EncodeSrgb(c.r), EncodeSrgb(c.g), EncodeSrgb(c.b), 255};
                }
            }
        }
        UploadLayer(queue, texture, size, face, pixels.data());
        if (withNight) UploadLayer(queue, nightCube, size, face, night.data());
    }
}

}  // namespace

//————————————————————————————————————————————————————————————————————————————————
// 上传环境数据，并为背景与车身提供相同的 Cubemap 视图和采样器。
//————————————————————————————————————————————————————————————————————————————————
EnvironmentMap::EnvironmentMap(WebGpuContext& gpu, const std::filesystem::path& panoramaPath, bool withNight) {
    const auto panorama = LoadPanorama(panoramaPath);
    for (std::size_t i = 0; i < mHorizon.size(); ++i) {
        const float u = static_cast<float>(i) / mHorizon.size();
        // 取地平线上方一条窄带，减少照片里地面颜色混进远海。
        const auto average = (SamplePanorama(panorama, {u, 0.35f}) +
                              SamplePanorama(panorama, {u, 0.38f}) +
                              SamplePanorama(panorama, {u, 0.41f})) / 3.0f;
        mHorizon[i] = glm::mix(glm::vec3{0.44f, 0.57f, 0.64f}, average, 0.55f);
    }
    constexpr WGPUExtent3D cubeSize{kFaceSize, kFaceSize, 6};
    const WGPUExtent3D panoSize{static_cast<std::uint32_t>(panorama.width),
                               static_cast<std::uint32_t>(panorama.height), 1};
    wgpuDevicePushErrorScope(gpu.Device(), WGPUErrorFilter_Validation);
    mCube = CreateTexture(gpu.Device(), cubeSize, "Chapter 04 environment cube");
    mPanorama = CreateTexture(gpu.Device(), panoSize, "Chapter 04 equirectangular panorama");
    gpu.CheckValidation("chapter 04 environment textures");
    if (withNight) {
        mNightCube = CreateTexture(gpu.Device(), cubeSize, "Chapter 18 night cube");
        mNightPanorama = CreateTexture(gpu.Device(), panoSize, "Chapter 18 night panorama");
    }
    if (!mCube || !mPanorama || (withNight && (!mNightCube || !mNightPanorama))) {
        throw std::runtime_error("Cannot create environment textures");
    }
    wgpuDevicePushErrorScope(gpu.Device(), WGPUErrorFilter_Validation);
    UploadCube(gpu.Queue(), mCube.get(), cubeSize, panorama, withNight, mNightCube.get());
    UploadLayer(gpu.Queue(), mPanorama.get(), panoSize, 0, panorama.pixels.get());
    WGPUTextureViewDescriptor cubeView = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    cubeView.dimension = WGPUTextureViewDimension_Cube;
    cubeView.arrayLayerCount = 6;
    mCubeView.reset(wgpuTextureCreateView(mCube.get(), &cubeView));
    mPanoView.reset(wgpuTextureCreateView(mPanorama.get(), nullptr));
    if (withNight) {
        const auto pixels = MakeNightPanorama(panorama);
        UploadLayer(gpu.Queue(), mNightPanorama.get(), panoSize, 0, pixels.data());
        mNightCubeView.reset(wgpuTextureCreateView(mNightCube.get(), &cubeView));
        mNightPanoView.reset(wgpuTextureCreateView(mNightPanorama.get(), nullptr));
    }

    WGPUSamplerDescriptor filtering = WGPU_SAMPLER_DESCRIPTOR_INIT;
    filtering.minFilter = WGPUFilterMode_Linear;
    filtering.magFilter = WGPUFilterMode_Linear;
    filtering.addressModeU = WGPUAddressMode_ClampToEdge;
    filtering.addressModeV = WGPUAddressMode_ClampToEdge;
    mCubeSampler.reset(wgpuDeviceCreateSampler(gpu.Device(), &filtering));
    filtering.addressModeU = WGPUAddressMode_Repeat;
    mPanoSampler.reset(wgpuDeviceCreateSampler(gpu.Device(), &filtering));

    std::array<WGPUBindGroupLayoutEntry, 6> layouts;
    for (std::uint32_t index = 0; index < layouts.size(); ++index) {
        layouts[index] = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        layouts[index].binding = index;
        layouts[index].visibility = WGPUShaderStage_Fragment;
    }
    layouts[0].texture.sampleType = WGPUTextureSampleType_Float;
    layouts[0].texture.viewDimension = WGPUTextureViewDimension_Cube;
    layouts[1].sampler.type = WGPUSamplerBindingType_Filtering;
    layouts[2].texture.sampleType = WGPUTextureSampleType_Float;
    layouts[2].texture.viewDimension = WGPUTextureViewDimension_2D;
    layouts[3].sampler.type = WGPUSamplerBindingType_Filtering;
    layouts[4].texture = layouts[0].texture;
    layouts[5].texture = layouts[2].texture;
    WGPUBindGroupLayoutDescriptor layout = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    layout.entryCount = layouts.size();
    layout.entries = layouts.data();
    mLayout.reset(wgpuDeviceCreateBindGroupLayout(gpu.Device(), &layout));
    gpu.CheckValidation("chapter 04 environment upload and views");
    if (!mCubeView || !mPanoView || !mCubeSampler || !mPanoSampler || !mLayout ||
        (withNight && (!mNightCubeView || !mNightPanoView))) {
        throw std::runtime_error("Cannot create environment views or sampling layout");
    }

    std::array<WGPUBindGroupEntry, 6> entries;
    for (std::uint32_t index = 0; index < entries.size(); ++index) {
        entries[index] = WGPU_BIND_GROUP_ENTRY_INIT;
        entries[index].binding = index;
    }
    entries[0].textureView = mCubeView.get();
    entries[1].sampler = mCubeSampler.get();
    entries[2].textureView = mPanoView.get();
    entries[3].sampler = mPanoSampler.get();
    entries[4].textureView = withNight ? mNightCubeView.get() : mCubeView.get();
    entries[5].textureView = withNight ? mNightPanoView.get() : mPanoView.get();
    WGPUBindGroupDescriptor binding = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    binding.layout = mLayout.get();
    binding.entryCount = entries.size();
    binding.entries = entries.data();
    wgpuDevicePushErrorScope(gpu.Device(), WGPUErrorFilter_Validation);
    mBinding.reset(wgpuDeviceCreateBindGroup(gpu.Device(), &binding));
    gpu.CheckValidation("chapter 04 environment binding");
    if (!mBinding) {
        throw std::runtime_error("Cannot bind environment textures");
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 查观察方向对应的经度，并在周期表中平滑过渡，避免环绕接缝时跳色。
//————————————————————————————————————————————————————————————————————————————————
glm::vec3 EnvironmentMap::HorizonColor(glm::vec3 direction, float nightMix) const {
    const float u = glm::fract(PanoramaUv(direction).x);
    const float sample = u * mHorizon.size();
    const auto index = static_cast<std::size_t>(sample) % mHorizon.size();
    const auto day = glm::mix(mHorizon[index], mHorizon[(index + 1) % mHorizon.size()], glm::fract(sample));
    return glm::mix(day, NightColor(day), nightMix);
}

}  // namespace tinyworld
