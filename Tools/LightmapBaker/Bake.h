#pragma once

#include "Engine/Scene/Lightmap.h"

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 在资产本地空间烘焙课件门灯，返回带覆盖标记的线性图集。
//————————————————————————————————————————————————————————————————————————————————
[[nodiscard]] GltfImage BakePorch(const GltfAsset& asset);

}  // namespace tinyworld
