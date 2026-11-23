#pragma once

#include "Engine/Scene/SceneMesh.h"

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 生成脚底位于原点、面向局部 +Z 的占位角色。
//————————————————————————————————————————————————————————————————————————————————
[[nodiscard]] std::vector<SceneVertex> MakeWalkerMesh();

}  // namespace tinyworld
