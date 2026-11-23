#pragma once

#include "Engine/Scene/SceneMesh.h"

#include <cstddef>

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 记录小车三角形列表中的材质范围。区间端点为从 0 开始的顶点索引。
//————————————————————————————————————————————————————————————————————————————————
struct CarSurfaceRanges {
    std::size_t glassBegin{};    // 玻璃的第一个顶点
    std::size_t glassEnd{};      // 玻璃区间的排他结束位置
    std::size_t reflectiveEnd{}; // 车漆和玻璃区间的排他结束位置
};

//————————————————————————————————————————————————————————————————————————————————
// 生成漫游小车的低模三角形列表。轮胎触地于 Y = 0，车头朝向 +Z。
// surfaces 可接收车漆和玻璃的顶点范围，用于指定反射强度。
//————————————————————————————————————————————————————————————————————————————————
[[nodiscard]] std::vector<SceneVertex> MakeRoamingCarMesh(CarSurfaceRanges* surfaces = nullptr);

}  // namespace tinyworld
