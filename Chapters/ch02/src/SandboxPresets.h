#pragma once

#include "Editor/Application.h"

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 建立平地热身场景。按章开启单图阴影、环境贴图与步行。
//————————————————————————————————————————————————————————————————————————————————
WorldScene CreateSandboxScene(const ApplicationOptions& options);

}  // namespace tinyworld
