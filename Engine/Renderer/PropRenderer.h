#pragma once

#include "Engine/Renderer/GltfMaterials.h"
#include "Engine/Renderer/WorldDraw.h"
#include "Engine/Scene/WorldScene.h"

namespace tinyworld {

class TerrainRenderer;

//————————————————————————————————————————————————————————————————————————————————
// 地标和摆件共享模型原型，每个摆放实例拥有独立变换。
//————————————————————————————————————————————————————————————————————————————————

class PropRenderer {
public:
    enum class Pass { BuiltinColor, GltfColor, Shadows };

    PropRenderer(WebGpuContext& gpu, WGPUTextureFormat format, const WorldScene& scene,
                 const std::filesystem::path& root, WGPUBindGroupLayout layout,
                 WGPUBindGroupLayout shadowLayout, WGPUBindGroupLayout environmentLayout);

    void Prepare(const WorldScene& scene, const TerrainRenderer& terrain, const DrawUniformWriter& uniforms,
                 const glm::vec4& shadows);
    unsigned Draw(WGPURenderPassEncoder pass, Pass layer) const;

private:
    void LoadGltfProps(const std::filesystem::path& root, bool withLightmaps);

    //————————————————————————————————————————————————————————————————————————————————
    // 原型的一个三角形批次，带与其他实例共享的材质绑定。
    //————————————————————————————————————————————————————————————————————————————————
    struct GltfDraw {
        Drawable mesh;             // 原型顶点和三角形顶点数
        std::size_t material{};    // 对应同一原型的 GPU 材质索引
    };

    //————————————————————————————————————————————————————————————————————————————————
    // 一份 glTF 原型的数据，多次摆放也只上传一次。
    //————————————————————————————————————————————————————————————————————————————————
    struct GltfPrototype {
        std::vector<GltfDraw> meshes;           // 按材质划分的网格批次
        std::vector<BindGroupHandle> materials; // 底色、金属粗糙度与系数
    };

    WebGpuContext& mGpu; // 共享纹理与原型网格从该设备创建
    WGPUBindGroupLayout mObjectLayout; // 借用每实例的常量布局
    Drawable mTree;      // 同时作固定地标和摆件
    Drawable mHouse;     // 同时作固定地标和摆件
    Drawable mParkedCar; // 游线样区内的停车参照物
    Drawable mRock;      // 可重复摆放的原型
    Drawable mCabin;     // 可重复摆放的原型
    std::vector<PlacedProp> mVisibleProps; // 当前地形窗口内提交的实例
    std::vector<Drawable> mPropDraws; // 与当前实例一一对应的绑定
    std::optional<GltfMaterials> mGltfMaterials; // 共享 glTF 纹理布局
    PipelineHandle mGltfPipeline;    // glTF 单独使用 UV 材质流水线
    std::array<GltfPrototype, kPropAssets.size() - kBuiltinPropCount> mGltfProps;
};

}  // namespace tinyworld
