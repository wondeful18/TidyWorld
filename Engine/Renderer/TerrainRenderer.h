#pragma once

#include "Engine/Renderer/WorldDraw.h"
#include "Engine/Terrain/ChunkStreamer.h"

namespace tinyworld {

class SurfaceMaterials;
class VegetationRenderer;
struct WorldScene;

//————————————————————————————————————————————————————————————————————————————————
// 持有共享索引、地形块、地面材质绑定和道路。颜色与太阳深度使用同一批几何。
//————————————————————————————————————————————————————————————————————————————————

class TerrainRenderer {
public:
    TerrainRenderer(WebGpuContext& gpu, WGPUBindGroupLayout layout, const WorldScene& scene,
                    SurfaceMaterials* materials, VegetationRenderer* vegetation);

    void Sync(const WorldScene& scene);
    void Prepare(const WorldScene& scene, const glm::mat4& viewProjection, glm::vec3 eye,
                 const DrawUniformWriter& uniforms, const glm::vec4& shadows, bool showSpheres);
    unsigned Draw(WGPURenderPassEncoder pass, bool shadows, const TerrainFrustum* shadowFrustum = nullptr);
    unsigned DrawOverlays(WGPURenderPassEncoder pass) const;
    unsigned DrawVegetation(WGPURenderPassEncoder pass, bool instanced, bool shadows) const;

    [[nodiscard]] TerrainGrid Grid() const { return mTerrainGrid; }
    [[nodiscard]] bool ResidentAt(glm::vec3 position) const;
    [[nodiscard]] const auto& TileProps() const { return mTileProps; }
    [[nodiscard]] const TerrainDrawStats& DrawStatistics() const { return mTerrainStats; }
    [[nodiscard]] const TerrainStreamStats& StreamStatistics() const { return mStreamStats; }
    [[nodiscard]] bool StreamingSettled() const;
    void WaitForChunkData();

private:
    static std::vector<WorldVertex> LocalVertices(const std::vector<SceneVertex>& geometry,
                                                   TerrainGrid grid, glm::ivec2 key);
    void SyncTerrain(const HeightField& field, bool showBorders, bool paintTrail);
    void SyncTerrainBorders(const HeightField& field, bool showBorders);
    void BuildTerrainIndices(TerrainGrid grid);
    void SyncStreamedTerrain(const WorldScene& scene);
    void SyncRoad(const HeightField& field, const RoadSpline& road);

    WebGpuContext& mGpu;              // 向设备和队列请求资源
    WGPUBindGroupLayout mObjectLayout; // 借用世界物体的常量布局
    SurfaceMaterials* mMaterials;     // 产生块的权重纹理绑定
    VegetationRenderer* mVegetation;  // 同一需求窗口内上传与逐出实例
    bool mShowRoad{};                 // 本帧显示道路
    bool mShowBorders{};              // 本帧显示调试共边
    std::vector<Drawable> mTerrainDraws;                    // 每个整数块键对应一次绘制
    std::array<BufferHandle, 4> mTerrainIndices;           // 全图块共享的各档索引缓冲
    std::array<std::uint32_t, 4> mTerrainIndexCounts{};    // 每档的索引数量
    std::array<TerrainBounds, TerrainGrid::kMaxChunkCount> mTerrainBounds{}; // 各档网格共用的包围盒
    std::array<bool, TerrainGrid::kMaxChunkCount> mTerrainVisible{}; // 仅限制相机通道
    TerrainDrawStats mTerrainStats;
    TerrainGrid mTerrainGrid;                     // 驻留索引所属的布局
    TerrainStreamStats mStreamStats;
    std::array<std::size_t, TerrainGrid::kMaxChunkCount> mChunkBytes{}; // 地形顶点和植被实例的提交字节
    std::optional<ChunkStreamer> mStream;    // 后台构网与磁盘块队列
    std::shared_ptr<ChunkCache> mStreamCache; // 上一帧的磁盘源，变化时清空旧 GPU 驻留
    std::vector<BindGroupHandle> mTileMaterials; // GPU 居留块自己的 RGBA8 权重绑定
    std::vector<std::vector<PlacedProp>> mTileProps; // 与 GPU 居留同步的实例记录
    std::array<Drawable, TerrainGrid::kMaxChunkCount> mStreamRoadDraws;
    std::array<std::uint8_t, TerrainGrid::kMaxChunkCount> mLod{};  // 本帧两个通道共用的等级
    std::optional<Drawable> mChunkBorders;                  // 仅供颜色通道绘制的边界提示
    std::array<std::uint64_t, TerrainGrid::kMaxChunkCount> mChunkRevisions{};  // 缓存的每块高度修订号
    std::uint64_t mBorderRevision{};                        // 贴地边界使用的高度修订号
    std::optional<Drawable> mRoadMesh;                      // 路面三角形及绘制资源
    std::optional<RoadSpline> mLastRoad;                     // 最近一次生成几何时的控制点和宽度
    std::uint64_t mRoadTerrainRevision{};                    // 最近一次裁剪时的高度修订号
};

}  // namespace tinyworld
