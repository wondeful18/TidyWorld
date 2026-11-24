#include "Engine/Terrain/TerrainMesh.h"
#include "Engine/Terrain/TerrainVisibility.h"

#include <glm/trigonometric.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
using namespace tinyworld;

void Require(bool valid, const char* message) {
    if (!valid) throw std::runtime_error(message);
}

//————————————————————————————————————————————————————————————————————————————————
// 块边上的每段线必须成对匹配，包含位置、法线和颜色一致的半米中点。
//————————————————————————————————————————————————————————————————————————————————
void CheckEdge(const HeightField& field, glm::ivec2 a, glm::ivec2 b, int axis, float boundary) {
    using Vertex = std::array<float, 9>;
    using Edge = std::pair<Vertex, Vertex>;
    const auto edges = [&](const std::vector<SceneVertex>& mesh) {
        const auto attributes = [](const SceneVertex& v) {
            return Vertex{v.position[0], v.position[1], v.position[2],
                          v.normal[0], v.normal[1], v.normal[2], v.color[0], v.color[1], v.color[2]};
        };
        std::set<Edge> result;
        for (std::size_t i = 0; i < mesh.size(); i += 3) {
            for (std::size_t j = 0; j < 3; ++j) {
                const auto& one = mesh[i + j];
                const auto& two = mesh[i + (j + 1) % 3];
                if (one.position[axis] == boundary && two.position[axis] == boundary) {
                    const auto first = attributes(one);
                    const auto second = attributes(two);
                    const auto pair = std::minmax(first, second);
                    result.emplace(pair.first, pair.second);
                }
            }
        }
        return result;
    };
    const auto coarse = edges(MakeTerrainChunk(field, a, false, true));
    const auto fine = edges(MakeTerrainChunk(field, b, false, false));
    Require(coarse.size() == HeightField::kCellsPerChunk && coarse == fine, "Mixed-LOD edge does not match.");
}

//————————————————————————————————————————————————————————————————————————————————
// 用裁剪已知的盒子检验 W-Z 与 Z 两个边界，以及编辑后的包围盒。
//————————————————————————————————————————————————————————————————————————————————
void CheckFrustum() {
    const auto frustum = MakeTerrainFrustum(glm::perspectiveRH_ZO(glm::radians(90.0f), 1.0f, 1.0f, 9.0f));
    Require(IntersectsTerrainFrustum(frustum, {{-1, -1, -4}, {1, 1, -2}}), "Inside box rejected.");
    Require(!IntersectsTerrainFrustum(frustum, {{-1, -1, -0.6f}, {1, 1, -0.3f}}), "Near plane must use Z >= 0.");
    Require(!IntersectsTerrainFrustum(frustum, {{-1, -1, -12}, {1, 1, -10}}), "Far plane failed.");
    Require(!IntersectsTerrainFrustum(frustum, {{6, -1, -4}, {8, 1, -2}}), "Right plane failed.");
    Require(IntersectsTerrainFrustum(frustum, {{2, -0.2f, -4}, {6, 0.2f, -2}}), "Straddling box rejected.");
    HeightField field;
    const glm::ivec2 key{0, 0};
    const auto index = TerrainIndex(key);
    const auto revision = field.ChunkRevision(index);
    const auto old = MeasureTerrainBounds(field, key);
    Require(field.ApplyBrush({4, 4}, {BrushMode::Flatten, 2.0f, 5.0f, 11.0f}, 1.0f), "Brush did not edit.");
    const auto bounds = MeasureTerrainBounds(field, key);
    Require(bounds.high.y > old.high.y && field.ChunkRevision(index) > revision, "Edited bounds retained old revision.");
    CheckEdge(field, {0, 0}, {1, 0}, 0, 8.0f);
    CheckEdge(field, {0, 0}, {0, 1}, 2, 8.0f);
    CheckEdge(field, {0, 0}, {-1, 0}, 0, 0.0f);
    CheckEdge(field, {0, 0}, {0, -1}, 2, 0.0f);
}
}

int main() {
    try {
        CheckFrustum();
        for (std::size_t index = 0; index < HeightField::kChunkCount; ++index) {
            Require(TerrainIndex(TerrainKey(index)) == index, "Terrain key ordering changed.");
        }
        std::cout << "Terrain frustum, revisions and four mixed-LOD edges passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
