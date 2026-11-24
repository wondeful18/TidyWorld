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
    [[nodiscard]] std::uint32_t Draw(WGPURenderPassEncoder pass, bool instanced, bool shadows) const;
    [[nodiscard]] std::array<std::uint32_t, kVegetationKinds> Counts() const;

private:
    struct Batch {
        BufferHandle mesh;
        BufferHandle instances;
        std::uint32_t vertexCount{};
        std::uint32_t instanceCount{};
        std::size_t capacity{};
    };
    WebGpuContext& mGpu;
    std::array<Batch, kVegetationKinds> mBatches;
    RoadMask mRoadMask;
    std::optional<RoadSpline> mRoad;
    std::optional<VegetationSettings> mSettings;
    std::uint64_t mTerrainRevision{};
};

}  // namespace tinyworld
