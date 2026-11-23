#pragma once

#include "Engine/Scene/SceneMesh.h"

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace tinyworld::primitive {

using Color = std::array<float, 3>;
using Mesh = std::vector<SceneVertex>;

//————————————————————————————————————————————————————————————————————————————————
// 由逆时针顶点求出朝外的面法线，并写入一个三角形或拆成两个三角形的四边形。
//————————————————————————————————————————————————————————————————————————————————
template <std::size_t N>
void AddFace(Mesh& mesh, const std::array<glm::vec3, N>& corners, Color color) {
    const glm::vec3 normal = glm::normalize(glm::cross(corners[1] - corners[0], corners[2] - corners[0]));
    const auto addVertex = [&](const glm::vec3& p) {
        mesh.push_back({{p.x, p.y, p.z}, {normal.x, normal.y, normal.z}, color});
    };
    for (std::size_t i = 1; i + 1 < N; ++i) {
        addVertex(corners[0]);
        addVertex(corners[i]);
        addVertex(corners[i + 1]);
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 加入一个带顶面收边的盒体。收边为零时生成普通长方体。
//————————————————————————————————————————————————————————————————————————————————
inline void AddBox(Mesh& mesh, glm::vec3 low, glm::vec3 high, Color color, glm::vec2 inset = {}) {
    const glm::vec3 a{low.x, low.y, low.z};
    const glm::vec3 b{high.x, low.y, low.z};
    const glm::vec3 c{high.x, low.y, high.z};
    const glm::vec3 d{low.x, low.y, high.z};
    const glm::vec3 e{low.x + inset.x, high.y, low.z + inset.y};
    const glm::vec3 f{high.x - inset.x, high.y, low.z + inset.y};
    const glm::vec3 g{high.x - inset.x, high.y, high.z - inset.y};
    const glm::vec3 h{low.x + inset.x, high.y, high.z - inset.y};
    AddFace(mesh, std::array{a, b, c, d}, color);
    AddFace(mesh, std::array{e, h, g, f}, color);
    AddFace(mesh, std::array{a, e, f, b}, color);
    AddFace(mesh, std::array{b, f, g, c}, color);
    AddFace(mesh, std::array{c, g, h, d}, color);
    AddFace(mesh, std::array{d, h, e, a}, color);
}

}  // namespace tinyworld::primitive
