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
    const float spacing = field.Grid().Spacing();
    const float span = 2.0f * spacing;
    const float x = ix * spacing;
    const float z = iz * spacing;
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
// 每个块只上传一份规则网格顶点，两档 LOD 均引用相同顶点。
//————————————————————————————————————————————————————————————————————————————————
std::vector<SceneVertex> MakeTerrainVertices(const HeightField& field, glm::ivec2 key, bool paintTrail) {
    const int side = field.Grid().Cells();
    const glm::ivec2 origin = key * side;
    std::vector<SceneVertex> vertices;
    vertices.reserve((side + 1) * (side + 1));
    for (int z = 0; z <= side; ++z) {
        for (int x = 0; x <= side; ++x) {
            vertices.push_back(Vertex(field, origin.x + x, origin.y + z, paintTrail));
        }
    }
    return vertices;
}

//————————————————————————————————————————————————————————————————————————————————
// 每格以 B-C 为对角线。粗档边缘保留全部共边样点，供相邻块拼接。
//————————————————————————————————————————————————————————————————————————————————
std::vector<std::uint16_t> MakeTerrainIndices(int side, int step) {
    const int stride = side + 1;
    std::vector<std::uint16_t> indices;
    for (int z = 0; z < side; z += step) {
        for (int x = 0; x < side; x += step) {
            const auto a = static_cast<std::uint16_t>(z * stride + x);
            const auto b = static_cast<std::uint16_t>(a + step);
            const auto c = static_cast<std::uint16_t>(a + step * stride);
            const auto d = static_cast<std::uint16_t>(c + step);
            if (step == 1 || (x != 0 && z != 0 && x + step != side && z + step != side)) {
                indices.insert(indices.end(), {a, c, b, b, c, d});
                continue;
            }
            const auto middle = static_cast<std::uint16_t>(a + (step / 2) * (stride + 1));
            auto previous = a;
            const auto emit = [&](int index) {
                const auto next = static_cast<std::uint16_t>(index);
                indices.insert(indices.end(), {middle, previous, next});
                previous = next;
            };
            if (x == 0) for (int j = 1; j < step; ++j) emit(a + j * stride);
            emit(c);
            if (z + step == side) for (int j = 1; j < step; ++j) emit(c + j);
            emit(d);
            if (x + step == side) for (int j = step - 1; j > 0; --j) emit(b + j * stride);
            emit(b);
            if (z == 0) for (int j = step - 1; j > 0; --j) emit(a + j);
            emit(a);
        }
    }
    return indices;
}

//————————————————————————————————————————————————————————————————————————————————
// 教学诊断需要展开三角形时，复用渲染器使用的同一张索引表。
//————————————————————————————————————————————————————————————————————————————————
std::vector<SceneVertex> MakeTerrainChunk(const HeightField& field, glm::ivec2 key, bool paintTrail, bool coarse) {
    const auto vertices = MakeTerrainVertices(field, key, paintTrail);
    const auto indices = MakeTerrainIndices(field.Grid().Cells(), coarse ? 2 : 1);
    std::vector<SceneVertex> mesh;
    mesh.reserve(indices.size());
    for (const auto index : indices) mesh.push_back(vertices[index]);
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
    const auto grid = field.Grid();
    for (int key = grid.FirstKey() + 1; key < grid.EndKey(); ++key) {
        const float edge = key * grid.ChunkSize();
        for (int i = grid.FirstSample(); i < grid.LastSample(); ++i) {
            const float low = i * grid.Spacing();
            const float high = low + grid.Spacing();
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
