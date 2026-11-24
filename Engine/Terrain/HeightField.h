#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace tinyworld {

enum class BrushMode { Raise, Lower, Smooth, Flatten };  // 地形样点的编辑方式

//————————————————————————————————————————————————————————————————————————————————
// 圆形笔刷参数，距离用米计，强度控制每秒变化量。
//————————————————————————————————————————————————————————————————————————————————
struct TerrainBrush {
    BrushMode mode{BrushMode::Raise};  // 当前编辑方式
    float radius{2.8f};               // XZ 平面上的作用半径
    float strength{1.5f};             // 抬降速度，或平滑、压平每秒的混合速率
    float targetHeight{};             // 压平所用的世界 Y，单位为米
};

//————————————————————————————————————————————————————————————————————————————————
// 固定采样间距下的正方形块布局。尺寸档位决定有效键、索引和世界边界。
//————————————————————————————————————————————————————————————————————————————————
class TerrainGrid {
public:
    static constexpr int kCells = 16;
    static constexpr float kSpacing = 0.5f;
    static constexpr float kChunkSize = kCells * kSpacing;
    static constexpr int kSmallRadius = 3;
    static constexpr int kLargeRadius = 5;
    static constexpr std::size_t kMaxChunkCount = 4 * kLargeRadius * kLargeRadius;

    explicit constexpr TerrainGrid(bool large = false) : mRadius(large ? kLargeRadius : kSmallRadius) {}
    constexpr bool operator==(const TerrainGrid&) const = default;
    [[nodiscard]] constexpr int FirstKey() const { return -mRadius; }
    [[nodiscard]] constexpr int EndKey() const { return mRadius; }
    [[nodiscard]] constexpr int Side() const { return 2 * mRadius; }
    [[nodiscard]] constexpr int FirstSample() const { return FirstKey() * kCells; }
    [[nodiscard]] constexpr int LastSample() const { return EndKey() * kCells; }
    [[nodiscard]] constexpr int SampleSide() const { return LastSample() - FirstSample() + 1; }
    [[nodiscard]] constexpr float HalfExtent() const { return mRadius * kChunkSize; }
    [[nodiscard]] constexpr float WalkLimit() const { return HalfExtent() - 0.35f; }
    [[nodiscard]] constexpr std::size_t ChunkCount() const { return Side() * Side(); }
    [[nodiscard]] std::size_t SampleIndex(int x, int z) const;
    [[nodiscard]] glm::ivec2 KeyAt(glm::vec2 xz) const;
    [[nodiscard]] glm::ivec2 Key(std::size_t index) const;
    [[nodiscard]] std::size_t Index(glm::ivec2 key) const;

private:
    int mRadius;
};

//————————————————————————————————————————————————————————————————————————————————
// 一份覆盖世界原点两侧的规则采样网格。所有块读取同一份高度数组。
//————————————————————————————————————————————————————————————————————————————————
class HeightField {
public:
    static constexpr float kSpacing = TerrainGrid::kSpacing;  // 两个整数样点之间的米数
    static constexpr int kCellsPerChunk = TerrainGrid::kCells;  // 每个块每轴的单元格数
    static constexpr int kFirstChunk = -TerrainGrid::kSmallRadius;  // X、Z 方向最小块键
    static constexpr int kEndChunk = TerrainGrid::kSmallRadius;  // X、Z 方向块键的上界，开区间
    static constexpr float kChunkSize = kCellsPerChunk * kSpacing;  // 块边长，单位为米
    static constexpr int kFirstSample = kFirstChunk * kCellsPerChunk;  // 首个全局样点，包含
    static constexpr int kLastSample = kEndChunk * kCellsPerChunk;  // 末端共边样点，包含
    static constexpr float kHalfExtent = kLastSample * kSpacing;  // 地形从 -24 m 延伸到 +24 m
    static constexpr float kWalkLimit = kHalfExtent - 0.35f;  // 给角色脚掌留出边缘余量
    static constexpr int kSampleCount = kLastSample - kFirstSample + 1;  // 含左右端点的每轴样点数
    static constexpr std::size_t kChunkCount = (kEndChunk - kFirstChunk) * (kEndChunk - kFirstChunk);  // 样区块数
    static constexpr float kHeightLimit = 12.0f;  // 高度编辑和文件读取共用的绝对上限，单位为米
    using Samples = std::vector<float>;

