#include "Engine/Renderer/VegetationRenderer.h"

#include "Engine/Renderer/WebGpuContext.h"
#include "Engine/Scene/PlacedProp.h"
#include "Engine/Scene/PrimitiveMesh.h"

#include <glm/gtc/constants.hpp>

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace tinyworld {
namespace {

//————————————————————————————————————————————————————————————————————————————————
// 用三张相交的卡片从不同角度保留草丛轮廓，片元着色器再裁出叶片。
//————————————————————————————————————————————————————————————————————————————————
std::vector<VegetationVertex> MakeGrassCards() {
    constexpr float halfWidth = 0.35f;
    constexpr float height = 0.76f;
    constexpr std::array indices{0, 1, 2, 2, 1, 3};
    constexpr std::array<glm::vec2, 4> uv{{{0, 0}, {1, 0}, {0, 1}, {1, 1}}};
    std::vector<VegetationVertex> mesh;
    for (int card = 0; card < 3; ++card) {
        const float angle = card * glm::pi<float>() / 3.0f;
        for (const int corner : indices) {
            const auto p = uv[corner];
            const float across = (p.x * 2.0f - 1.0f) * halfWidth;
            mesh.push_back({{{std::cos(angle) * across, height * p.y - 0.04f, std::sin(angle) * across},
                             {0.0f, 1.0f, 0.0f}, {0.10f + 0.18f * p.y, 0.22f + 0.22f * p.y, 0.044f}},
                            p});
        }
    }
    return mesh;
}

//————————————————————————————————————————————————————————————————————————————————
// 用多层锥体表现林缘针叶树，高度控制在原型的局部坐标中。
//————————————————————————————————————————————————————————————————————————————————
std::vector<SceneVertex> MakePine() {
    primitive::Mesh mesh;
    primitive::AddBox(mesh, {-0.12f, -0.13f, -0.12f}, {0.12f, 2.7f, 0.12f}, {0.21f, 0.12f, 0.065f});
    constexpr int sides = 8;
    constexpr std::array base{0.92f, 1.71f, 2.43f};
    constexpr std::array tip{2.55f, 3.18f, 3.92f};
    constexpr std::array radius{1.09f, 0.86f, 0.61f};
    for (std::size_t tier = 0; tier < base.size(); ++tier) {
        const auto rim = [&](int i) {
            const float a = glm::two_pi<float>() * i / sides + tier * 0.28f;
            return glm::vec3{std::cos(a) * radius[tier], base[tier], std::sin(a) * radius[tier]};
        };
        for (int side = 0; side < sides; ++side) {
            const auto a = rim(side), b = rim(side + 1);
            const float tint = side % 2 == 0 ? 1.0f : 0.82f;
            primitive::AddFace(mesh, std::array{a, glm::vec3{0, tip[tier], 0}, b},
                               {0.047f * tint, (0.15f + 0.031f * tier) * tint, 0.072f * tint});
            primitive::AddFace(mesh, std::array{a, b, glm::vec3{0, base[tier], 0}}, {0.033f, 0.075f, 0.042f});
        }
    }
    return mesh;
}

//————————————————————————————————————————————————————————————————————————————————
// 实心模型使用负 UV，让片元保留整张三角面。
//————————————————————————————————————————————————————————————————————————————————
std::vector<VegetationVertex> Solid(std::vector<SceneVertex> mesh) {
    std::vector<VegetationVertex> vertices;
    vertices.reserve(mesh.size());
    for (const auto& vertex : mesh) vertices.push_back({vertex, {-1.0f, 0.0f}});
    return vertices;
}

//————————————————————————————————————————————————————————————————————————————————
// 网格与实例数据使用可写入的顶点缓冲，生命周期覆盖两个绘制通道。
//————————————————————————————————————————————————————————————————————————————————
BufferHandle MakeVertexBuffer(WGPUDevice device, std::size_t bytes, const char* label) {
    WGPUBufferDescriptor description = WGPU_BUFFER_DESCRIPTOR_INIT;
    description.label = {label, WGPU_STRLEN};
    description.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    description.size = bytes;
    BufferHandle buffer(wgpuDeviceCreateBuffer(device, &description));
    if (!buffer) throw std::runtime_error("Cannot allocate vegetation buffer.");
    return buffer;
}

}  // namespace

//————————————————————————————————————————————————————————————————————————————————
// 显式描述两条顶点流，颜色与太阳深度管线共用这一布局。
//————————————————————————————————————————————————————————————————————————————————
VegetationVertexLayout::VegetationVertexLayout() {
    constexpr auto geometry = offsetof(VegetationVertex, geometry);
    constexpr std::array offsets{geometry + offsetof(SceneVertex, position),
        geometry + offsetof(SceneVertex, normal), geometry + offsetof(SceneVertex, color),
        offsetof(VegetationVertex, cardUv)};
    for (std::uint32_t i = 0; i < vertices.size(); ++i) {
        vertices[i] = WGPU_VERTEX_ATTRIBUTE_INIT;
        vertices[i].shaderLocation = i;
        vertices[i].format = i == 3 ? WGPUVertexFormat_Float32x2 : WGPUVertexFormat_Float32x3;
        vertices[i].offset = offsets[i];
    }
    for (std::uint32_t i = 0; i < instances.size(); ++i) {
        instances[i] = WGPU_VERTEX_ATTRIBUTE_INIT;
        instances[i].shaderLocation = i + 4;
        instances[i].format = WGPUVertexFormat_Float32x4;
        instances[i].offset = i == 0 ? offsetof(VegetationInstance, positionScale) : offsetof(VegetationInstance, facingShade);
    }
    buffers[0] = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
    buffers[1] = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
    buffers[0].arrayStride = sizeof(VegetationVertex);
    buffers[0].stepMode = WGPUVertexStepMode_Vertex;
    buffers[0].attributeCount = vertices.size();
    buffers[0].attributes = vertices.data();
    buffers[1].arrayStride = sizeof(VegetationInstance);
    buffers[1].stepMode = WGPUVertexStepMode_Instance;
    buffers[1].attributeCount = instances.size();
    buffers[1].attributes = instances.data();
}

//————————————————————————————————————————————————————————————————————————————————
// 原型网格只在构造时上传，换种子时只更新实例流。
//————————————————————————————————————————————————————————————————————————————————
VegetationRenderer::VegetationRenderer(WebGpuContext& gpu) : mGpu(gpu) {
    const std::array geometry{MakeGrassCards(), Solid(MakePine()), Solid(MakeRockMesh())};
    for (std::size_t i = 0; i < mPrototypes.size(); ++i) {
        auto& batch = mPrototypes[i];
        const auto bytes = geometry[i].size() * sizeof(VegetationVertex);
        batch.vertexCount = static_cast<std::uint32_t>(geometry[i].size());
        batch.mesh = MakeVertexBuffer(gpu.Device(), bytes, "Vegetation mesh");
        wgpuQueueWriteBuffer(gpu.Queue(), batch.mesh.get(), 0, geometry[i].data(), bytes);
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 源数据不变时保留上次的批次，切换逐个提交不会重建或重传。
//————————————————————————————————————————————————————————————————————————————————
void VegetationRenderer::Sync(const HeightField& terrain, const std::optional<RoadSpline>& road,
                               const VegetationSettings& settings) {
    const bool roadChanged = road != mRoad;
    if (mSettings && settings.mode == mSettings->mode && settings.seed == mSettings->seed &&
        settings.density == mSettings->density && settings.minPlantHeight == mSettings->minPlantHeight &&
        terrain.Revision() == mTerrainRevision && !roadChanged) return;
    if (roadChanged) mRoadMask = road ? MakeRoadMask(*road) : RoadMask{};
    VegetationPatch instances;
    for (int z = HeightField::kFirstChunk; z < HeightField::kEndChunk; ++z) {
        for (int x = HeightField::kFirstChunk; x < HeightField::kEndChunk; ++x) {
            const auto chunk = GenerateVegetationChunk(terrain, mRoadMask, {x, z}, settings);
            for (std::size_t i = 0; i < instances.size(); ++i) {
                instances[i].insert(instances[i].end(), chunk[i].begin(), chunk[i].end());
            }
        }
    }
    Upload(mGlobal, instances);
    mTerrainRevision = terrain.Revision();
    mRoad = road;
    mSettings = settings;
}

//————————————————————————————————————————————————————————————————————————————————
// 共用实例上传。全局批保留容量，块加载用新的临时批，上传完成后才替换。
//————————————————————————————————————————————————————————————————————————————————
std::size_t VegetationRenderer::Upload(Instances& buffers, const VegetationPatch& instances) {
    std::size_t total{};
    for (std::size_t i = 0; i < buffers.size(); ++i) {
        auto& batch = buffers[i];
        const auto bytes = instances[i].size() * sizeof(VegetationInstance);
        if (bytes > batch.capacity) {
            batch.buffer = MakeVertexBuffer(mGpu.Device(), bytes, "Vegetation instances");
            batch.capacity = bytes;
        }
        batch.count = static_cast<std::uint32_t>(instances[i].size());
        if (bytes != 0) wgpuQueueWriteBuffer(mGpu.Queue(), batch.buffer.get(), 0, instances[i].data(), bytes);
        total += bytes;
    }
    return total;
}

//————————————————————————————————————————————————————————————————————————————————
// 按键替换或释放实例流。所有块仍共享三个原型网格。
//————————————————————————————————————————————————————————————————————————————————
std::size_t VegetationRenderer::UploadChunk(std::size_t index, const VegetationPatch& patch) {
    Instances replacement;
    const auto bytes = Upload(replacement, patch);
    mChunks[index] = std::move(replacement);
    return bytes;
}

void VegetationRenderer::EvictChunk(std::size_t index) {
    mChunks[index] = Instances();
}

std::uint32_t VegetationRenderer::DrawChunk(WGPURenderPassEncoder pass, std::size_t index,
                                             bool instanced, bool shadows) const {
    return DrawInstances(pass, mChunks[index], instanced, shadows);
}

//————————————————————————————————————————————————————————————————————————————————
// 比较时只改变提交次数和 firstInstance，两个模式消费等量的三角形。
//————————————————————————————————————————————————————————————————————————————————
std::uint32_t VegetationRenderer::Draw(WGPURenderPassEncoder pass, bool instanced, bool shadows) const {
    return DrawInstances(pass, mGlobal, instanced, shadows);
}

//————————————————————————————————————————————————————————————————————————————————
// 把共享原型和指定实例批连到两个顶点流。
//————————————————————————————————————————————————————————————————————————————————
std::uint32_t VegetationRenderer::DrawInstances(WGPURenderPassEncoder pass, const Instances& buffers,
                                                 bool instanced, bool shadows) const {
    std::uint32_t calls{};
    // 草接收树木与地形阴影，只有不透明的树和岩石进入太阳通道。
    for (std::size_t i = shadows ? 1 : 0; i < buffers.size(); ++i) {
        const auto& batch = buffers[i];
        const auto& prototype = mPrototypes[i];
        if (batch.count == 0) continue;
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, prototype.mesh.get(), 0,
                                             prototype.vertexCount * sizeof(VegetationVertex));
        wgpuRenderPassEncoderSetVertexBuffer(pass, 1, batch.buffer.get(), 0, batch.count * sizeof(VegetationInstance));
        if (instanced) {
            wgpuRenderPassEncoderDraw(pass, prototype.vertexCount, batch.count, 0, 0);
            ++calls;
        } else {
            for (std::uint32_t instance = 0; instance < batch.count; ++instance) {
                wgpuRenderPassEncoderDraw(pass, prototype.vertexCount, 1, 0, instance);
                ++calls;
            }
        }
    }
    return calls;
}

//————————————————————————————————————————————————————————————————————————————————
// 交给检查器显示已经上传的实例数量。
//————————————————————————————————————————————————————————————————————————————————
std::array<std::uint32_t, kVegetationKinds> VegetationRenderer::Counts() const {
    std::array<std::uint32_t, kVegetationKinds> counts{};
    for (std::size_t i = 0; i < counts.size(); ++i) {
        counts[i] = mGlobal[i].count;
        for (const auto& chunk : mChunks) counts[i] += chunk[i].count;
    }
    return counts;
}

}  // namespace tinyworld
