#include "Engine/Renderer/WorldRenderer.h"

#include "Engine/Renderer/Color.h"
#include "Engine/Renderer/FrameAtmosphere.h"
#include "Engine/Renderer/RenderPass.h"
#include "Engine/Renderer/ShaderModule.h"
#include "Engine/Renderer/WebGpuContext.h"
#include "Engine/Scene/DayCycle.h"
#include "Engine/Scene/GltfAsset.h"
#include "Engine/Scene/Lightmap.h"
#include "Engine/Scene/RobotMesh.h"
#include "Engine/Scene/RoamingCarMesh.h"
#include "Engine/Scene/SceneMesh.h"
#include "Engine/Scene/WalkerMesh.h"
#include "Engine/Terrain/TerrainMesh.h"

#include <glm/matrix.hpp>
#include <glm/vec4.hpp>

#include <cstddef>
#include <stdexcept>
#include <vector>

namespace tinyworld {
namespace {

constexpr auto kDepthFormat = WGPUTextureFormat_Depth24Plus;

//————————————————————————————————————————————————————————————————————————————————
// 区分车漆与玻璃，其他网格顶点的环境反射权重为零。
//————————————————————————————————————————————————————————————————————————————————
std::vector<WorldVertex> WithReflectivity(const std::vector<SceneVertex>& geometry,
                                         const CarSurfaceRanges& surfaces = {}) {
    std::vector<WorldVertex> vertices;
    vertices.reserve(geometry.size());
    for (std::size_t i = 0; i < geometry.size(); ++i) {
        float reflection = 0.0f;
        if (i < surfaces.reflectiveEnd) {
            const bool glass = i >= surfaces.glassBegin && i < surfaces.glassEnd;
            reflection = glass ? 1.0f : 0.47f;
        }
        vertices.push_back({geometry[i], reflection});
    }
    return vertices;
}

//————————————————————————————————————————————————————————————————————————————————
// 按 WGSL 对齐顺序打包每个物体的矩阵与光照参数。
//————————————————————————————————————————————————————————————————————————————————
struct DrawUniform {
    glm::mat4 viewProjection;  // 将世界坐标转换到 WebGPU 裁剪空间
    glm::mat4 model;           // 将模型顶点转换到世界空间
    glm::mat4 normalToWorld;   // 模型矩阵的逆转置，处理非均匀缩放
    glm::vec4 towardSun;       // 从表面指向太阳的世界方向
    glm::vec4 sunRadiance;     // 线性灯光颜色乘以强度
    glm::mat4 lightViewProjection;  // 从世界空间到太阳的裁剪空间
    glm::vec4 shadowControls;      // 依次为阴影开关、PCF 开关、基础深度偏移和保留槽
    glm::vec4 cameraAndReflection; // xyz 为世界相机位置，w 为车身反射开关
    glm::vec4 material;            // 材质类别和对应的参数
    glm::vec4 coast;               // 海岸开关、水位 Y、岸坡过渡的 X 起点和终点
    FrameAtmosphere air;           // 物体、植被与水面共用的距离参数
    glm::vec4 lighting;             // 环境亮度与夜景混合，z/w 保留
};

static_assert(offsetof(DrawUniform, lightViewProjection) == 224);
static_assert(sizeof(DrawUniform) == 400);

//————————————————————————————————————————————————————————————————————————————————
// 显式定义物体布局，使太阳深度和颜色管线可共用每个物体的绑定。
//————————————————————————————————————————————————————————————————————————————————
BindGroupLayoutHandle BuildObjectLayout(WebGpuContext& gpu) {
    WGPUBindGroupLayoutEntry uniform = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    uniform.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    uniform.buffer.type = WGPUBufferBindingType_Uniform;
    uniform.buffer.minBindingSize = sizeof(DrawUniform);

    WGPUBindGroupLayoutDescriptor description = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    description.entryCount = 1;
    description.entries = &uniform;
    wgpuDevicePushErrorScope(gpu.Device(), WGPUErrorFilter_Validation);
    BindGroupLayoutHandle layout(wgpuDeviceCreateBindGroupLayout(gpu.Device(), &description));
    gpu.CheckValidation("world object bind group layout");
    if (!layout) {
        throw std::runtime_error("Cannot create world object layout");
    }
    return layout;
}

//————————————————————————————————————————————————————————————————————————————————
// 创建相机颜色管线。第三章额外绑定太阳深度图，第二章沿用 Lambert 入口。
//————————————————————————————————————————————————————————————————————————————————
PipelineHandle BuildPipeline(WebGpuContext& gpu, WGPUTextureFormat format, WGPUShaderModule shader,
                             WGPUBindGroupLayout objects, WGPUBindGroupLayout shadows,
                             WGPUBindGroupLayout environment, WGPUBindGroupLayout materials,
                             bool vegetation = false, bool gltf = false) {
    const std::array layouts{objects, shadows, environment, materials};
    WGPUPipelineLayoutDescriptor pipelineLayout = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    pipelineLayout.bindGroupLayoutCount = materials ? 4 : (environment ? 3 : (shadows ? 2 : 1));
    pipelineLayout.bindGroupLayouts = layouts.data();

    std::array<WGPUVertexAttribute, 6> attributes{};
    constexpr auto geometry = offsetof(WorldVertex, geometry);
    constexpr std::array offsets{geometry + offsetof(SceneVertex, position),
                                 geometry + offsetof(SceneVertex, normal),
                                 geometry + offsetof(SceneVertex, color),
                                 offsetof(WorldVertex, reflectivity), offsetof(WorldVertex, uv),
                                 offsetof(WorldVertex, lightmapUv)};
    for (std::uint32_t i = 0; i < attributes.size(); ++i) {
        attributes[i] = WGPU_VERTEX_ATTRIBUTE_INIT;
        attributes[i].format = WGPUVertexFormat_Float32x3;
        if (i == 3) attributes[i].format = WGPUVertexFormat_Float32;
        if (i >= 4) attributes[i].format = WGPUVertexFormat_Float32x2;
        attributes[i].offset = offsets[i];
        attributes[i].shaderLocation = i;
    }
    WGPUVertexBufferLayout layout = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
    layout.arrayStride = sizeof(WorldVertex);
    layout.stepMode = WGPUVertexStepMode_Vertex;
    layout.attributeCount = gltf ? attributes.size() : (environment ? 4 : 3);
    layout.attributes = attributes.data();

    WGPUColorTargetState color = WGPU_COLOR_TARGET_STATE_INIT;
    color.format = format;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = shader;
    const bool srgb = IsSrgbFormat(format);
    const char* entry = srgb ? "fsLinear" : "fsDisplay";
    if (shadows) {
        entry = srgb ? "fsShadowedLinear" : "fsShadowedDisplay";
    }
    if (environment) {
        entry = srgb ? "fsEnvironmentLinear" : "fsEnvironmentDisplay";
    }
    if (materials) {
        entry = srgb ? "fsMaterialsLinear" : "fsMaterialsDisplay";
    }
    if (gltf) {
        entry = srgb ? "fsGltfLinear" : "fsGltfDisplay";
    }
    if (vegetation) {
        entry = srgb ? "fsVegetationLinear" : "fsVegetationDisplay";
    }
    fragment.entryPoint = {entry, WGPU_STRLEN};
    fragment.targetCount = 1;
    fragment.targets = &color;

    WGPUDepthStencilState depth = WGPU_DEPTH_STENCIL_STATE_INIT;
    depth.format = kDepthFormat;
    depth.depthCompare = WGPUCompareFunction_Less;
    depth.depthWriteEnabled = WGPUOptionalBool_True;

    WGPURenderPipelineDescriptor description = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    description.label = {vegetation ? "Vegetation color pipeline" : "World color pipeline", WGPU_STRLEN};
    description.vertex.module = shader;
    const char* vertexEntry = shadows ? "vsShadowedWorld" : "vsWorld";
    if (environment) vertexEntry = "vsEnvironmentWorld";
    if (gltf) vertexEntry = "vsGltf";
    description.vertex.entryPoint = {vertexEntry, WGPU_STRLEN};
    description.vertex.bufferCount = 1;
    description.vertex.buffers = &layout;
    description.primitive.cullMode = WGPUCullMode_Back;
    const VegetationVertexLayout vegetationLayout;
    if (vegetation) {
        description.vertex.entryPoint = {"vsVegetation", WGPU_STRLEN};
        description.vertex.bufferCount = vegetationLayout.buffers.size();
        description.vertex.buffers = vegetationLayout.buffers.data();
        description.primitive.cullMode = WGPUCullMode_None;
    }
    description.depthStencil = &depth;
    description.fragment = &fragment;

    wgpuDevicePushErrorScope(gpu.Device(), WGPUErrorFilter_Validation);
    PipelineLayoutHandle bindLayouts(wgpuDeviceCreatePipelineLayout(gpu.Device(), &pipelineLayout));
    description.layout = bindLayouts.get();
    PipelineHandle pipeline(bindLayouts ? wgpuDeviceCreateRenderPipeline(gpu.Device(), &description) : nullptr);
    gpu.CheckValidation("world color pipeline");
    if (!bindLayouts || !pipeline) {
        throw std::runtime_error("Cannot create world color pipeline");
    }
    return pipeline;
}

//————————————————————————————————————————————————————————————————————————————————
// 创建可由 Queue 上传数据的缓冲，并检查分配结果。
//————————————————————————————————————————————————————————————————————————————————
BufferHandle MakeBuffer(WGPUDevice device, WGPUBufferUsage usage, std::uint64_t size, const char* label) {
    WGPUBufferDescriptor description = WGPU_BUFFER_DESCRIPTOR_INIT;
    description.label = {label, WGPU_STRLEN};
    description.usage = usage | WGPUBufferUsage_CopyDst;
    description.size = size;
    BufferHandle buffer(wgpuDeviceCreateBuffer(device, &description));
    if (!buffer) {
        throw std::runtime_error("Cannot create world GPU buffer");
    }
    return buffer;
}

}  // namespace

//————————————————————————————————————————————————————————————————————————————————
// 每个模型上传一次，并单独绑定一个 uniform 以支持编辑器变换。
//————————————————————————————————————————————————————————————————————————————————
WorldRenderer::WorldRenderer(WebGpuContext& gpu, WGPUTextureFormat format,
                             const WorldScene& scene, const std::filesystem::path& root)
    : mGpu(gpu) {
    const auto shaders = root / "shaders";
    const auto shader = LoadWgslModule(gpu, shaders / "world.wgsl", shaders / "fog.wgsl");
    mObjectLayout = BuildObjectLayout(gpu);
    const auto layout = mObjectLayout.get();
    if (scene.shadows || scene.environment || scene.vegetation) {
        mShadowMap.emplace(gpu, shader.get(), layout, scene.vegetation.has_value());
    }
    if (scene.environment) {
        mSky.emplace(gpu, format, kDepthFormat, root, scene.dayCycle.has_value());
    }
    if (scene.water) {
        if (!mSky) throw std::runtime_error("Water requires a sky environment.");
        mWater.emplace(gpu, format, mSky->Environment(), root / "shaders" / "water.wgsl", scene.water->coast);
    }
    if (scene.materials) {
        mMaterials.emplace(gpu, root);
        const auto sphere = WithReflectivity(MakeMaterialSphere());
        for (auto& draw : mMaterialSpheres) {
            draw = UploadMesh(sphere, layout, "Material comparison sphere");
        }
    }
    mPipeline = BuildPipeline(gpu, format, shader.get(), layout,
                              mShadowMap ? mShadowMap->SamplingLayout() : nullptr,
                              mSky ? mSky->Environment().Layout() : nullptr,
                              mMaterials ? mMaterials->Layout() : nullptr);
    if (scene.vegetation) {
        mVegetation.emplace(gpu);
        mVegetationUniform = MakeDrawBinding(layout);
        mVegetationPipeline = BuildPipeline(gpu, format, shader.get(), layout,
                                            mShadowMap->SamplingLayout(), nullptr, nullptr, true);
    }

    if (scene.canUseGltfProps) {
        if (!mShadowMap || !mSky || !mMaterials) {
            throw std::runtime_error("Village models require the shadow, sky and material pipelines.");
        }
        mGltfMaterials.emplace(gpu);
        mGltfPipeline = BuildPipeline(gpu, format, shader.get(), layout, mShadowMap->SamplingLayout(),
                                      mSky->Environment().Layout(), mGltfMaterials->Layout(), false, true);
        LoadGltfProps(root, scene.lightmapEnabled.has_value());
    }

    mClearColor = {0.89, 0.925, 0.941, 1.0};
    if (IsSrgbFormat(format)) {
        mClearColor.r = DisplayToLinear(mClearColor.r);
        mClearColor.g = DisplayToLinear(mClearColor.g);
        mClearColor.b = DisplayToLinear(mClearColor.b);
    }

    const std::size_t meshCount = scene.spawn ? mDraws.size() : WorldScene::kObjectCount;
    const auto* terrain = scene.Terrain();
    for (std::size_t i = terrain ? 1 : 0; i < meshCount; ++i) {
        CarSurfaceRanges carSurfaces;
        const bool walker = i == WorldScene::kObjectCount;
        const auto geometry = walker ? (terrain ? MakeRobotMesh() : MakeWalkerMesh())
            : i == WorldScene::kCarIndex ? MakeRoamingCarMesh(&carSurfaces)
            : LoadSceneMesh(root / scene.objects[i].asset);
        mDraws[i] = UploadMesh(WithReflectivity(geometry, carSurfaces), layout,
                               walker ? "Walker" : scene.objects[i].name);
    }
    if (scene.canPlaceProps) {
        mRock = UploadMesh(WithReflectivity(MakeRockMesh()), layout, "Rock");
        mCabin = UploadMesh(WithReflectivity(MakeCabinMesh()), layout, "Cabin");
    }
    if (terrain) {
        for (std::size_t i = 0; i < mRobotLimbs.size(); ++i) {
            mRobotLimbs[i] = UploadMesh(WithReflectivity(MakeRobotLimb(static_cast<RobotLimb>(i))), layout, "Robot limb");
        }
        for (int z = HeightField::kFirstChunk; z < HeightField::kEndChunk; ++z) {
            for (int x = HeightField::kFirstChunk; x < HeightField::kEndChunk; ++x) {
                const auto mesh = MakeTerrainChunk(*terrain, {x, z}, !scene.road);
                const auto index = mTerrainDraws.size();
                mTerrainDraws.push_back(UploadMesh(WithReflectivity(mesh), layout, "Terrain chunk"));
                if (scene.canPreviewLod) {
                    const auto coarse = MakeTerrainChunk(*terrain, {x, z}, true, true);
                    mCoarseTerrainDraws.push_back(UploadMesh(WithReflectivity(coarse), layout, "Terrain LOD 1"));
                }
                mChunkRevisions[index] = terrain->ChunkRevision(index);
            }
        }
        mChunkBorders.emplace(UploadMesh(WithReflectivity(MakeChunkBorders(*terrain)), layout, "Chunk borders"));
        mBorderRevision = terrain->Revision();
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 共边附近的笔刷会给两个块相同的新修订号，两个通道会一起看到新网格。
//————————————————————————————————————————————————————————————————————————————————
void WorldRenderer::SyncTerrain(const HeightField& field, bool showBorders, bool paintTrail) {
    std::size_t index = 0;
    for (int z = HeightField::kFirstChunk; z < HeightField::kEndChunk; ++z) {
        for (int x = HeightField::kFirstChunk; x < HeightField::kEndChunk; ++x, ++index) {
            const auto revision = field.ChunkRevision(index);
            if (mChunkRevisions[index] == revision) {
                continue;
            }
            const auto vertices = WithReflectivity(MakeTerrainChunk(field, {x, z}, paintTrail));
            wgpuQueueWriteBuffer(mGpu.Queue(), mTerrainDraws[index].vertices.get(), 0,
                                 vertices.data(), vertices.size() * sizeof(WorldVertex));
            mChunkRevisions[index] = revision;
        }
    }
    if (showBorders && mBorderRevision != field.Revision()) {
        const auto vertices = WithReflectivity(MakeChunkBorders(field));
        wgpuQueueWriteBuffer(mGpu.Queue(), mChunkBorders->vertices.get(), 0,
                             vertices.data(), vertices.size() * sizeof(WorldVertex));
        mBorderRevision = field.Revision();
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 避让边距不改变路面，拖动控制点或雕刻高度才需要裁剪并上传。
//————————————————————————————————————————————————————————————————————————————————
void WorldRenderer::SyncRoad(const HeightField& field, const RoadSpline& road) {
    if (mLastRoad && mLastRoad->points == road.points && mLastRoad->width == road.width &&
        mLastRoad->junction == road.junction && mLastRoad->junctionPoint == road.junctionPoint &&
        mLastRoad->branchLength == road.branchLength &&
        mRoadTerrainRevision == field.Revision()) {
        return;
    }
    const auto mesh = MakeRoadMesh(field, road);
    if (mesh.empty()) {
        mRoadMesh.reset();
    } else {
        mRoadMesh.emplace(UploadMesh(WithReflectivity(mesh), mObjectLayout.get(), "Spline road"));
    }
    mLastRoad = road;
    mRoadTerrainRevision = field.Revision();
}

//————————————————————————————————————————————————————————————————————————————————
// 同一上传路径接收房屋、小车、机器人和地形的顶点数据。
//————————————————————————————————————————————————————————————————————————————————
WorldRenderer::Drawable WorldRenderer::UploadMesh(std::span<const WorldVertex> vertices,
                                                   WGPUBindGroupLayout layout, const char* name) {
    Drawable draw;
    if (layout) draw = MakeDrawBinding(layout);
    draw.count = static_cast<std::uint32_t>(vertices.size());
    const auto bytes = vertices.size_bytes();
    draw.vertices = MakeBuffer(mGpu.Device(), WGPUBufferUsage_Vertex, bytes, name);
    wgpuQueueWriteBuffer(mGpu.Queue(), draw.vertices.get(), 0, vertices.data(), bytes);
    return draw;
}

WorldRenderer::Drawable WorldRenderer::MakeDrawBinding(WGPUBindGroupLayout layout) {
    Drawable draw;
    draw.uniform = MakeBuffer(mGpu.Device(), WGPUBufferUsage_Uniform, sizeof(DrawUniform), "World object uniform");

    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.buffer = draw.uniform.get();
    entry.size = sizeof(DrawUniform);
    WGPUBindGroupDescriptor binding = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    binding.layout = layout;
    binding.entryCount = 1;
    binding.entries = &entry;
    draw.binding.reset(wgpuDeviceCreateBindGroup(mGpu.Device(), &binding));
    if (!draw.binding) {
        throw std::runtime_error("Cannot bind world object uniform");
    }
    return draw;
}

//————————————————————————————————————————————————————————————————————————————————
// 实例共用原型顶点缓冲，各自持有用于两条绘制通道的物体绑定。
//————————————————————————————————————————————————————————————————————————————————
void WorldRenderer::SyncProps(std::size_t count) {
    while (mPropDraws.size() < count) {
        mPropDraws.push_back(MakeDrawBinding(mObjectLayout.get()));
    }
    mPropDraws.resize(count);
}

//————————————————————————————————————————————————————————————————————————————————
// 按原型上传顶点与两张材质纹理，实例之间共享这些 GPU 资源。
//————————————————————————————————————————————————————————————————————————————————
void WorldRenderer::LoadGltfProps(const std::filesystem::path& root, bool withLightmaps) {
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
            auto mesh = UploadMesh(vertices, nullptr, asset.name);
            prototype.meshes.push_back({std::move(mesh), primitive.material});
        }
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 深度附件的尺寸须与场景颜色纹理完全相同。
//————————————————————————————————————————————————————————————————————————————————
void WorldRenderer::ResizeDepth(PixelSize size) {
    if (mDepthSize == size) {
        return;
    }
    WGPUTextureDescriptor description = WGPU_TEXTURE_DESCRIPTOR_INIT;
    description.label = {"Chapter 02 depth", WGPU_STRLEN};
    description.size = {size.width, size.height, 1};
    description.format = kDepthFormat;
    description.usage = WGPUTextureUsage_RenderAttachment;
    if (mWater) description.usage |= WGPUTextureUsage_TextureBinding;
    TextureHandle texture(wgpuDeviceCreateTexture(mGpu.Device(), &description));
    if (!texture) {
        throw std::runtime_error("Cannot allocate chapter 02 depth texture");
    }
    TextureViewHandle view(wgpuTextureCreateView(texture.get(), nullptr));
    if (!view) {
        throw std::runtime_error("Cannot create chapter 02 depth view");
    }
    mDepthView = std::move(view);
    mDepthTexture = std::move(texture);
    mDepthSize = size;
}

//————————————————————————————————————————————————————————————————————————————————
// 同一帧内物体变换只上传一次。先写入太阳深度，再切换到相机读取阴影。
//————————————————————————————————————————————————————————————————————————————————
void WorldRenderer::Encode(WGPUCommandEncoder encoder, WGPUTextureView target, PixelSize size,
                           const WorldScene& scene) {
    ResizeDepth(size);
    SyncProps(scene.props.size());
    if (mMaterials && scene.paint) {
        mMaterials->SyncPaint(mGpu.Queue(), *scene.paint);
    }
    if (const auto* terrain = scene.Terrain()) {
        SyncTerrain(*terrain, scene.showChunkBorders, !scene.road);
        if (scene.road) {
            SyncRoad(*terrain, *scene.road);
        }
        if (mVegetation && scene.vegetation) {
            mVegetation->Sync(*terrain, scene.road, *scene.vegetation);
        }
    }
    const auto& camera = scene.ActiveCamera();
    if (!mCoarseTerrainDraws.empty()) {
        std::size_t i = 0;
        const auto eye = camera.Position();
        for (int z = HeightField::kFirstChunk; z < HeightField::kEndChunk; ++z) {
            for (int x = HeightField::kFirstChunk; x < HeightField::kEndChunk; ++x, ++i) {
                mCoarseChoice[i] = scene.previewLod && IsCoarseTerrainChunk({x, z}, eye);
            }
        }
    }
    const bool hasCoast = scene.water && scene.water->coast;
    const auto projection = camera.Projection(static_cast<float>(size.width) / size.height, true,
                                              hasCoast ? WaterSettings::kCoastFar : 80.0f);
    const glm::vec4 coast{hasCoast ? 1.0f : 0.0f, WaterSettings::kLevel,
                         WaterSettings::kCoastStart, WaterSettings::kCoastEnd};
    const auto view = camera.View();
    const glm::mat4 vp = projection * view;
    const auto eye = camera.Position();
    const auto light = scene.dayCycle ? EvaluateDaylight(scene.dayCycle->hour) : Daylight{scene.sun};
    const glm::vec4 lighting{light.ambient, light.nightMix, 0.0f, 0.0f};
    const glm::vec4 horizon = hasCoast && mSky
        ? glm::vec4(mSky->Environment().HorizonColor(glm::normalize(camera.target - eye), light.nightMix), 1.0f)
        : glm::vec4{0.0f};
    FrameAtmosphere air;
    if (scene.atmosphere) {
        const auto& settings = *scene.atmosphere;
        air.colorDensity = {glm::vec3(horizon), settings.fogEnabled ? settings.fogDensity : 0.0f};
        air.distances = {settings.fogStart, settings.shadowDistance * AtmosphereSettings::kShadowFadeRatio,
                         settings.shadowDistance, 0.0f};
    }
    const bool reflectCar = scene.environment && scene.environment->carReflection;
    const glm::vec4 cameraAndReflection(eye, reflectCar ? 1.0f : 0.0f);
    const auto towardSun = light.sun.TowardLight();
    const glm::vec4 direction(towardSun, 0.0f);
    const glm::vec4 sun(light.sun.color * light.sun.intensity, 0.0f);
    glm::mat4 lightViewProjection{1.0f};
    glm::vec4 shadowControls{0.0f};
    // 当夜间直射已经为零，本帧不需要更新太阳深度图，接收端也不再查询它。
    const bool renderShadow = mShadowMap && scene.shadows && scene.shadows->enabled &&
                              (!scene.dayCycle || light.sun.intensity > 0.0f);
    if (renderShadow) {
        const float shadowScale = scene.terrain ? HeightField::kHalfExtent * (3.0f / 16.0f) : 1.0f;
        lightViewProjection = SunShadowMap::ViewProjection(towardSun, light.sun.azimuth, shadowScale);
        shadowControls = {1.0f, scene.shadows->pcf ? 1.0f : 0.0f, scene.shadows->depthBias, 0.0f};
    }

    std::size_t visibleCount = (scene.IsDriving() || scene.spawn) ? WorldScene::kObjectCount
                                                                : WorldScene::kCarIndex;
    if (scene.spawn && !scene.IsDriving()) {
        // 编辑时显示出生预览，步行时使用同一网格显示当前角色。
        visibleCount = mDraws.size();
    }
    const bool showSpheres = scene.materials && scene.materials->spheres;
    const bool showVegetation = mVegetation && scene.vegetation && scene.vegetation->visible && !showSpheres;
    if (showSpheres) {
        visibleCount = 0;
    }
    const auto uploadUniform = [&](const Drawable& draw, const glm::mat4& model, const glm::vec4& shadows,
                                   const glm::vec4& material = glm::vec4{0.0f}) {
        const DrawUniform uniforms{vp, model, glm::transpose(glm::inverse(model)), direction, sun,
                                   lightViewProjection, shadows, cameraAndReflection, material, coast, air, lighting};
        wgpuQueueWriteBuffer(mGpu.Queue(), draw.uniform.get(), 0, &uniforms, sizeof(uniforms));
    };
    for (std::size_t i = scene.terrain ? 1 : 0; i < visibleCount; ++i) {
        const glm::mat4 model = i == WorldScene::kObjectCount ? scene.WalkerPose().Matrix()
                                                           : scene.objects[i].transform.Matrix();
        uploadUniform(mDraws[i], model, shadowControls);
    }
    if (scene.terrain && visibleCount > WorldScene::kObjectCount) {
        const auto root = scene.WalkerPose().Matrix();
        const auto gait = scene.WalkerGait();
        for (std::size_t i = 0; i < mRobotLimbs.size(); ++i) {
            uploadUniform(mRobotLimbs[i], root * RobotLimbPose(static_cast<RobotLimb>(i), gait), shadowControls);
        }
    }
    if (!showSpheres) {
        for (std::size_t i = 0; i < scene.props.size(); ++i) {
            const auto& prop = scene.props[i];
            glm::vec4 material{0.0f};
            if (scene.lightmapEnabled.value_or(false) && !prop.lightmap.empty()) material.w = kLightmapRange;
            uploadUniform(mPropDraws[i], prop.transform.Matrix(), shadowControls, material);
        }
    }
    glm::vec4 terrainMaterial{0.0f};
    if (scene.materials && scene.materials->view != MaterialView::VertexColor) {
        terrainMaterial = {1.0f, static_cast<float>(scene.materials->view), scene.road ? 0.0f : 1.0f,
                           2.0f * HeightField::kHalfExtent};
    }
    for (std::size_t i = 0; i < mTerrainDraws.size(); ++i) {
        uploadUniform(mCoarseChoice[i] ? mCoarseTerrainDraws[i] : mTerrainDraws[i], glm::mat4{1.0f},
                      shadowControls, terrainMaterial);
    }
    if (showSpheres) {
        constexpr float radius = 0.83f;
        for (std::size_t i = 0; i < mMaterialSpheres.size(); ++i) {
            const float x = -3.24f + 2.16f * static_cast<float>(i);
            constexpr float z = 8.0f;
            const float y = scene.terrain->HeightAt({x, z}) + radius;
            glm::mat4 model{radius};
            model[3] = {x, y, z, 1.0f};
            const glm::vec4 settings{2.0f, i < 2 ? 0.0f : 1.0f, i % 2 == 0 ? 0.24f : 0.62f, 0.0f};
            uploadUniform(mMaterialSpheres[i], model, shadowControls, settings);
        }
    }
    const bool showBorders = mChunkBorders.has_value() && scene.showChunkBorders && !showSpheres;
    const bool showRoad = mRoadMesh.has_value() && !showSpheres;
    if (showRoad) {
        uploadUniform(*mRoadMesh, glm::mat4{1.0f}, shadowControls,
                      {3.0f, static_cast<float>(scene.road->surface), 0.0f, 0.0f});
    }
    if (showBorders) {
        uploadUniform(*mChunkBorders, glm::mat4{1.0f}, glm::vec4{0.0f});
    }
    mVegetationStats = {};
    if (mVegetation) {
        mVegetationStats.instances = mVegetation->Counts();
    }
    if (showVegetation) {
        uploadUniform(mVegetationUniform, glm::mat4{1.0f}, shadowControls);
    }
    if (renderShadow) {
        auto depthPass = mShadowMap->Begin(encoder);
        DrawObjects(depthPass.get(), visibleCount, showSpheres);
        if (!showSpheres) DrawProps(depthPass.get(), scene.props, PropPass::Shadows);
        if (showVegetation) {
            wgpuRenderPassEncoderSetPipeline(depthPass.get(), mShadowMap->VegetationPipeline());
            wgpuRenderPassEncoderSetBindGroup(depthPass.get(), 0, mVegetationUniform.binding.get(), 0, nullptr);
            mVegetationStats.shadowDraws = mVegetation->Draw(depthPass.get(), scene.vegetation->instanced, true);
        }
        wgpuRenderPassEncoderEnd(depthPass.get());
    }

    const bool showWater = mWater && scene.water && scene.water->view != WaterView::Off && !showSpheres;
    auto pass = BeginColorPass(encoder, target, mClearColor, "World color pass", mDepthView.get(),
                               showWater ? WGPUStoreOp_Store : WGPUStoreOp_Discard);
    wgpuRenderPassEncoderSetPipeline(pass.get(), mPipeline.get());
    if (mShadowMap) {
        wgpuRenderPassEncoderSetBindGroup(pass.get(), 1, mShadowMap->SamplingBinding(), 0, nullptr);
    }
    if (mSky) {
        wgpuRenderPassEncoderSetBindGroup(pass.get(), 2, mSky->Environment().Binding(), 0, nullptr);
    }
    if (mMaterials) {
        wgpuRenderPassEncoderSetBindGroup(pass.get(), 3, mMaterials->Binding(), 0, nullptr);
    }
    DrawObjects(pass.get(), visibleCount, showSpheres);
    if (!showSpheres) DrawProps(pass.get(), scene.props, PropPass::BuiltinColor);
    if (showRoad) {
        // 路面接收地形和地标的阴影；薄路面本身只在颜色通道绘制。
        DrawMesh(pass.get(), *mRoadMesh);
    }
    if (showBorders) {
        DrawMesh(pass.get(), *mChunkBorders);
    }
    if (!showSpheres && mGltfPipeline) {
        wgpuRenderPassEncoderSetPipeline(pass.get(), mGltfPipeline.get());
        DrawProps(pass.get(), scene.props, PropPass::GltfColor);
    }
    if (showVegetation) {
        wgpuRenderPassEncoderSetPipeline(pass.get(), mVegetationPipeline.get());
        wgpuRenderPassEncoderSetBindGroup(pass.get(), 0, mVegetationUniform.binding.get(), 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(pass.get(), 1, mShadowMap->SamplingBinding(), 0, nullptr);
        mVegetationStats.colorDraws = mVegetation->Draw(pass.get(), scene.vegetation->instanced, false);
    }
    if (mSky && scene.environment) {
        mSky->Draw(pass.get(), projection, view, scene.environment->sky, horizon, light.nightMix);
    }
    wgpuRenderPassEncoderEnd(pass.get());
    if (showWater) {
        mWater->Encode(encoder, target, mDepthView.get(), vp, eye, *scene.water, glm::vec3(horizon), air, lighting);
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 两个通道读取同一份顶点与物体变换，保证角色和车辆也能投影。
//————————————————————————————————————————————————————————————————————————————————
void WorldRenderer::DrawObjects(WGPURenderPassEncoder pass, std::size_t visibleCount, bool showSpheres) const {
    for (std::size_t i = 0; i < mTerrainDraws.size(); ++i) {
        DrawMesh(pass, mCoarseChoice[i] ? mCoarseTerrainDraws[i] : mTerrainDraws[i]);
    }
    for (std::size_t i = mTerrainDraws.empty() ? 0 : 1; i < visibleCount; ++i) {
        DrawMesh(pass, mDraws[i]);
    }
    if (showSpheres) {
        for (const auto& sphere : mMaterialSpheres) {
            DrawMesh(pass, sphere);
        }
    } else {
        if (!mTerrainDraws.empty() && visibleCount > WorldScene::kObjectCount) {
            for (const auto& limb : mRobotLimbs) {
                DrawMesh(pass, limb);
            }
        }
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 根据原型选择几何，深度通道只读顶点和实例变换。
//————————————————————————————————————————————————————————————————————————————————
void WorldRenderer::DrawProps(WGPURenderPassEncoder pass, const std::vector<PlacedProp>& props,
                              PropPass type) const {
    const std::array builtins{&mRock, &mDraws[1], &mDraws[2], &mCabin};
    for (std::size_t i = 0; i < props.size(); ++i) {
        const auto asset = props[i].asset;
        const auto instance = mPropDraws[i].binding.get();
        if (asset < kBuiltinPropCount) {
            if (type != PropPass::GltfColor) DrawMesh(pass, *builtins[asset], instance);
        } else if (type != PropPass::BuiltinColor) {
            const auto& prototype = mGltfProps[asset - kBuiltinPropCount];
            for (const auto& draw : prototype.meshes) {
                if (type == PropPass::GltfColor) {
                    wgpuRenderPassEncoderSetBindGroup(pass, 3, prototype.materials[draw.material].get(), 0, nullptr);
                }
                DrawMesh(pass, draw.mesh, instance);
            }
        }
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 提交与一个对象或地形块对应的三角形列表。
//————————————————————————————————————————————————————————————————————————————————
void WorldRenderer::DrawMesh(WGPURenderPassEncoder pass, const Drawable& mesh, WGPUBindGroup instance) {
    wgpuRenderPassEncoderSetBindGroup(pass, 0, instance ? instance : mesh.binding.get(), 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, mesh.vertices.get(), 0, mesh.count * sizeof(WorldVertex));
    wgpuRenderPassEncoderDraw(pass, mesh.count, 1, 0, 0);
}

}  // namespace tinyworld
