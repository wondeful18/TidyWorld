#include "Engine/Terrain/TerrainVisibility.h"

#include <glm/geometric.hpp>

#include <algorithm>

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 索引增量首先沿 X，行进到末尾后沿 Z 换行。
//————————————————————————————————————————————————————————————————————————————————
glm::ivec2 TerrainKey(std::size_t index) {
    return TerrainGrid{}.Key(index);
}

//————————————————————————————————————————————————————————————————————————————————
// 调用方传入样区范围内的块键。
//————————————————————————————————————————————————————————————————————————————————
std::size_t TerrainIndex(glm::ivec2 key) {
    return TerrainGrid{}.Index(key);
}

//————————————————————————————————————————————————————————————————————————————————
// 同时扫描边缘样点，避免把轮廓或接缝排除在包围盒之外。
//————————————————————————————————————————————————————————————————————————————————
TerrainBounds MeasureTerrainBounds(const HeightField& field, glm::ivec2 key) {
    const auto start = key * HeightField::kCellsPerChunk;
    const auto end = start + HeightField::kCellsPerChunk;
    float low = field.Sample(start.x, start.y);
    float high = low;
    for (int z = start.y; z <= end.y; ++z) {
        for (int x = start.x; x <= end.x; ++x) {
            const float y = field.Sample(x, z);
            low = std::min(low, y);
            high = std::max(high, y);
        }
    }
    return {{start.x * HeightField::kSpacing, low, start.y * HeightField::kSpacing},
            {end.x * HeightField::kSpacing, high, end.y * HeightField::kSpacing}};
}

//————————————————————————————————————————————————————————————————————————————————
// GLM 按列存取。先显式取行，再把裁剪不等式写成六组平面系数。
//————————————————————————————————————————————————————————————————————————————————
TerrainFrustum MakeTerrainFrustum(const glm::mat4& vp) {
    const auto row = [&](int i) { return glm::vec4(vp[0][i], vp[1][i], vp[2][i], vp[3][i]); };
    const auto w = row(3);
    return {w + row(0), w - row(0), w + row(1), w - row(1), row(2), w - row(2)};
}

//————————————————————————————————————————————————————————————————————————————————
// 直接比较未归一化平面上的符号，给浮点舍入留少量余地。
//————————————————————————————————————————————————————————————————————————————————
bool IntersectsTerrainFrustum(const TerrainFrustum& frustum, const TerrainBounds& bounds) {
    constexpr float tolerance = 0.001f;
    for (const auto p : frustum) {
        const glm::vec3 farthest{p.x >= 0.0f ? bounds.high.x : bounds.low.x,
                                p.y >= 0.0f ? bounds.high.y : bounds.low.y,
                                p.z >= 0.0f ? bounds.high.z : bounds.low.z};
        if (glm::dot(glm::vec3(p), farthest) + p.w < -tolerance) return false;
    }
    return true;
}

}  // namespace tinyworld