    //————————————————————————————————————————————————————————————————————————————————
    // 用第六章的缓坡与两侧丘陵填满全局采样网格。
    //————————————————————————————————————————————————————————————————————————————————
    explicit HeightField(TerrainGrid grid = TerrainGrid{});
    [[nodiscard]] TerrainGrid Grid() const { return mGrid; }

    //————————————————————————————————————————————————————————————————————————————————
    // 查整数样点。外边界之外取最近的边缘样点，供法线差分使用。
    //————————————————————————————————————————————————————————————————————————————————
    [[nodiscard]] float Sample(int ix, int iz) const;

    //————————————————————————————————————————————————————————————————————————————————
    // 在世界 XZ 处求与已绘制三角形一致的表面高度。外部坐标钳到边界。
    //————————————————————————————————————————————————————————————————————————————————
    [[nodiscard]] float HeightAt(glm::vec2 xz) const;

    //————————————————————————————————————————————————————————————————————————————————
    // 返回包含该世界位置的整数块键，右侧外边界归入最后一块。
    //————————————————————————————————————————————————————————————————————————————————
    [[nodiscard]] static glm::ivec2 ChunkAt(glm::vec2 xz);

    //————————————————————————————————————————————————————————————————————————————————
    // 编辑圆内样点并标记受位置或法线变化影响的块。
    //————————————————————————————————————————————————————————————————————————————————
    bool ApplyBrush(glm::vec2 center, const TerrainBrush& brush, float seconds);

    //————————————————————————————————————————————————————————————————————————————————
    // 沿归一化世界射线找最近的地面命中。无交点时保留 hit 的原值。
    //————————————————————————————————————————————————————————————————————————————————
    bool Raycast(glm::vec3 origin, glm::vec3 direction, glm::vec3& hit) const;

    //————————————————————————————————————————————————————————————————————————————————
    // 返回持久化使用的 Z 行优先样点表。
    //————————————————————————————————————————————————————————————————————————————————
    [[nodiscard]] const Samples& Values() const { return mHeights; }

    //————————————————————————————————————————————————————————————————————————————————
    // 校验并替换全部高度，刷新所有块的修订号。
    //————————————————————————————————————————————————————————————————————————————————
    void ReplaceSamples(const Samples& samples);

    //————————————————————————————————————————————————————————————————————————————————
    // 返回网格刷新使用的修订号。
    //————————————————————————————————————————————————————————————————————————————————
    [[nodiscard]] std::uint64_t Revision() const { return mRevision; }

    //————————————————————————————————————————————————————————————————————————————————
    // 用按 Z、X 遍历时的块序号查询修订号。
    //————————————————————————————————————————————————————————————————————————————————
    [[nodiscard]] std::uint64_t ChunkRevision(std::size_t index) const { return mChunkRevisions[index]; }

private:
    //————————————————————————————————————————————————————————————————————————————————
    // 将编辑范围向外扩一格，覆盖引用该样点做差分的邻居。
    //————————————————————————————————————————————————————————————————————————————————
    void Invalidate(glm::ivec2 low, glm::ivec2 high);

    TerrainGrid mGrid;  // 与样点表、修订号和块键映射共用的布局
    Samples mHeights;  // 按 Z 行存储高度，单位为米
    std::uint64_t mRevision{};  // 当前高度数组的修订号
    std::vector<std::uint64_t> mChunkRevisions;  // 受编辑影响的块使用新修订号
};

//————————————————————————————————————————————————————————————————————————————————
// 返回样区小径在指定世界 Z 处的中心 X，单位为米。
//————————————————————————————————————————————————————————————————————————————————
[[nodiscard]] float TerrainTrailX(float z);

}  // namespace tinyworld
