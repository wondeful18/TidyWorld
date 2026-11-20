#pragma once

#include "Engine/Renderer/WgpuHandle.h"

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 在指定颜色目标上开始一个清屏后写入的通道，可同时清除深度。
// 调用方编码绘制命令并在释放句柄前结束通道。
//————————————————————————————————————————————————————————————————————————————————
[[nodiscard]] PassHandle BeginColorPass(WGPUCommandEncoder encoder, WGPUTextureView target,
                                       WGPUColor clearColor, const char* label,
                                       WGPUTextureView depthTarget = nullptr);

}  // namespace tinyworld
