#include "Editor/FrameCapture.h"

#include "Engine/Renderer/WebGpuContext.h"

#include <chrono>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 为诊断绘制准备同格式的颜色纹理与对齐后的回读缓冲。
// 每个像素在本章支持的四种窗口颜色格式中均占四字节。
//————————————————————————————————————————————————————————————————————————————————
FrameCapture::FrameCapture(WebGpuContext& gpu, PixelSize size)
    : mSize(size), mRowBytes((size.width * 4 + 255) & ~255u), mFormat(gpu.SurfaceFormat()) {
    WGPUTextureDescriptor texture = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texture.label = {"Chapter 01 GPU capture", WGPU_STRLEN};
    texture.size = {mSize.width, mSize.height, 1};
    texture.format = mFormat;
    texture.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    mTexture.reset(wgpuDeviceCreateTexture(gpu.Device(), &texture));
    if (!mTexture) {
        throw std::runtime_error("Cannot create the capture texture");
    }
    mView.reset(wgpuTextureCreateView(mTexture.get(), nullptr));

    WGPUBufferDescriptor buffer = WGPU_BUFFER_DESCRIPTOR_INIT;
    buffer.label = {"Chapter 01 readback", WGPU_STRLEN};
    buffer.size = static_cast<std::uint64_t>(mRowBytes) * mSize.height;
    buffer.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    mReadback.reset(wgpuDeviceCreateBuffer(gpu.Device(), &buffer));
    if (!mView || !mReadback) {
        throw std::runtime_error("Cannot allocate capture view or readback buffer");
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 将绘制好的颜色纹理复制到 CPU 可映射缓冲区。
// 拷贝命令在所有绘制通道之后执行。
//————————————————————————————————————————————————————————————————————————————————
void FrameCapture::EncodeCopy(WGPUCommandEncoder encoder) {
    WGPUTexelCopyTextureInfo source = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    source.texture = mTexture.get();
    WGPUTexelCopyBufferInfo destination = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    destination.buffer = mReadback.get();
    destination.layout.bytesPerRow = mRowBytes;
    destination.layout.rowsPerImage = mSize.height;
    const WGPUExtent3D extent{mSize.width, mSize.height, 1};
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &extent);
}

//————————————————————————————————————————————————————————————————————————————————
// 从映射缓冲中去掉 Alpha 与行尾填充，再写入二进制 PPM。
// 回调只保存状态，文件写入仍在主线程完成。
//————————————————————————————————————————————————————————————————————————————————
void FrameCapture::Write(WebGpuContext& gpu, const std::filesystem::path& path) {
    using namespace std::chrono_literals;
    const auto size = static_cast<std::size_t>(mRowBytes) * mSize.height;
    WGPUBufferMapCallbackInfo callback = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_AllowProcessEvents;
    callback.userdata1 = &mMapStatus;
    callback.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* user, void*) {
        static_cast<std::atomic<WGPUMapAsyncStatus>*>(user)->store(status);
    };
    wgpuBufferMapAsync(mReadback.get(), WGPUMapMode_Read, 0, size, callback);
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (mMapStatus.load() == 0 && std::chrono::steady_clock::now() < deadline) {
        gpu.ProcessEvents();
        if (mMapStatus.load() == 0) {
            std::this_thread::sleep_for(1ms);
        }
    }
    const auto status = mMapStatus.load();
    if (status != WGPUMapAsyncStatus_Success) {
        throw std::runtime_error("Cannot map the GPU capture buffer, status " + std::to_string(status));
    }
    const auto* bytes = static_cast<const char*>(wgpuBufferGetConstMappedRange(mReadback.get(), 0, size));
    if (!bytes) {
        throw std::runtime_error("Cannot read mapped GPU capture bytes");
    }

    std::ofstream output(path, std::ios::binary);
    output << "P6\n" << mSize.width << ' ' << mSize.height << "\n255\n";
    const bool bgra = mFormat == WGPUTextureFormat_BGRA8Unorm || mFormat == WGPUTextureFormat_BGRA8UnormSrgb;
    std::vector<char> row(static_cast<std::size_t>(mSize.width) * 3);
    for (std::uint32_t y = 0; y < mSize.height; ++y) {
        const auto* source = bytes + static_cast<std::size_t>(y) * mRowBytes;
        for (std::uint32_t x = 0; x < mSize.width; ++x) {
            row[x * 3] = source[x * 4 + (bgra ? 2 : 0)];
            row[x * 3 + 1] = source[x * 4 + 1];
            row[x * 3 + 2] = source[x * 4 + (bgra ? 0 : 2)];
        }
        output.write(row.data(), static_cast<std::streamsize>(row.size()));
    }
    output.close();
    wgpuBufferUnmap(mReadback.get());
    if (!output) {
        throw std::runtime_error("Cannot write capture to " + path.string());
    }
}

}  // namespace tinyworld
