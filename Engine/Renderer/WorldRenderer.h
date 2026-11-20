#pragma once

#include "Engine/Platform/Window.h"
#include "Engine/Renderer/WgpuHandle.h"
#include "Engine/Scene/WorldScene.h"

#include <array>
#include <filesystem>

namespace tinyworld {

class WebGpuContext;

//————————————————————————————————————————————————————————————————————————————————
// 持有第二章的顶点缓冲、光照管线和深度目标。
// 按场景数据更新 uniform，并使用同一个颜色通道绘制三个物体。
//————————————————————————————————————————————————————————————————————————————————
class WorldRenderer {
public:
    //————————————————————————————————————————————————————————————————————————————————
    // 从可执行文件邻近的资源目录载入网格和 WGSL，建立 GPU 状态。
    //————————————————————————————————————————————————————————————————————————————————
    WorldRenderer(WebGpuContext& gpu, WGPUTextureFormat colorFormat,
                  const WorldScene& scene, const std::filesystem::path& assetRoot);

    //————————————————————————————————————————————————————————————————————————————————
    // 按目标像素尺寸准备深度缓冲，并编码第二章场景。
    //————————————————————————————————————————————————————————————————————————————————
    void Encode(WGPUCommandEncoder encoder, WGPUTextureView target, PixelSize size,
                const WorldScene& scene);

private:
    //————————————————————————————————————————————————————————————————————————————————
    // 一个物体对应常驻的几何和独立的 uniform。
    //————————————————————————————————————————————————————————————————————————————————
    struct Drawable {
        BufferHandle vertices;    // GPU 顶点缓冲，启动时上传一次
        BufferHandle uniform;     // 该物体单独使用的每帧常量缓冲
        BindGroupHandle binding;  // 将 uniform 连到着色器绑定槽
        std::uint32_t count{};    // 三角形列表的顶点数
    };

    //————————————————————————————————————————————————————————————————————————————————
    // 仅在颜色目标的像素尺寸变化时替换深度纹理与视图。
    //————————————————————————————————————————————————————————————————————————————————
    void ResizeDepth(PixelSize size);

    WebGpuContext& mGpu;                                   // 借用 GPU 上下文，覆盖所有资源的生命期
    PipelineHandle mPipeline;                             // 方向光与深度测试管线
    std::array<Drawable, WorldScene::kObjectCount> mDraws;   // 与场景物体同序的装备
    TextureHandle mDepthTexture;                          // 深度纹理的拥有引用
    TextureViewHandle mDepthView;                         // 当前深度附件的视图
    PixelSize mDepthSize{};                                // 深度目标的像素宽高
    WGPUColor mClearColor{};                               // 与颜色目标编码匹配的背景色
};

}  // namespace tinyworld
