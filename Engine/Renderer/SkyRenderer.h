#pragma once

#include "Engine/Renderer/EnvironmentMap.h"
#include "Engine/Scene/WorldScene.h"

#include <array>
#include <filesystem>

#include <glm/vec4.hpp>

namespace tinyworld {

class WebGpuContext;

//————————————————————————————————————————————————————————————————————————————————
// 在相机颜色通道的最后绘制天空盒或天空球。
// 使用相机旋转与投影，天空通过远平面深度测试保留前景。
//————————————————————————————————————————————————————————————————————————————————
class SkyRenderer {
public:
    //————————————————————————————————————————————————————————————————————————————————
    // 从可执行文件的资源目录载入照片及 WGSL，创建背景与共享环境纹理。
    //————————————————————————————————————————————————————————————————————————————————
    SkyRenderer(WebGpuContext& gpu, WGPUTextureFormat colorFormat,
                WGPUTextureFormat depthFormat, const std::filesystem::path& assetRoot, bool withNight);

    //————————————————————————————————————————————————————————————————————————————————
    // 在已画完不透明物体的通道中输出选中的天空网格。
    //————————————————————————————————————————————————————————————————————————————————
    void Draw(WGPURenderPassEncoder pass, const glm::mat4& projection,
              const glm::mat4& view, SkyMode mode, glm::vec4 horizon, float nightMix) const;

    //————————————————————————————————————————————————————————————————————————————————
    // 借用资源，以便车身和背景采样同一环境 Cubemap。
    //————————————————————————————————————————————————————————————————————————————————
    [[nodiscard]] const EnvironmentMap& Environment() const { return mEnvironment; }

private:
    WebGpuContext& mGpu;                   // 借用上下文，用于逐帧上传天空常量
    EnvironmentMap mEnvironment;         // 背景与车身共用的纹理
    PipelineHandle mPipeline;             // 深度判断在远平面，关闭深度写入
    BufferHandle mUniform;                // 相机旋转投影与天空模式
    BindGroupHandle mViewBinding;          // 绑定相机常量
    std::array<BufferHandle, 2> mMeshes;    // 天空盒、天空球顶点缓冲
    std::array<std::uint32_t, 2> mCounts{}; // 对应网格的三角形列表顶点数
};

}  // namespace tinyworld
