#pragma once

#include "Engine/Terrain/RoadSpline.h"

#include <glm/vec4.hpp>

#include <optional>

namespace tinyworld {

enum class VegetationKind { Grass, Tree, Rock, Count };
enum class VegetationMode { Meadow, TerrainRules };

//————————————————————————————————————————————————————————————————————————————————
// 第 11 章复用固定样地。第 12 章保存种子和密度，由场景重建实例。
//————————————————————————————————————————————————————————————————————————————————
struct VegetationSettings {
    static constexpr std::uint32_t kDefaultSeed = 21847;
    VegetationMode mode{VegetationMode::Meadow};
    std::uint32_t seed{kDefaultSeed};
    float density{0.75f};
    bool visible{true};
    bool instanced{true};
    std::optional<float> minPlantHeight{};  // 草和树的最低落点 Y，单位为米
};

//————————————————————————————————————————————————————————————————————————————————
// 第二条顶点流每个实例读取一条记录。余弦与正弦避免每顶点重复求三角函数。
//————————————————————————————————————————————————————————————————————————————————
struct VegetationInstance {
    glm::vec4 positionScale;   // 世界 XYZ、统一缩放
    glm::vec4 facingShade;     // 朝向的 cos、sin、亮度与保留位
    bool operator==(const VegetationInstance&) const = default;
};

inline constexpr auto kVegetationKinds = static_cast<std::size_t>(VegetationKind::Count);
using VegetationPatch = std::array<std::vector<VegetationInstance>, kVegetationKinds>;

//————————————————————————————————————————————————————————————————————————————————
// 生成一个块所属的实例。道路遮罩和地形均采用世界坐标。
//————————————————————————————————————————————————————————————————————————————————
[[nodiscard]] VegetationPatch GenerateVegetationChunk(const HeightField& field, const RoadMask& road,
                                                       glm::ivec2 chunk, const VegetationSettings& settings);

}  // namespace tinyworld
