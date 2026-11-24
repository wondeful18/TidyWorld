#pragma once

#include "Engine/Terrain/HeightField.h"

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

namespace tinyworld {

struct TerrainPreview {
    bool lod{true};
    bool cull{true};
};

struct TerrainDrawStats {
    unsigned visible{};
    unsigned culled{};
    unsigned fine{};
    unsigned coarse{};
    unsigned colorTriangles{};
    unsigned shadowTriangles{};
};

struct TerrainBounds {
    glm::vec3 low;
    glm::vec3 high;
};

//————————————————————————————————————————————————————————————————————————————————
// 在渲染、统计和后台队列之间使用行优先的块索引。
//————————————————————————————————————————————————————————————————————————————————
[[nodiscard]] glm::ivec2 TerrainKey(std::size_t index);
[[nodiscard]] std::size_t TerrainIndex(glm::ivec2 key);

//————————————————————————————————————————————————————————————————————————————————
// 用所有原始样点求块的高度范围。粗细网格的顶点都在该范围内。
//————————————————————————————————————————————————————————————————————————————————
[[nodiscard]] TerrainBounds MeasureTerrainBounds(const HeightField& field, glm::ivec2 key);

//————————————————————————————————————————————————————————————————————————————————
// 从 WebGPU 的 0–1 深度投影提取世界空间半平面。法线朝向视锥内部。
//————————————————————————————————————————————————————————————————————————————————
using TerrainFrustum = std::array<glm::vec4, 6>;
[[nodiscard]] TerrainFrustum MakeTerrainFrustum(const glm::mat4& viewProjection);

//————————————————————————————————————————————————————————————————————————————————
// 若包围盒沿任一平面法线伸得最远的点还在外侧，整个块可安全丢弃。
//————————————————————————————————————————————————————————————————————————————————
[[nodiscard]] bool IntersectsTerrainFrustum(const TerrainFrustum& frustum, const TerrainBounds& bounds);

}  // namespace tinyworld
