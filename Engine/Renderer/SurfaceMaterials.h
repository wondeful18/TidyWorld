#pragma once

#include "Engine/Renderer/WgpuHandle.h"
#include "Engine/Scene/SceneMesh.h"
#include "Engine/Terrain/MaterialSplat.h"

namespace tinyworld {

class WebGpuContext;
using MaterialPreviews = std::array<WGPUTextureView, 4>;

//————————————————————————————————————————————————————————————————————————————————
// 为地表材质持有草、碎石和裸岩的纹理数组及共享采样器。
//————————————————————————————————————————————————————————————————————————————————
class SurfaceMaterials {
public:
    //————————————————————————————————————————————————————————————————————————————————
    // 读取部署的六张贴图，生成 mip 链并建立片元着色器绑定。
    //————————————————————————————————————————————————————————————————————————————————
    SurfaceMaterials(WebGpuContext& gpu, const std::filesystem::path& assetRoot);

    //————————————————————————————————————————————————————————————————————————————————
    // 借用第 3 组的绑定布局。
    //————————————————————————————————————————————————————————————————————————————————
    [[nodiscard]] WGPUBindGroupLayout Layout() const { return mLayout.get(); }

    //————————————————————————————————————————————————————————————————————————————————
    // 借用颜色通道使用的纹理与采样器绑定。
    //————————————————————————————————————————————————————————————————————————————————
    [[nodiscard]] WGPUBindGroup Binding() const { return mBinding.get(); }
    [[nodiscard]] MaterialPreviews PreviewViews() const;

    //————————————————————————————————————————————————————————————————————————————————
    // 笔划改变权重后上传像素。底色和粗糙度纹理可继续使用。
    //————————————————————————————————————————————————————————————————————————————————
    void SyncPaint(WGPUQueue queue, const MaterialSplat& paint);

private:
    TextureHandle mBaseColor;      // sRGB 底色数组
    TextureHandle mRoughness;      // 线性粗糙度数组
    TextureHandle mWeightTexture;  // 线性二维 RGBA 权重
    TextureViewHandle mWeightView; // 供片元采样
    std::uint64_t mPaintRevision{};  // 上一次上传的修订号
    TextureViewHandle mColorView;  // 草、碎石、岩石三层的底色视图
    std::array<TextureViewHandle, 4> mPreviews;
    TextureViewHandle mRoughView;  // 同序的粗糙度视图
    SamplerHandle mSampler;        // 平铺和 mip 插值共用的采样器
    BindGroupLayoutHandle mLayout; // 着色器纹理槽布局
    BindGroupHandle mBinding;      // 绑定两个视图与采样器
};

//————————————————————————————————————————————————————————————————————————————————
// 生成半径一米的材质球，顶点法线和位置方向相同。
//————————————————————————————————————————————————————————————————————————————————
[[nodiscard]] std::vector<SceneVertex> MakeMaterialSphere();

}  // namespace tinyworld
