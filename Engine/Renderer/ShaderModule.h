#pragma once

#include "Engine/Renderer/WgpuHandle.h"

#include <filesystem>

namespace tinyworld {

class WebGpuContext;

//————————————————————————————————————————————————————————————————————————————————
// 从独立文件创建并验证 WGSL 模块，错误消息保留文件路径。
//————————————————————————————————————————————————————————————————————————————————
[[nodiscard]] ShaderHandle LoadWgslModule(WebGpuContext& gpu, const std::filesystem::path& path);

}  // namespace tinyworld
