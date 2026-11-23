#pragma once

#include "Engine/Scene/SceneMesh.h"
#include "Engine/Terrain/HeightField.h"

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 从整数块键生成一块三角形列表，顶点使用世界空间坐标。
//————————————————————————————————————————————————————————————————————————————————
[[nodiscard]] std::vector<SceneVertex> MakeTerrainChunk(const HeightField& field, glm::ivec2 key,
                                                        bool paintTrail = true, bool coarse = false);

//————————————————————————————————————————————————————————————————————————————————
// 第六章按相机到块中心的 XZ 距离选择 0 或 1 档。
//————————————————————————————————————————————————————————————————————————————————
[[nodiscard]] bool IsCoarseTerrainChunk(glm::ivec2 key, glm::vec3 eye);

//————————————————————————————————————————————————————————————————————————————————
// 将共边画成贴地的窄带，供颜色通道显示块边界。
//————————————————————————————————————————————————————————————————————————————————
[[nodiscard]] std::vector<SceneVertex> MakeChunkBorders(const HeightField& field);

}  // namespace tinyworld
