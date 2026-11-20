#pragma once

#include <webgpu/webgpu.h>

#include <cmath>

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 检查通道的颜色目标是否会在写入时执行硬件 sRGB 编码。
//————————————————————————————————————————————————————————————————————————————————
[[nodiscard]] inline bool IsSrgbFormat(WGPUTextureFormat format) {
    return format == WGPUTextureFormat_RGBA8UnormSrgb || format == WGPUTextureFormat_BGRA8UnormSrgb;
}

//————————————————————————————————————————————————————————————————————————————————
// 将显示颜色换算为线性强度，供启用硬件编码的目标使用。
//————————————————————————————————————————————————————————————————————————————————
[[nodiscard]] inline double DisplayToLinear(double value) {
    return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}

}  // namespace tinyworld
