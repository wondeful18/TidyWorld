#include "Runtime/WalkController.h"

#include "Engine/Terrain/HeightField.h"

#include <glm/geometric.hpp>
#include <glm/matrix.hpp>
#include <glm/vec4.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace tinyworld {
namespace {

constexpr double kStep = 1.0 / 120.0;
constexpr double kTimeTolerance = 1.0e-9;
constexpr float kWalkSpeed = 1.8f;
constexpr float kTurnSpeed = 125.0f;
constexpr float kStrideMeters = 0.95f;  // 同一只脚两次落地之间的距离

//————————————————————————————————————————————————————————————————————————————————
// 将绕 Y 轴的角度转换成水平前向，角度使用度。
//————————————————————————————————————————————————————————————————————————————————
glm::vec3 Forward(float yaw) {
    return {std::sin(glm::radians(yaw)), 0.0f, std::cos(glm::radians(yaw))};
}

//————————————————————————————————————————————————————————————————————————————————
// 以竖直线查询桥面。只接纳足够朝上的表面，防止旋转成墙的桥仍可踩踏。
//————————————————————————————————————————————————————————————————————————————————
float WalkSurfaceHeight(const HeightField& terrain, glm::vec2 xz, std::span<const PlacedProp> props,
                        float maxDeckHeight = std::numeric_limits<float>::infinity()) {
    float height = terrain.HeightAt(xz);
    for (const auto& prop : props) {
        if (prop.asset != kBridgeAsset) continue;
        const auto model = prop.transform.Matrix();
        const auto normal = glm::normalize(glm::cross(glm::vec3(model[2]), glm::vec3(model[0])));
        if (normal.y < 0.55f) continue;
        const auto center = prop.transform.position;
        const float y = center.y - (normal.x * (xz.x - center.x) + normal.z * (xz.y - center.z)) / normal.y;
        if (y <= height || y > maxDeckHeight) continue;
        const auto local = glm::inverse(model) * glm::vec4(xz.x, y, xz.y, 1.0f);
        if (std::abs(local.x) <= kBridgeDeckHalfWidth && std::abs(local.z) <= kBridgeDeckHalfLength) {
            height = y;
        }
    }
    return height;
}

}  // namespace

//————————————————————————————————————————————————————————————————————————————————
// T × R × S 作用于脚底的点，朝向向量只应用 R × S。
//————————————————————————————————————————————————————————————————————————————————
Transform WalkSpawn::WorldPose(const Transform& ground, const HeightField* terrain,
                               std::span<const PlacedProp> props) const {
    if (terrain) {
        return {{groundXZ.x, WalkSurfaceHeight(*terrain, groundXZ, props), groundXZ.y}, {0.0f, yaw, 0.0f}};
    }
    const auto model = ground.Matrix();
    const auto feet = glm::vec3(model * glm::vec4(groundXZ.x, 0.0f, groundXZ.y, 1.0f));
    const auto forward = glm::vec3(model * glm::vec4(Forward(yaw), 0.0f));
    // 草台朝向可编辑，角色保持直立。水平投影消失时沿用局部偏航角。
    const float heading = std::hypot(forward.x, forward.z) > 0.0001f
        ? glm::degrees(std::atan2(forward.x, forward.z))
        : yaw;
    return {feet, {0.0f, heading, 0.0f}};
}

//————————————————————————————————————————————————————————————————————————————————
// 从出生点生成脚底和跟随相机，固定距离保留前方可检查的地面。
//————————————————————————————————————————————————————————————————————————————————
WalkController::WalkController(const WalkSpawn& spawn, const Transform& ground, const HeightField* terrain,
                               std::span<const PlacedProp> props)
    : mFeet(spawn), mFeetHeight(terrain ? WalkSurfaceHeight(*terrain, spawn.groundXZ, props) : 0.0f) {
    mCamera.distance = 4.3f;
    mCamera.elevation = 9.0f;
    Follow(ground, terrain);
}

