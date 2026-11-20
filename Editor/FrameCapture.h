#pragma once

#include "Engine/Platform/Window.h"
#include "Engine/Renderer/WgpuHandle.h"

#include <atomic>
#include <filesystem>

namespace tinyworld {

class WebGpuContext;

//————————————————————————————————————————————————————————————————————————————————
// 在无需可见窗口的情况下回读真实 GPU 绘制结果。
// 使用与 Surface 相同的颜色格式，把最终帧写成无损的 PPM。
//————————————————————————————————————————————————————————————————————————————————
class FrameCapture {
public:
    //————————————————————————————————————————————————————————————————————————————————
    // 创建与窗口像素尺寸相同的可回读颜色目标及暂存缓冲。
    // WebGPU 纹理复制的行距需按 256 字节对齐。
    //————————————————————————————————————————————————————————————————————————————————
    FrameCapture(WebGpuContext& gpu, PixelSize size);

    //————————————————————————————————————————————————————————————————————————————————
    // 借用捕获目标的视图，供正常帧编码函数使用。
    //————————————————————————————————————————————————————————————————————————————————
    [[nodiscard]] WGPUTextureView View() const { return mView.get(); }

    //————————————————————————————————————————————————————————————————————————————————
    // 在同一 encoder 中追加从颜色纹理到缓冲区的复制。
    // 调用前须结束对该纹理的颜色通道。
    //————————————————————————————————————————————————————————————————————————————————
    void EncodeCopy(WGPUCommandEncoder encoder);

    //————————————————————————————————————————————————————————————————————————————————
    // 等待映射，在 CPU 上重排每行，并写入 PPM 文件。
    // 调用前须已向 Queue 提交本次捕获的复制命令。
    //————————————————————————————————————————————————————————————————————————————————
    void Write(WebGpuContext& gpu, const std::filesystem::path& path);

private:
    PixelSize mSize;                                 // 回读图像的像素宽高
    std::uint32_t mRowBytes;                         // GPU 每行字节数，含对齐填充
    WGPUTextureFormat mFormat;                      // 用于判断 RGBA 或 BGRA 的字节顺序
    std::atomic<WGPUMapAsyncStatus> mMapStatus{};   // 映射回调状态，零表示尚未完成
    TextureHandle mTexture;                         // 持有可复制的最终颜色纹理
    TextureViewHandle mView;                        // 供正常绘制通道借用的颜色视图
    BufferHandle mReadback;                         // 存放 GPU 复制结果的 CPU 可读缓冲
};

}  // namespace tinyworld
