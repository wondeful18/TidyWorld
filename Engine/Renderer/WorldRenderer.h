#pragma once

#include "Engine/Platform/Window.h"
#include "Engine/Renderer/PropRenderer.h"
#include "Engine/Renderer/SkyRenderer.h"
#include "Engine/Renderer/SunShadowMap.h"
#include "Engine/Renderer/SurfaceMaterials.h"
#include "Engine/Renderer/ToneMapping.h"
#include "Engine/Renderer/VegetationRenderer.h"
#include "Engine/Renderer/WaterRenderer.h"
#include "Engine/Renderer/WgpuHandle.h"
#include "Engine/Renderer/WorldDraw.h"
#include "Engine/Scene/RobotMesh.h"
#include "Engine/Scene/WorldScene.h"
#include "Engine/Renderer/TerrainRenderer.h"

#include <array>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace tinyworld {

class WebGpuContext;

//————————————————————————————————————————————————————————————————————————————————
// 持有小屋场景的网格、颜色管线和相机深度目标。
// 第三章在颜色通道之前绘制太阳深度图。
//————————————————————————————————————————————————————————————————————————————————
class WorldRenderer {
public:
    //————————————————————————————————————————————————————————————————————————————————
    // 从可执行文件邻近的资源目录载入网格和 WGSL，建立 GPU 状态。
    //————————————————————————————————————————————————————————————————————————————————
    WorldRenderer(WebGpuContext& gpu, WGPUTextureFormat colorFormat,
                  const WorldScene& scene, const std::filesystem::path& assetRoot);
    [[nodiscard]] MaterialPreviews PaintPreviews() const {
        return mMaterials ? mMaterials->PreviewViews() : MaterialPreviews{};
    }
    [[nodiscard]] const unsigned& SceneDraws() const { return mSceneDraws; }
    [[nodiscard]] const TerrainStreamStats& StreamStatistics() const { return mTerrain->StreamStatistics(); }
    [[nodiscard]] bool StreamingSettled() const { return mTerrain->StreamingSettled(); }
    void WaitForChunkData() { mTerrain->WaitForChunkData(); }
    [[nodiscard]] const TerrainDrawStats& TerrainStatistics() const { return mTerrain->DrawStatistics(); }
    [[nodiscard]] const VegetationDrawStats& VegetationStatistics() const { return mVegetationStats; }

    //————————————————————————————————————————————————————————————————————————————————
    // 按目标像素尺寸准备相机深度缓冲，再编码本帧需要的通道。
    //————————————————————————————————————————————————————————————————————————————————
    void Encode(WGPUCommandEncoder encoder, WGPUTextureView target, PixelSize size,
                const WorldScene& scene);

private:
    //————————————————————————————————————————————————————————————————————————————————
    // 提交地形块与已上传变换的物体，供两个绘制通道共用。
    //————————————————————————————————————————————————————————————————————————————————
    void DrawObjects(WGPURenderPassEncoder pass, bool showRobot, bool showSpheres, bool shadows, const TerrainFrustum* shadowFrustum = nullptr);

    //————————————————————————————————————————————————————————————————————————————————
    // 绑定一个网格的顶点与 uniform，提交一次非索引绘制。
    //————————————————————————————————————————————————————————————————————————————————
    void DrawMesh(WGPURenderPassEncoder pass, const Drawable& mesh, WGPUBindGroup instance = nullptr);

    WebGpuContext& mGpu;                                   // 借用 GPU 上下文，覆盖所有资源的生命期
    BindGroupLayoutHandle mObjectLayout;                    // 保留动态路面上传所需的物体布局
    std::optional<SunShadowMap> mShadowMap;                // 仅由支持阴影的章节创建
    std::optional<SkyRenderer> mSky;                       // 第四章背景与共享环境纹理
    std::optional<WaterRenderer> mWater;  // 读取当前相机深度的半透明水面
    std::optional<ToneMapping> mToneMapping; // 第十八章在独立通道映射到显示亮度
    std::optional<SurfaceMaterials> mMaterials;            // 第八章底色与粗糙度数组
    std::optional<VegetationRenderer> mVegetation;
    Drawable mVegetationUniform;
    PipelineHandle mVegetationPipeline;
    VegetationDrawStats mVegetationStats;
    PipelineHandle mPipeline;                             // 方向光与深度测试管线
    Drawable mRobot;                         // 小机器人的躯干
    std::optional<Drawable> mRobotLamp;        // 第十八章跟着右手摆动的发光灯具
    std::array<Drawable, kRobotLimbCount> mRobotLimbs; // 共享步态驱动的四肢
    std::optional<PropRenderer> mProps;       // 固定地标与摆件
    unsigned mSceneDraws{};                       // 场景三通道的实际绘制调用
    std::array<Drawable, 4> mMaterialSpheres;                // 成对比较粗糙度与金属度的球
    std::optional<TerrainRenderer> mTerrain;  // 共享地形、道路与流送状态
    DepthTarget mDepth;  // 与颜色附件同尺寸的相机深度目标
    WGPUColor mClearColor{};                               // 与颜色目标编码匹配的背景色
};

}  // namespace tinyworld