//————————————————————————————————————————————————————————————————————————————————
// 每个时间步先转向、再按世界米数移动，并把局部坐标夹在草台边界内。
//————————————————————————————————————————————————————————————————————————————————
void WalkController::Update(const WalkSpawn& spawn, const Transform& ground, WalkInput input,
                            double deltaSeconds, const HeightField* terrain, std::span<const PlacedProp> props) {
    if (input.reset) {
        *this = WalkController(spawn, ground, terrain, props);
        return;
    }

    // 窗口恢复后最多补算 0.1 秒，避免把长时间暂停变成一次大跳跃。
    mRemaining += std::clamp(deltaSeconds, 0.0, 0.1);
    const auto groundBasis = terrain ? glm::mat3{1.0f} : glm::mat3(ground.Matrix());
    const float limitX = terrain ? terrain->Grid().WalkLimit() : WalkSpawn::kLimitX;
    const float limitZ = terrain ? terrain->Grid().WalkLimit() : WalkSpawn::kLimitZ;
    const float stepSeconds = static_cast<float>(kStep);
    const float turnDegrees = input.turn * kTurnSpeed * stepSeconds;
    // 允许纳秒量级的舍入误差，避免同一段时间因帧分组少推进一步。
    while (mRemaining + kTimeTolerance >= kStep) {
        mFeet.yaw = std::remainder(mFeet.yaw + turnDegrees, 360.0f);
        const glm::vec3 forward = Forward(mFeet.yaw);
        const float metersPerUnit = glm::length(groundBasis * forward);
        const float travel = input.forward * kWalkSpeed * stepSeconds / metersPerUnit;
        const auto previous = mFeet.groundXZ;
        mFeet.groundXZ += glm::vec2(forward.x, forward.z) * travel;
        mFeet.groundXZ.x = std::clamp(mFeet.groundXZ.x, -limitX, limitX);
        mFeet.groundXZ.y = std::clamp(mFeet.groundXZ.y, -limitZ, limitZ);
        if (terrain) {
            // 从沟底靠近桥时，只有可迈上去的桥面才成为新的脚底高度。
            constexpr float maxStepUp = 0.24f;
            mFeetHeight = WalkSurfaceHeight(*terrain, mFeet.groundXZ, props, mFeetHeight + maxStepUp);
        }
        const auto moved = mFeet.groundXZ - previous;
        const float distance = glm::length(groundBasis * glm::vec3{moved.x, 0.0f, moved.y});
        const bool stepping = distance > 1.0e-5f;
        if (stepping) {
            mGait.direction = std::copysign(1.0f, input.forward);
            mGait.phase = std::remainder(mGait.phase + glm::two_pi<float>() * mGait.direction * distance / kStrideMeters,
                                         glm::two_pi<float>());
        }
        mGait.strength = std::lerp(mGait.strength, stepping ? 1.0f : 0.0f, 12.0f * stepSeconds);
        mRemaining = std::max(0.0, mRemaining - kStep);
    }
    Follow(ground, terrain);
}

//————————————————————————————————————————————————————————————————————————————————
// 看向角色胸口前方。OrbitCamera 的 0 度在观察点的 +Z 侧，加 180 度得到角色身后。
//————————————————————————————————————————————————————————————————————————————————
void WalkController::Follow(const Transform& ground, const HeightField* terrain) {
    mPose = mFeet.WorldPose(ground, terrain);
    if (terrain) mPose.position.y = mFeetHeight;
    const float yaw = mPose.rotationDegrees.y;
    mCamera.target = mPose.position + glm::vec3{0.0f, 1.05f, 0.0f} + Forward(yaw) * 0.45f;
    mCamera.azimuth = yaw + 180.0f;
    if (terrain) {
        // 稍偏向一侧，让第三人称视角同时看到腿和手臂的前后摆动。
        mCamera.azimuth += 13.0f;
        mCamera.distance = 4.3f;
        mCamera.elevation = 13.0f;
        const auto eye = mCamera.Position();
        const float minimumY = terrain->HeightAt({eye.x, eye.z}) + 0.35f;
        if (eye.y < minimumY) {
            // 只抬高镜头，保留水平位置，避免从坡下回头时仰角把相机拉进坡面。
            const float horizontal = glm::length(glm::vec2(eye.x - mCamera.target.x, eye.z - mCamera.target.z));
            const float vertical = minimumY - mCamera.target.y;
            mCamera.elevation = glm::degrees(std::atan2(vertical, horizontal));
            mCamera.distance = std::hypot(horizontal, vertical);
        }
    }
}

}  // namespace tinyworld
