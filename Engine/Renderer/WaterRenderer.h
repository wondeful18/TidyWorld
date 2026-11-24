#pragma once

#include "Engine/Platform/Window.h"
#include "Engine/Renderer/FrameAtmosphere.h"
#include "Engine/Renderer/WgpuHandle.h"
#include "Engine/Scene/WaterSettings.h"

#include <filesystem>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace tinyworld {

class EnvironmentMap;
class WebGpuContext;

//————————————————————————————————————————————————————————————————————————————————
// 在不透明通道之后绘制水。读取相机深度和第四章的环境贴图。
//————————————————————————————————————————————————————————————————————————————————
class WaterRenderer {
public:
    //————————————————————————————————————————————————————————————————————————————————
    // 创建水面顶点、颜色混合管线和相机常量资源。
    //————————————————————————————————————————————————————————————————————————————————
    WaterRenderer(WebGpuContext& gpu, WGPUTextureFormat format, const EnvironmentMap& environment,
                  const std::filesystem::path& shaderPath, bool coast);

    //————————————————————————————————————————————————————————————————————————————————
    // 从不透明深度重建水下地形，再在已有颜色附件上叠画水面。
    // depthView 变化时重建采样绑定。通道不再把该视图当作可写附件。
    //————————————————————————————————————————————————————————————————————————————————
    void Encode(WGPUCommandEncoder encoder, WGPUTextureView target, WGPUTextureView depthView,
                const glm::mat4& viewProjection, glm::vec3 camera, const WaterSettings& settings,
                glm::vec3 horizon, const FrameAtmosphere& air);

private:
    WebGpuContext& mGpu;                    // 借用设备与上传队列
    const EnvironmentMap& mEnvironment;    // 与天空共用 Cubemap，寿命覆盖水面
    PipelineHandle mPipeline;              // 颜色混合及水深重建管线
    BindGroupLayoutHandle mLayout;         // 深度视图与逐帧 uniform 的布局
    BindGroupHandle mBinding;              // 深度尺寸变化时按当前视图重建
    WGPUTextureView mBoundDepth{};          // 用于比较的借用句柄，绑定组保有引用
    BufferHandle mVertices;                // 湖面与溪道连成一条三角形带
    BufferHandle mUniform;                 // 视图投影、逆矩阵、相机位置与显示模式
    std::uint32_t mVertexCount{};           // 三角形列表的总顶点数
};

}  // namespace tinyworld
