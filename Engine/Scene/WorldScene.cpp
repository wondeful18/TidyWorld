#include "Engine/Scene/WorldScene.h"

#include <glm/trigonometric.hpp>

#include <cmath>

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 把可编辑的方位与仰角转为三维单位方向。
//————————————————————————————————————————————————————————————————————————————————
glm::vec3 DirectionalSun::TowardLight() const {
    const float yaw = glm::radians(azimuth);
    const float pitch = glm::radians(elevation);
    return {std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
}

//————————————————————————————————————————————————————————————————————————————————
// 两个演示分别改变遮挡关系和日照方向，便于用相同资源做对照。
//————————————————————————————————————————————————————————————————————————————————
void WorldScene::ApplyPreset(WorldPreset preset) {
    *this = WorldScene{};
    switch (preset) {
    case WorldPreset::Default:
        break;
    case WorldPreset::Overlap:
        objects[1].transform.position = {0.27f, 0.0f, 2.82f};
        break;
    case WorldPreset::Evening:
        sun = {121.0f, 18.0f, 1.12f, {1.0f, 0.73f, 0.48f}};
        break;
    }
}

}  // namespace tinyworld
