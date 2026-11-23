#include "Engine/Terrain/TerrainMesh.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>

namespace tinyworld {
namespace {

//————————————————————————————————————————————————————————————————————————————————
// 高度和法线都从全局样点表读取，共边顶点在相邻块中有相同的光照结果。
//————————————————————————————————————————————————————————————————————————————————
SceneVertex Vertex(const HeightField& field, int ix, int iz, bool paintTrail) {
    constexpr float span = 2.0f * HeightField::kSpacing;
    const float x = ix * HeightField::kSpacing;
    const float z = iz * HeightField::kSpacing;
    const float y = field.Sample(ix, iz);
    const auto n = glm::normalize(glm::vec3{field.Sample(ix - 1, iz) - field.Sample(ix + 1, iz), span,
                                           field.Sample(ix, iz - 1) - field.Sample(ix, iz + 1)});
    // 顶点颜色给步行提供参照。地表材质留到后面的 PBR 章节。
    const float fleck = 0.5f + 0.5f * std::sin(1.65f * x + 1.11f * z) * std::cos(1.39f * z - 0.53f * x);
    const float highland = std::clamp((y + 1.0f) * 0.23f + 0.12f * fleck, 0.0f, 1.0f);
    const glm::vec3 grass = glm::mix(glm::vec3{0.12f, 0.27f, 0.071f}, glm::vec3{0.26f, 0.41f, 0.12f}, highland);
    const glm::vec3 trail = glm::vec3{0.48f, 0.33f, 0.159f} * (0.91f + 0.16f * fleck);
    const float shoulder = std::abs(x - TerrainTrailX(z));
    const float coverage = paintTrail ? 1.0f - glm::smoothstep(0.61f, 1.18f, shoulder) : 0.0f;
    const auto color = glm::mix(grass, trail, coverage);
    return {{x, y, z}, {n.x, n.y, n.z}, {color.x, color.y, color.z}};
}

//————————————————————————————————————————————————————————————————————————————————
// 调试窄带使用与角色相同的高度查询，并略抬高以避免贴面闪烁。
//————————————————————————————————————————————————————————————————————————————————
SceneVertex BorderVertex(const HeightField& field, glm::vec2 xz) {
    return {{xz.x, field.HeightAt(xz) + 0.045f, xz.y}, {0.0f, 1.0f, 0.0f}, {0.93f, 0.64f, 0.19f}};
}

}  // namespace

//————————————————————————————————————————————————————————————————————————————————
// 每格用 B-C 作为对角线，三角形从上方观察呈逆时针。
//————————————————————————————————————————————————————————————————————————————————
std::vector<SceneVertex> MakeTerrainChunk(const HeightField& field, glm::ivec2 key, bool paintTrail, bool coarse) {
    constexpr int side = HeightField::kCellsPerChunk;
    const glm::ivec2 origin = key * side;
    std::array<SceneVertex, (side + 1) * (side + 1)> samples;
    for (int z = 0; z <= side; ++z) {
        for (int x = 0; x <= side; ++x) {
            samples[z * (side + 1) + x] = Vertex(field, origin.x + x, origin.y + z, paintTrail);
        }
    }
    std::vector<SceneVertex> mesh;
    mesh.reserve(side * side * 6);
    const int step = coarse ? 2 : 1;
    for (int z = 0; z < side; z += step) {
        for (int x = 0; x < side; x += step) {
            const int i = z * (side + 1) + x;
            const auto& a = samples[i];
            const auto& b = samples[i + step];
            const auto& c = samples[i + step * (side + 1)];
            const auto& d = samples[i + step * (side + 2)];
            if (!coarse || (x != 0 && z != 0 && x + step != side && z + step != side)) {
                mesh.insert(mesh.end(), {a, c, b, b, c, d});
                continue;
            }
            // 粗格只在块外围插入半米中点，以中心扇形与所有邻块共边。
            const auto& middle = samples[i + side + 2];
            const SceneVertex* previous = &a;
            const auto emit = [&](const SceneVertex& next) {
                mesh.insert(mesh.end(), {middle, *previous, next});
                previous = &next;
            };
            if (x == 0) emit(samples[i + side + 1]);
            emit(c);
            if (z + step == side) emit(samples[i + 2 * (side + 1) + 1]);
            emit(d);
            if (x + step == side) emit(samples[i + side + 3]);
            emit(b);
            if (z == 0) emit(samples[i + 1]);
            emit(a);
        }
    }
    return mesh;
}

//————————————————————————————————————————————————————————————————————————————————
// 使用水平距离，抬高观察镜头不会单独切换所在块的细分等级。
//————————————————————————————————————————————————————————————————————————————————
bool IsCoarseTerrainChunk(glm::ivec2 key, glm::vec3 eye) {
    const auto center = (glm::vec2(key) + 0.5f) * HeightField::kChunkSize;
    const auto offset = center - glm::vec2{eye.x, eye.z};
    return glm::dot(offset, offset) > 14.0f * 14.0f;
}

//————————————————————————————————————————————————————————————————————————————————
// 每条内部边界只生成一次，按半米分段以贴合高度场。
//————————————————————————————————————————————————————————————————————————————————
std::vector<SceneVertex> MakeChunkBorders(const HeightField& field) {
    std::vector<SceneVertex> mesh;
    constexpr float halfWidth = 0.045f;
    for (int key = HeightField::kFirstChunk + 1; key < HeightField::kEndChunk; ++key) {
        const float edge = key * HeightField::kChunkSize;
        for (int i = HeightField::kFirstSample; i < HeightField::kLastSample; ++i) {
            const float low = i * HeightField::kSpacing;
            const float high = low + HeightField::kSpacing;
            const auto a = BorderVertex(field, {edge - halfWidth, low});
            const auto b = BorderVertex(field, {edge + halfWidth, low});
            const auto c = BorderVertex(field, {edge - halfWidth, high});
            const auto d = BorderVertex(field, {edge + halfWidth, high});
            const auto e = BorderVertex(field, {low, edge - halfWidth});
            const auto f = BorderVertex(field, {high, edge - halfWidth});
            const auto g = BorderVertex(field, {low, edge + halfWidth});
            const auto h = BorderVertex(field, {high, edge + halfWidth});
            mesh.insert(mesh.end(), {a, c, b, b, c, d, e, g, f, f, g, h});
        }
    }
    return mesh;
}

}  // namespace tinyworld
