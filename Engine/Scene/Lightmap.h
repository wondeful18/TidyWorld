#pragma once

#include "Engine/Scene/GltfAsset.h"

namespace tinyworld {

// 图集存线性辐照度，RGB 每个字节对应 [0, 4]。
inline constexpr float kLightmapRange = 4.0f;
inline constexpr std::uint32_t kPorchLightmapSide = 256;

//————————————————————————————————————————————————————————————————————————————————
// 从部署目录读取固定大小的 TWLM 图集，并拒绝损坏的资源。
//————————————————————————————————————————————————————————————————————————————————
[[nodiscard]] GltfImage ReadLightmap(const std::filesystem::path& path);
//————————————————————————————————————————————————————————————————————————————————
// 写入指定路径。构建命令传入临时路径，烘焙结束后再替换正式产物。
//————————————————————————————————————————————————————————————————————————————————
void WriteLightmap(const std::filesystem::path& path, const GltfImage& image);

}  // namespace tinyworld
