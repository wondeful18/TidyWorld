#include "Engine/Scene/RoamingCarMesh.h"

#include "Engine/Scene/PrimitiveMesh.h"

#include <glm/gtc/constants.hpp>

#include <cmath>

namespace tinyworld {
namespace {

using primitive::AddBox;
using primitive::AddFace;
using primitive::Color;
using primitive::Mesh;

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
std::vector<SceneVertex> MakeRoamingCarMesh(CarSurfaceRanges* surfaces) {
    Mesh mesh;
    constexpr Color paint{0.79f, 0.20f, 0.045f};
    constexpr Color glass{0.078f, 0.19f, 0.25f};
    constexpr Color trim{0.035f, 0.052f, 0.061f};
    AddBox(mesh, {-0.29f, 0.15f, -0.54f}, {0.29f, 0.34f, 0.54f}, paint, {0.018f, 0.04f});
    const auto glassBegin = mesh.size();
    AddBox(mesh, {-0.23f, 0.32f, -0.39f}, {0.23f, 0.53f, 0.17f}, glass, {0.04f, 0.10f});
    const auto glassEnd = mesh.size();
    AddBox(mesh, {-0.197f, 0.52f, -0.297f}, {0.197f, 0.56f, 0.077f}, paint, {0.016f, 0.022f});
    if (surfaces) {
        *surfaces = {glassBegin, glassEnd, mesh.size()};
    }
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
