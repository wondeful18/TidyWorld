#pragma once

#include "Engine/Scene/SceneMesh.h"

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 生成第六章的占位机器人。脚底在原点，面板朝局部 +Z。
//————————————————————————————————————————————————————————————————————————————————
[[nodiscard]] std::vector<SceneVertex> MakeRobotMesh();

}  // namespace tinyworld
