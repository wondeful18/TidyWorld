#pragma once

#include <webgpu/webgpu.h>

#include <functional>

namespace tinyworld {

class Log;
class WebGpuContext;
class Window;

//————————————————————————————————————————————————————————————————————————————————
// 把 Surface 的颜色视图交给 encode，统一处理命令提交、呈现和窗口事件。
// encode 每帧收到借用的编码器和颜色视图，需要在返回前结束 Render Pass。
//————————————————————————————————————————————————————————————————————————————————
void RunDirectFrames(Window& window, WebGpuContext& gpu, Log& log,
                     const std::function<void(WGPUCommandEncoder, WGPUTextureView)>& encode);

}  // namespace tinyworld
