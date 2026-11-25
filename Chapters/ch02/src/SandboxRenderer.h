#pragma once

#include "Engine/Renderer/SkyRenderer.h"
#include "Engine/Renderer/SunShadowMap.h"
#include "Engine/Renderer/WorldDraw.h"
#include "Engine/Scene/WorldScene.h"

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 第 02–05 章的平地、小屋、天空与角色共用这个小型渲染入口。
//————————————————————————————————————————————————————————————————————————————————

class SandboxRenderer {
public:
    SandboxRenderer(WebGpuContext& gpu, WGPUTextureFormat format, const WorldScene& scene,
                    const std::filesystem::path& root);
    void Encode(WGPUCommandEncoder encoder, WGPUTextureView target, PixelSize size, const WorldScene& scene);

private:
    WebGpuContext& mGpu;  // GPU 生命周期覆盖演示
    BindGroupLayoutHandle mLayout;  // 每物体的变换与光照
    std::array<Drawable, WorldScene::kObjectCount> mObjects; // 草台、树、屋与车
    std::optional<Drawable> mWalker;   // 第五章的步行角色
    std::optional<SunShadowMap> mShadowMap; // 第三章引入的单张太阳深度图
    std::optional<SkyRenderer> mSky;  // 第四章引入的天空与共享环境
    PipelineHandle mPipeline;  // 所选章节的颜色管线
    WGPUColor mClear{};        // 与颜色目标编码匹配的背景
    DepthTarget mDepth;       // 与视口同大小的深度
};

}  // namespace tinyworld
