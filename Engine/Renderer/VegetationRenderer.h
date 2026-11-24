#pragma once

#include "Engine/Renderer/WgpuHandle.h"
#include "Engine/Terrain/Vegetation.h"

#include <optional>

namespace tinyworld {

class WebGpuContext;

struct VegetationDrawStats {
    std::array<std::uint32_t, kVegetationKinds> instances{};
    std::uint32_t colorDraws{};
    std::uint32_t shadowDraws{};
};

struct VegetationVertex {
    SceneVertex geometry;
    glm::vec2 cardUv;
};

//————————————————————————————————————————————————————————————————————————————————
// 顶点流按顶点读取几何和 UV，实例流按实例读取位置、缩放与朝向。
//————————————————————————————————————————————————————————————————————————————————
struct VegetationVertexLayout {
    VegetationVertexLayout();
    std::array<WGPUVertexAttribute, 4> vertices;
    std::array<WGPUVertexAttribute, 2> instances;
    std::array<WGPUVertexBufferLayout, 2> buffers;
};

//————————————————————————————————————————————————————————————————————————————————
// 将各块按原型合并到实例缓冲。两种提交方式共用完全相同的数据。
//————————————————————————————————————————————————————————————————————————————————
class VegetationRenderer {
public:
    explicit VegetationRenderer(WebGpuContext& gpu);
    void Sync(const HeightField& terrain, const std::optional<RoadSpline>& road, const VegetationSettings& settings);
    std::size_t UploadChunk(std::size_t index, const VegetationPatch& patch);
    void EvictChunk(std::size_t index);
    [[nodiscard]] std::uint32_t DrawChunk(WGPURenderPassEncoder pass, std::size_t index,
                                           bool instanced, bool shadows) const;
    [[nodiscard]] std::uint32_t Draw(WGPURenderPassEncoder pass, bool instanced, bool shadows) const;
    [[nodiscard]] std::array<std::uint32_t, kVegetationKinds> Counts() const;

private:
    struct Prototype {
        BufferHandle mesh;
        std::uint32_t vertexCount{};
    };
    struct InstanceBuffer {
        BufferHandle buffer;
        std::uint32_t count{};
        std::size_t capacity{};
    };
    using Instances = std::array<InstanceBuffer, kVegetationKinds>;

    std::size_t Upload(Instances& buffers, const VegetationPatch& instances);
    [[nodiscard]] std::uint32_t DrawInstances(WGPURenderPassEncoder pass, const Instances& buffers,
                                               bool instanced, bool shadows) const;

    WebGpuContext& mGpu;
    std::array<Prototype, kVegetationKinds> mPrototypes;
    Instances mGlobal;                            // 早期章节的合批
    std::array<Instances, TerrainGrid::kMaxChunkCount> mChunks; // 第二十章按块释放实例流
    RoadMask mRoadMask;
    std::optional<RoadSpline> mRoad;
    std::optional<VegetationSettings> mSettings;
    std::uint64_t mTerrainRevision{};
};

}  // namespace tinyworld
