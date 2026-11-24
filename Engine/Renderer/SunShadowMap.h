#pragma once

#include "Engine/Renderer/WgpuHandle.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace tinyworld {

class WebGpuContext;

//————————————————————————————————————————————————————————————————————————————————
// 为小屋场景持有单张太阳深度图、深度管线和后续采样所需的绑定。
//————————————————————————————————————————————————————————————————————————————————
class SunShadowMap {
public:
    //————————————————————————————————————————————————————————————————————————————————
    // depthShader 提供太阳视角的顶点入口，objectLayout 与颜色通道共用。
    //————————————————————————————————————————————————————————————————————————————————
    SunShadowMap(WebGpuContext& gpu, WGPUShaderModule depthShader, WGPUBindGroupLayout objectLayout,
                 bool vegetation = false);

    //————————————————————————————————————————————————————————————————————————————————
    // 以太阳方向和固定场景范围构建正交视图投影。azimuth 的单位为度。
    //————————————————————————————————————————————————————————————————————————————————
    [[nodiscard]] static glm::mat4 ViewProjection(glm::vec3 towardSun, float azimuth, float rangeScale = 1.0f);

    //————————————————————————————————————————————————————————————————————————————————
    // 清空深度图并绑定深度管线。调用方提交物体绘制后结束通道。
    //————————————————————————————————————————————————————————————————————————————————
    [[nodiscard]] PassHandle Begin(WGPUCommandEncoder encoder) const;

    //————————————————————————————————————————————————————————————————————————————————
    // 借用颜色管线的采样布局，生命周期由本对象管理。
    //————————————————————————————————————————————————————————————————————————————————
    [[nodiscard]] WGPUBindGroupLayout SamplingLayout() const { return mSamplingLayout.get(); }

    //————————————————————————————————————————————————————————————————————————————————
    // 借用包含深度视图与比较采样器的绑定组，只用于颜色通道。
    //————————————————————————————————————————————————————————————————————————————————
    [[nodiscard]] WGPUBindGroup SamplingBinding() const { return mSamplingBinding.get(); }
    [[nodiscard]] WGPURenderPipeline VegetationPipeline() const { return mVegetationPipeline.get(); }

private:
    TextureHandle mTexture;               // 固定分辨率深度纹理，兼作附件和采样输入
    TextureViewHandle mView;              // 两个通道共用的深度视图
    SamplerHandle mComparisonSampler;     // 最近邻深度比较，WGSL 决定采样次数
    BindGroupLayoutHandle mSamplingLayout; // 颜色管线读取深度图所用的绑定布局
    BindGroupHandle mSamplingBinding;     // 将深度图与比较采样器连到颜色管线
    PipelineHandle mDepthPipeline;       // 只有顶点和深度输出的太阳视角管线
    PipelineHandle mVegetationPipeline;
};

}  // namespace tinyworld
