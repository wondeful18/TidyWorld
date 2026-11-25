#pragma once

#include "Engine/Renderer/WgpuHandle.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <array>

namespace tinyworld {

class WebGpuContext;

//————————————————————————————————————————————————————————————————————————————————
// 同一组矩阵用于写入深度和读取阴影，分界距离沿相机前向量计量。
//————————————————————————————————————————————————————————————————————————————————
struct SunShadowFrame {
    std::array<glm::mat4, 4> matrices{}; // 各级从地图坐标到太阳裁剪空间
    glm::vec4 splits{0.0f};              // 各级远端，单位为米
    glm::vec4 eyeAndCount{0.0f};         // XYZ 是相机位置，W 是本帧级数
    glm::vec4 forward{0.0f};             // 相机前向量
};


//————————————————————————————————————————————————————————————————————————————————
// 为小屋场景持有单张太阳深度图、深度管线和后续采样所需的绑定。
//————————————————————————————————————————————————————————————————————————————————
class SunShadowMap {
public:
    //————————————————————————————————————————————————————————————————————————————————
    // depthShader 提供太阳视角的顶点入口，objectLayout 与颜色通道共用。
    //————————————————————————————————————————————————————————————————————————————————
    SunShadowMap(WebGpuContext& gpu, WGPUShaderModule depthShader, WGPUBindGroupLayout objectLayout,
                 bool vegetation = false, bool cascades = false);

    //————————————————————————————————————————————————————————————————————————————————
    // 以太阳方向和固定场景范围构建正交视图投影。azimuth 的单位为度。
    //————————————————————————————————————————————————————————————————————————————————
    [[nodiscard]] static glm::mat4 ViewProjection(glm::vec3 towardSun, float azimuth, float rangeScale = 1.0f);

    //————————————————————————————————————————————————————————————————————————————————
    // 清空深度图并绑定深度管线。调用方提交物体绘制后结束通道。
    //————————————————————————————————————————————————————————————————————————————————
    [[nodiscard]] PassHandle Begin(WGPUCommandEncoder encoder, unsigned layer = 0) const;

    //————————————————————————————————————————————————————————————————————————————————
    // 为当前相机拟合多级正交区域，并在光源平面按 texel 对齐以抑制晃动。
    //————————————————————————————————————————————————————————————————————————————————
    [[nodiscard]] static SunShadowFrame FitCascades(glm::vec3 eye, glm::vec3 forward, const glm::mat4& projection,
                                                     glm::vec3 towardSun, float azimuth);
    void Upload(WGPUQueue queue, const SunShadowFrame& frame);

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
    std::array<TextureViewHandle, 4> mLayerViews;  // 每级独立的深度附件视图
    std::array<BufferHandle, 4> mLayerMatrices;   // 共享队列写入，每个通道使用不同的矩阵
    std::array<BindGroupHandle, 4> mLayerBindings; // 深度通道绑定
    BufferHandle mFrameUniform;              // 颜色通道采样用的级联参数
    BindGroupLayoutHandle mDepthLayout;       // 深度通道每级的矩阵布局
    TextureViewHandle mView;              // 两个通道共用的深度视图
    SamplerHandle mComparisonSampler;     // 最近邻深度比较，WGSL 决定采样次数
    BindGroupLayoutHandle mSamplingLayout; // 颜色管线读取深度图所用的绑定布局
    BindGroupHandle mSamplingBinding;     // 将深度图与比较采样器连到颜色管线
    PipelineHandle mDepthPipeline;       // 只有顶点和深度输出的太阳视角管线
    PipelineHandle mVegetationPipeline;
};

}  // namespace tinyworld
