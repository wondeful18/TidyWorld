#pragma once

#include "Engine/Scene/SceneMesh.h"

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 生成漫游小车的低模三角形列表。轮胎触地于 Y = 0，车头朝向 +Z。
//————————————————————————————————————————————————————————————————————————————————
[[nodiscard]] std::vector<SceneVertex> MakeRoamingCarMesh();

}  // namespace tinyworld
