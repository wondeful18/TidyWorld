#include "Engine/Scene/RoamingCarMesh.h"

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cmath>

namespace tinyworld {
namespace {

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
void AddBox(Mesh& mesh, glm::vec3 low, glm::vec3 high, Color color, glm::vec2 inset = {}) {
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

//————————————————————————————————————————————————————————————————————————————————
// 以模型空间的 X 轴为轮轴生成轮胎，侧壁用平面三角形封口。
//————————————————————————————————————————————————————————————————————————————————
void AddWheel(Mesh& mesh, float x, float z) {
    constexpr int segments = 12;
    constexpr float radius = 0.15f;
    constexpr float halfWidth = 0.053f;
    constexpr Color rubber{0.035f, 0.042f, 0.05f};
    constexpr Color sidewall{0.075f, 0.085f, 0.098f};
    const glm::vec3 left{x - halfWidth, radius, z};
    const glm::vec3 right{x + halfWidth, radius, z};
    glm::vec3 current{0.0f, radius, 0.0f};
    for (int i = 1; i <= segments; ++i) {
        const float angle = glm::two_pi<float>() * i / segments;
        const glm::vec3 next{0.0f, radius * std::cos(angle), radius * std::sin(angle)};
        AddFace(mesh, std::array{left + current, left + next, right + next, right + current}, rubber);
        AddFace(mesh, std::array{left, left + next, left + current}, sidewall);
        AddFace(mesh, std::array{right, right + current, right + next}, sidewall);
        current = next;
    }
}

}  // namespace

//————————————————————————————————————————————————————————————————————————————————
// 组合车身、玻璃、车顶、车灯和轮胎，供场景管线一次上传和绘制。
//————————————————————————————————————————————————————————————————————————————————
std::vector<SceneVertex> MakeRoamingCarMesh() {
    Mesh mesh;
    constexpr Color paint{0.79f, 0.20f, 0.045f};
    constexpr Color glass{0.078f, 0.19f, 0.25f};
    constexpr Color trim{0.035f, 0.052f, 0.061f};
    AddBox(mesh, {-0.29f, 0.15f, -0.54f}, {0.29f, 0.34f, 0.54f}, paint, {0.018f, 0.04f});
    AddBox(mesh, {-0.23f, 0.32f, -0.39f}, {0.23f, 0.53f, 0.17f}, glass, {0.04f, 0.10f});
    AddBox(mesh, {-0.197f, 0.52f, -0.297f}, {0.197f, 0.56f, 0.077f}, paint, {0.016f, 0.022f});
    AddBox(mesh, {-0.27f, 0.14f, 0.49f}, {0.27f, 0.20f, 0.56f}, trim);
    AddBox(mesh, {-0.27f, 0.14f, -0.56f}, {0.27f, 0.20f, -0.49f}, trim);
    for (const float x : {-0.205f, 0.205f}) {
        AddBox(mesh, {x - 0.048f, 0.237f, 0.51f}, {x + 0.048f, 0.302f, 0.546f}, {1.0f, 0.90f, 0.62f});
        AddBox(mesh, {x - 0.048f, 0.237f, -0.546f}, {x + 0.048f, 0.302f, -0.51f}, {0.55f, 0.025f, 0.027f});
    }
    for (const float x : {-0.265f, 0.265f}) {
        AddWheel(mesh, x, -0.36f);
        AddWheel(mesh, x, 0.36f);
    }
    return mesh;
}

}  // namespace tinyworld
