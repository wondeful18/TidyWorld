#pragma once

#include "Engine/Scene/SceneMesh.h"

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 运行时顶点布局。磁盘上的场景顶点仍只保存位置、法线和底色。
//————————————————————————————————————————————————————————————————————————————————
struct WorldVertex {
    SceneVertex geometry;   // 原始场景顶点，位置仍位于字节 0
    float reflectivity{};   // 玻璃反射较强，车漆保留底色，其余表面为 0
};

static_assert(sizeof(WorldVertex) == 10 * sizeof(float));

}  // namespace tinyworld
