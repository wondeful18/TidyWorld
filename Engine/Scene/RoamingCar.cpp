#include "Engine/Scene/RoamingCar.h"

#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>

namespace tinyworld {
namespace {

//————————————————————————————————————————————————————————————————————————————————
// 车头的局部 +Z 轴绕世界 Y 轴旋转后得到水平前向。
//————————————————————————————————————————————————————————————————————————————————
glm::vec3 Forward(float yawDegrees) {
    const float yaw = glm::radians(yawDegrees);
    return {std::sin(yaw), 0.0f, std::cos(yaw)};
}

}  // namespace

//————————————————————————————————————————————————————————————————————————————————
// 从小屋前的草地出发，对准房屋和树之间的空地。
//————————————————————————————————————————————————————————————————————————————————
RoamingCar::RoamingCar(Transform& body) {
    body = Transform{{0.9f, 0.0f, 2.8f}, {0.0f, 180.0f, 0.0f}};
    mCamera.elevation = 24.0f;
    mCamera.distance = 3.5f;
    Follow(body);
}

//————————————————————————————————————————————————————————————————————————————————
// 用加速、减速和转向更新平面位置，限制车身留在草地范围内。
//————————————————————————————————————————————————————————————————————————————————
void RoamingCar::Update(Transform& body, CarInput input, float deltaSeconds) {
    if (input.reset) {
        *this = RoamingCar(body);
        return;
    }

    // 断点和窗口恢复可能带来很长的一帧，限制单次位移以免车身跳跃。
    const float dt = std::clamp(deltaSeconds, 0.0f, 0.05f);
    const float targetSpeed = input.throttle * (input.throttle < 0.0f ? 1.25f : 2.8f);
    const float speedStep = (input.throttle == 0.0f ? 6.0f : 4.5f) * dt;
    mSpeed += std::clamp(targetSpeed - mSpeed, -speedStep, speedStep);

    // 前进和倒车时转向符号相反，静止时车身不会原地旋转。
    auto& yaw = body.rotationDegrees.y;
    yaw = std::remainder(yaw + input.steering * 100.0f * (mSpeed / 2.8f) * dt, 360.0f);
    body.position += Forward(yaw) * (mSpeed * dt);

    // 草地半宽 5.8 m、半长 4.2 m，给车身和圆角留出余量。
    body.position.x = std::clamp(body.position.x, -5.0f, 5.0f);
    body.position.z = std::clamp(body.position.z, -3.4f, 3.4f);
    Follow(body);
}

//————————————————————————————————————————————————————————————————————————————————
// 观察点略低于车顶，并向车头方向偏移。
//————————————————————————————————————————————————————————————————————————————————
void RoamingCar::Follow(const Transform& body) {
    const float yaw = body.rotationDegrees.y;
    mCamera.target = body.position + glm::vec3{0.0f, 0.46f, 0.0f} + Forward(yaw) * 0.45f;
    mCamera.azimuth = yaw + 180.0f;
}

}  // namespace tinyworld
