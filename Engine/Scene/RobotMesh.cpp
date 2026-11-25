#include "Engine/Scene/RobotMesh.h"

#include "Engine/Scene/PrimitiveMesh.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace tinyworld {
namespace {
constexpr primitive::Color kShell{0.71f, 0.77f, 0.69f};
constexpr primitive::Color kDark{0.026f, 0.069f, 0.084f};
constexpr primitive::Color kYellow{0.96f, 0.44f, 0.06f};
}  // namespace

//————————————————————————————————————————————————————————————————————————————————
// 用头壳、面板、短腿和天线组成容易辨认的机器人轮廓。
//————————————————————————————————————————————————————————————————————————————————
std::vector<SceneVertex> MakeRobotMesh() {
    using primitive::AddBox;
    using primitive::Color;
    primitive::Mesh mesh;
    constexpr Color teal{0.035f, 0.39f, 0.39f};
    constexpr Color led{0.40f, 0.91f, 0.86f};

    AddBox(mesh, {-0.215f, 0.78f, -0.16f}, {0.215f, 1.16f, 0.16f}, kShell, {0.032f, 0.025f});
    AddBox(mesh, {-0.174f, 0.848f, 0.154f}, {0.174f, 1.085f, 0.181f}, kDark);
    AddBox(mesh, {-0.17f, 0.27f, -0.142f}, {0.17f, 0.73f, 0.142f}, teal, {0.025f, 0.021f});
    AddBox(mesh, {-0.115f, 0.39f, -0.18f}, {0.115f, 0.64f, -0.122f}, kDark, {0.02f, 0.012f});
    AddBox(mesh, {-0.072f, 0.49f, -0.192f}, {0.072f, 0.526f, -0.175f}, kYellow);
    AddBox(mesh, {-0.065f, 0.69f, -0.058f}, {0.065f, 0.81f, 0.058f}, kDark);
    AddBox(mesh, {0.075f, 1.12f, -0.022f}, {0.105f, 1.34f, 0.018f}, kDark);
    AddBox(mesh, {0.048f, 1.30f, -0.045f}, {0.133f, 1.39f, 0.043f}, kYellow, {0.012f, 0.01f});

    for (const float side : {-1.0f, 1.0f}) {
        const float eye = side * 0.077f;
        AddBox(mesh, {eye - 0.028f, 0.944f, 0.179f}, {eye + 0.028f, 1.011f, 0.19f}, led);
    }
    return mesh;
}

std::vector<SceneVertex> MakeRobotLamp() {
    primitive::Mesh mesh;
    constexpr primitive::Color housing{0.063f, 0.044f, 0.028f};
    constexpr primitive::Color glass{0.93f, 0.54f, 0.14f};
    primitive::AddBox(mesh, {-0.027f, 0.035f, -0.22f}, {0.027f, 0.068f, 0.0f}, housing);
    primitive::AddBox(mesh, {-0.049f, -0.043f, -0.049f}, {0.049f, 0.036f, 0.049f}, glass);
    primitive::AddBox(mesh, {-0.059f, -0.06f, -0.059f}, {0.059f, -0.043f, 0.059f}, housing);
    primitive::AddBox(mesh, {-0.059f, 0.036f, -0.059f}, {0.059f, 0.052f, 0.059f}, housing);
    return mesh;
}

//————————————————————————————————————————————————————————————————————————————————
// 手臂以肩部为原点，腿和脚以髋部为原点，网格可独立绕关节运动。
//————————————————————————————————————————————————————————————————————————————————
std::vector<SceneVertex> MakeRobotLimb(RobotLimb limb) {
    primitive::Mesh mesh;
    if (limb == RobotLimb::LeftArm || limb == RobotLimb::RightArm) {
        primitive::AddBox(mesh, {-0.042f, -0.26f, -0.068f}, {0.042f, 0.03f, 0.068f}, kYellow, {0.012f, 0.01f});
    } else {
        primitive::AddBox(mesh, {-0.052f, -0.218f, -0.062f}, {0.052f, 0.02f, 0.066f}, kDark);
        primitive::AddBox(mesh, {-0.068f, -0.29f, -0.101f}, {0.068f, -0.18f, 0.136f}, kShell);
    }
    return mesh;
}

//————————————————————————————————————————————————————————————————————————————————
// 左右侧相差半个周期，同侧肩和髋反向摆动，摆腿向行走方向前进时抬脚。
//————————————————————————————————————————————————————————————————————————————————
glm::mat4 RobotLimbPose(RobotLimb limb, WalkGait gait) {
    const bool arm = limb == RobotLimb::LeftArm || limb == RobotLimb::RightArm;
    const bool left = limb == RobotLimb::LeftArm || limb == RobotLimb::LeftLeg;
    const float phase = gait.phase + (left ? 0.0f : glm::pi<float>());
    const float swing = std::sin(phase) * gait.strength;
    const float angle = glm::radians(arm ? 38.0f : -31.0f) * swing;
    const float lift = arm ? 0.0f : 0.085f * std::max(0.0f, std::cos(phase) * gait.direction) * gait.strength;
    const glm::vec3 pivot{(left ? 1.0f : -1.0f) * (arm ? 0.205f : 0.091f),
                          (arm ? 0.64f : 0.29f) + lift, 0.0f};
    return glm::rotate(glm::translate(glm::mat4{1.0f}, pivot), angle, {1.0f, 0.0f, 0.0f});
}

}  // namespace tinyworld
