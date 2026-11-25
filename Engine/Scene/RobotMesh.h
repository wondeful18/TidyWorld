#pragma once

#include "Engine/Scene/SceneMesh.h"
#include "Runtime/WalkController.h"

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 生成第六章的占位机器人。脚底在原点，面板朝局部 +Z。
//————————————————————————————————————————————————————————————————————————————————
[[nodiscard]] std::vector<SceneVertex> MakeRobotMesh();
[[nodiscard]] std::vector<SceneVertex> MakeRobotLamp();

enum class RobotLimb { LeftArm, RightArm, LeftLeg, RightLeg, Count };
inline constexpr std::size_t kRobotLimbCount = static_cast<std::size_t>(RobotLimb::Count);

[[nodiscard]] std::vector<SceneVertex> MakeRobotLimb(RobotLimb limb);
[[nodiscard]] glm::mat4 RobotLimbPose(RobotLimb limb, WalkGait gait);

}  // namespace tinyworld
