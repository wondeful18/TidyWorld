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
// 预设从默认布局出发，分别演示遮挡、日照和小车漫游。
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
    case WorldPreset::Drive:
        SetDriving(true);
        break;
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 只有进入漫游时才初始化小车，退出后恢复使用编辑器相机。
//————————————————————————————————————————————————————————————————————————————————
void WorldScene::SetDriving(bool active) {
    if (active && !mCar) {
        mCar.emplace(objects[kCarIndex].transform);
    } else if (!active) {
        mCar.reset();
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 漫游开启时把控制器结果写到与渲染器共享的车身变换。
//————————————————————————————————————————————————————————————————————————————————
void WorldScene::UpdateDrive(CarInput input, float deltaSeconds) {
    if (mCar) {
        mCar->Update(objects[kCarIndex].transform, input, deltaSeconds);
    }
}

}  // namespace tinyworld
