#pragma once

#include "Engine/Renderer/WgpuHandle.h"

#include <array>
#include <filesystem>

#include <glm/vec3.hpp>

namespace tinyworld {

class WebGpuContext;

//————————————————————————————————————————————————————————————————————————————————
// 持有同一片山地环境的 Cubemap 与等距柱状全景照片。
// 背景和车漆共用环境绑定。纹理内容只在启动时上传。
//————————————————————————————————————————————————————————————————————————————————
class EnvironmentMap {
public:
    //————————————————————————————————————————————————————————————————————————————————
    // 读取仓库随附的全景照片，根据面坐标生成 Cubemap。
    //————————————————————————————————————————————————————————————————————————————————
    EnvironmentMap(WebGpuContext& gpu, const std::filesystem::path& panoramaPath);

    //————————————————————————————————————————————————————————————————————————————————
    // 借用环境绑定布局，供天空与车身管线创建时使用。
    //————————————————————————————————————————————————————————————————————————————————
    [[nodiscard]] WGPUBindGroupLayout Layout() const { return mLayout.get(); }

    //————————————————————————————————————————————————————————————————————————————————
    // 借用纹理和采样器的绑定组，有效期与本对象相同。
    //————————————————————————————————————————————————————————————————————————————————
    [[nodiscard]] WGPUBindGroup Binding() const { return mBinding.get(); }

    //————————————————————————————————————————————————————————————————————————————————
    // 从同一张全景图取得观察方向附近的天际线色，返回线性 RGB。
    //————————————————————————————————————————————————————————————————————————————————
    [[nodiscard]] glm::vec3 HorizonColor(glm::vec3 direction) const;

private:
    std::array<glm::vec3, 32> mHorizon{};  // 沿经度采样的 32 个低空平均色，在线性空间插值
    TextureHandle mCube;           // 六层 sRGB 纹理，面顺序为 +X、-X、+Y、-Y、+Z、-Z
    TextureHandle mPanorama;       // 单层 2:1 等距柱状 sRGB 纹理
    TextureViewHandle mCubeView;   // 以 Cube 维度解释六层纹理
    TextureViewHandle mPanoView;   // 全景图的 2D 视图
    SamplerHandle mCubeSampler;    // 天空盒与车身共用的线性采样器
    SamplerHandle mPanoSampler;    // 水平方向循环、竖直方向钳位的线性采样器
    BindGroupLayoutHandle mLayout; // 天空与物体管线共用的环境布局
    BindGroupHandle mBinding;      // 两张环境纹理及对应采样器的绑定组
};

}  // namespace tinyworld
