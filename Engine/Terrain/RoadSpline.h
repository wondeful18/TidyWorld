#pragma once

#include "Engine/Scene/SceneMesh.h"
#include "Engine/Terrain/HeightField.h"

namespace tinyworld {

enum class RoadSurface { Gravel, Dirt, Paved };  // 同一套道路几何的三种地表
enum class RoadJunction { None, Tee, Cross };  // 中心控制点的支路形式

//————————————————————————————————————————————————————————————————————————————————
// 控制点的 XZ 是世界坐标，Y 是相对地面的高度偏移。
//————————————————————————————————————————————————————————————————————————————————
struct RoadSpline {
    static constexpr std::size_t kMinPoints = 2;   // 曲线至少需要起点和终点
    static constexpr std::size_t kMaxPoints = 24;  // 样区编辑点上限
    static constexpr float kPointLimit = HeightField::kHalfExtent - 0.5f;
    static constexpr float kHeightOffsetLimit = 6.0f;
    static constexpr float kMinimumWidth = 0.8f;   // 路面全宽的下限，米
    static constexpr float kMaximumWidth = 4.0f;   // 路面全宽的上限，米
    static constexpr float kMaximumMargin = 2.5f;  // 额外避让距离的上限，米
    std::vector<glm::vec3> points{{-0.70f, 0.0f, 22.8f}, {0.15f, 0.0f, 10.2f}, {1.65f, 0.0f, 5.2f},
                                  {0.50f, 0.0f, 0.50f}, {-1.30f, 0.0f, -4.6f},
                                  {-2.20f, 0.0f, -8.4f}, {-1.15f, 0.0f, -21.75f}};
    float width{1.95f};   // 路面全宽，米
    float margin{0.70f};  // 路面外每侧额外留给植被的避让距离，米
    RoadSurface surface{RoadSurface::Gravel};  // 路面着色方式
    RoadJunction junction{RoadJunction::None};  // 可选 T 字或十字
    std::size_t junctionPoint{3};  // 支路借用的主路控制点
    float branchLength{5.0f};  // 交点到支路端点的长度，米

    //————————————————————————————————————————————————————————————————————————————————
    // 比较可保存参数，供绘制器检测重新建网的时机。
    //————————————————————————————————————————————————————————————————————————————————
    bool operator==(const RoadSpline&) const = default;

    //————————————————————————————————————————————————————————————————————————————————
    // 构造通过控制点的 Catmull-Rom 折线采样，剔除重合样点。
    //————————————————————————————————————————————————————————————————————————————————
    [[nodiscard]] std::vector<glm::vec3> Centerline() const;

    //————————————————————————————————————————————————————————————————————————————————
    // 用主路控制点与近似切线生成共用同一节点的支路。
    //————————————————————————————————————————————————————————————————————————————————
    [[nodiscard]] std::vector<glm::vec3> Branch(float pointLimit = kPointLimit) const;
};

//————————————————————————————————————————————————————————————————————————————————
// 道路和路肩在整个高度场的二值覆盖，按 Z 行存储。
//————————————————————————————————————————————————————————————————————————————————
struct RoadMask {
    static constexpr float kSpacing = 0.25f;
    explicit RoadMask(TerrainGrid layout = TerrainGrid{})
        : grid(layout), pixels(Side() * Side()) {}
    [[nodiscard]] int Side() const { return static_cast<int>(2.0f * grid.HalfExtent() / kSpacing); }
    TerrainGrid grid;
    std::vector<std::uint8_t> pixels;  // 0 可种植，255 避让
    [[nodiscard]] bool Blocks(glm::vec2 xz, float radius = 0.0f) const;
};

inline constexpr float kRoadOffset = 0.028f;  // 路面在对应地形三角面上方的距离，米

//————————————————————————————————————————————————————————————————————————————————
// 采样中心线，展开带状三角面，并沿地形三角形边界裁切。
//————————————————————————————————————————————————————————————————————————————————
[[nodiscard]] std::vector<SceneVertex> MakeRoadMesh(const HeightField& field, const RoadSpline& road);

//————————————————————————————————————————————————————————————————————————————————
// 用每个像素中心到折线的最近距离生成避让遮罩。
//————————————————————————————————————————————————————————————————————————————————
[[nodiscard]] RoadMask MakeRoadMask(const RoadSpline& road, TerrainGrid grid = TerrainGrid{});

}  // namespace tinyworld
