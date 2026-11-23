#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

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
// 一份覆盖世界原点两侧的规则采样网格。所有块读取同一份高度数组。
//————————————————————————————————————————————————————————————————————————————————
class HeightField {
public:
    static constexpr float kSpacing = 0.5f;  // 两个整数样点之间的米数
    static constexpr int kCellsPerChunk = 16;  // 每个块每轴的单元格数
    static constexpr int kFirstChunk = -2;  // X、Z 方向最小块键
    static constexpr int kEndChunk = 2;  // X、Z 方向块键的上界，开区间
    static constexpr float kChunkSize = kCellsPerChunk * kSpacing;  // 块边长，单位为米
    static constexpr int kFirstSample = kFirstChunk * kCellsPerChunk;  // 首个全局样点，包含
    static constexpr int kLastSample = kEndChunk * kCellsPerChunk;  // 末端共边样点，包含
    static constexpr float kHalfExtent = kLastSample * kSpacing;  // 地形从 -16 m 延伸到 +16 m
    static constexpr float kWalkLimit = kHalfExtent - 0.35f;  // 给角色脚掌留出边缘余量
    static constexpr int kSampleCount = kLastSample - kFirstSample + 1;  // 含左右端点的每轴样点数
    static constexpr std::size_t kChunkCount = (kEndChunk - kFirstChunk) * (kEndChunk - kFirstChunk);  // 样区块数
    static constexpr float kHeightLimit = 12.0f;  // 高度编辑和文件读取共用的绝对上限，单位为米
    using Samples = std::array<float, static_cast<std::size_t>(kSampleCount * kSampleCount)>;

    //————————————————————————————————————————————————————————————————————————————————
    // 用第六章的缓坡与两侧丘陵填满全局采样网格。
    //————————————————————————————————————————————————————————————————————————————————
    HeightField();

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

    Samples mHeights{};  // 按 Z 行存储高度，单位为米
    std::uint64_t mRevision{};  // 当前高度数组的修订号
    std::array<std::uint64_t, kChunkCount> mChunkRevisions{};  // 受编辑影响的块使用新修订号
};

//————————————————————————————————————————————————————————————————————————————————
// 返回样区小径在指定世界 Z 处的中心 X，单位为米。
//————————————————————————————————————————————————————————————————————————————————
[[nodiscard]] float TerrainTrailX(float z);

}  // namespace tinyworld
