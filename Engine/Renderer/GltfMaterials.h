#pragma once

#include "Engine/Renderer/WgpuHandle.h"

namespace tinyworld {

class WebGpuContext;
struct GltfMaterial;

//————————————————————————————————————————————————————————————————————————————————
// 管理导入材质的 GPU 布局与采样器。每份材质绑定可以供多个实例使用。
//————————————————————————————————————————————————————————————————————————————————
class GltfMaterials {
public:
    //————————————————————————————————————————————————————————————————————————————————
    // 建立底色、金属粗糙度纹理和系数的绑定约定。
    //————————————————————————————————————————————————————————————————————————————————
    explicit GltfMaterials(WebGpuContext& gpu);

    //————————————————————————————————————————————————————————————————————————————————
    // 返回借用的布局，供 PBR 渲染管线创建使用。
    //————————————————————————————————————————————————————————————————————————————————
    [[nodiscard]] WGPUBindGroupLayout Layout() const { return mLayout.get(); }

    //————————————————————————————————————————————————————————————————————————————————
    // 上传材质，返回保有两张纹理与系数缓冲的绑定。
    //————————————————————————————————————————————————————————————————————————————————
    [[nodiscard]] BindGroupHandle Upload(const GltfMaterial& source) const;

private:
    WebGpuContext& mGpu;              // 借用渲染设备与上传队列
    BindGroupLayoutHandle mLayout;   // glTF 管线第四组绑定
    SamplerHandle mSampler;          // 使用线性过滤和重复寻址
};

}  // namespace tinyworld
