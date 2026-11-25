#include "Engine/Renderer/PropRenderer.h"

#include "Engine/Renderer/ShaderModule.h"
#include "Engine/Renderer/TerrainRenderer.h"
#include "Engine/Renderer/WebGpuContext.h"
#include "Engine/Scene/GltfAsset.h"
#include "Engine/Scene/Lightmap.h"

#include <stdexcept>

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 固定的树、小屋和停车位在地形章节里继续提供尺度参照。
//————————————————————————————————————————————————————————————————————————————————

PropRenderer::PropRenderer(WebGpuContext& gpu, WGPUTextureFormat format, const WorldScene& scene,
                           const std::filesystem::path& root, WGPUBindGroupLayout layout,
                           WGPUBindGroupLayout shadowLayout, WGPUBindGroupLayout environmentLayout)
    : mGpu(gpu), mObjectLayout(layout) {
    mTree = UploadIndexedMesh(gpu, WithReflectivity(LoadSceneMesh(root / scene.objects[WorldScene::kTreeIndex].asset)), layout, "Tree");
    mHouse = UploadIndexedMesh(gpu, WithReflectivity(LoadSceneMesh(root / scene.objects[WorldScene::kHouseIndex].asset)), layout, "House");
    CarSurfaceRanges surfaces;
    const auto car = MakeRoamingCarMesh(&surfaces);
    mParkedCar = UploadIndexedMesh(gpu, WithReflectivity(car, surfaces), layout, "Parked car");
    if (scene.canPlaceProps) {
        mRock = UploadIndexedMesh(gpu, WithReflectivity(MakeRockMesh()), layout, "Rock");
        mCabin = UploadIndexedMesh(gpu, WithReflectivity(MakeCabinMesh()), layout, "Cabin");
    }
    const auto shaders = root / "shaders";
    if (scene.canUseGltfProps) {
        if (!shadowLayout || !environmentLayout) {
            throw std::runtime_error("Village models require the shadow, sky and material pipelines.");
        }
        mGltfMaterials.emplace(gpu);
        const auto gltfShader = LoadWgslModule(gpu, shaders / "prop_gltf.wgsl",
            {"output_color.wgsl", "fog.wgsl", "world.wgsl", "shadow.wgsl", "pbr.wgsl"});
        mGltfPipeline = BuildPipeline(gpu, format, gltfShader.get(), layout, shadowLayout,
                                      environmentLayout, mGltfMaterials->Layout(), false, true);
        LoadGltfProps(root, scene.lightmapEnabled.has_value());
    }

}

//————————————————————————————————————————————————————————————————————————————————
// 按原型上传顶点与两张材质纹理，实例之间共享这些 GPU 资源。
//————————————————————————————————————————————————————————————————————————————————
void PropRenderer::LoadGltfProps(const std::filesystem::path& root, bool withLightmaps) {
    for (std::size_t i = 0; i < mGltfProps.size(); ++i) {
        const auto& asset = kPropAssets[i + kBuiltinPropCount];
        if (asset.lightmap && !withLightmaps) continue;
        const auto source = LoadGltfAsset(root / asset.id);
        TextureViewHandle atlas;
        if (asset.lightmap) atlas = mGltfMaterials->UploadLightmap(ReadLightmap(root / asset.lightmap));
        auto& prototype = mGltfProps[i];
        for (const auto& material : source.materials) {
            prototype.materials.push_back(mGltfMaterials->Upload(material, atlas.get()));
        }
        for (const auto& primitive : source.primitives) {
            if (asset.lightmap && !primitive.hasLightmapUv) {
                throw std::runtime_error("Missing TEXCOORD_1 in baked prop.");
            }
            std::vector<WorldVertex> vertices;
            vertices.reserve(primitive.vertices.size());
            for (const auto& vertex : primitive.vertices) {
                vertices.push_back({vertex.geometry, 0.0f, vertex.uv, vertex.lightmapUv});
            }
            auto mesh = UploadIndexedMesh(mGpu, vertices, primitive.indices, nullptr, asset.name);
            prototype.meshes.push_back({std::move(mesh), primitive.material});
        }
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 为相机当前需要的对象写入变换，两个通道使用同一批绑定。
//————————————————————————————————————————————————————————————————————————————————

void PropRenderer::Prepare(const WorldScene& scene, const TerrainRenderer& terrain,
                           const DrawUniformWriter& uniforms, const glm::vec4& shadows) {
    uniforms.Set(mTree, scene.objects[WorldScene::kTreeIndex].transform.Matrix(), shadows);
    uniforms.Set(mHouse, scene.objects[WorldScene::kHouseIndex].transform.Matrix(), shadows);
    uniforms.Set(mParkedCar, scene.objects[WorldScene::kCarIndex].transform.Matrix(), shadows);
    mVisibleProps.clear();
    if (scene.storage) {
        for (const auto& tile : terrain.TileProps()) {
            mVisibleProps.insert(mVisibleProps.end(), tile.begin(), tile.end());
        }
    } else {
        for (const auto& prop : scene.props) {
            if (terrain.ResidentAt(prop.transform.position)) mVisibleProps.push_back(prop);
        }
    }
    while (mPropDraws.size() < mVisibleProps.size()) mPropDraws.push_back(MakeDrawBinding(mGpu, mObjectLayout));
    for (std::size_t i = 0; i < mVisibleProps.size(); ++i) {
        const auto& prop = mVisibleProps[i];
        SurfaceParameters material;
        if (scene.lightmapEnabled.value_or(false) && !prop.lightmap.empty()) material.bakedIrradiance = kLightmapRange;
        uniforms.Set(mPropDraws[i], prop.transform.Matrix(), shadows, material);
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 颜色通道分开绘制自带网格与 glTF，太阳深度只读取位置。
//————————————————————————————————————————————————————————————————————————————————

unsigned PropRenderer::Draw(WGPURenderPassEncoder pass, Pass layer) const {
    unsigned draws = 0;
    if (layer == Pass::GltfColor) {
        if (!mGltfPipeline) return 0;
        wgpuRenderPassEncoderSetPipeline(pass, mGltfPipeline.get());
    } else {
        DrawMesh(pass, mTree);
        DrawMesh(pass, mHouse);
        DrawMesh(pass, mParkedCar);
        draws += 3;
    }
    const std::array builtins{&mRock, &mTree, &mHouse, &mCabin};
    for (std::size_t i = 0; i < mVisibleProps.size(); ++i) {
        const auto asset = mVisibleProps[i].asset;
        const auto instance = mPropDraws[i].binding.get();
        if (asset < kBuiltinPropCount) {
            if (layer == Pass::GltfColor) continue;
            DrawMesh(pass, *builtins[asset], instance);
            ++draws;
        } else if (layer != Pass::BuiltinColor) {
            const auto& prototype = mGltfProps[asset - kBuiltinPropCount];
            for (const auto& draw : prototype.meshes) {
                if (layer == Pass::GltfColor) {
                    wgpuRenderPassEncoderSetBindGroup(pass, 3, prototype.materials[draw.material].get(), 0, nullptr);
                }
                DrawMesh(pass, draw.mesh, instance);
                ++draws;
            }
        }
    }
    return draws;
}

}  // namespace tinyworld
