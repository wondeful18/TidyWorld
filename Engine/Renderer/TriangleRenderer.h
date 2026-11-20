#pragma once

#include "Engine/Renderer/WgpuHandle.h"

#include <filesystem>

namespace tinyworld {

class WebGpuContext;

//————————————————————————————————————————————————————————————————————————————————
// 加载独立 WGSL 并绘制本章的三个彩色顶点。
// 可画入窗口或编辑器纹理，颜色格式在创建时确定。
//————————————————————————————————————————————————————————————————————————————————
class TriangleRenderer {
public:
    //————————————————————————————————————————————————————————————————————————————————
    // 验证着色器与 Pipeline，并上传一次静态顶点数据。
    // colorFormat 必须匹配后续绘制目标的纹理格式。
    //————————————————————————————————————————————————————————————————————————————————
    TriangleRenderer(WebGpuContext& gpu, WGPUTextureFormat colorFormat,
                     const std::filesystem::path& shaderPath);

    //————————————————————————————————————————————————————————————————————————————————
    // 编码清屏与三角形绘制命令。
    // 不提交 encoder，提交时机由应用统一决定。
    //————————————————————————————————————————————————————————————————————————————————
    void Encode(WGPUCommandEncoder encoder, WGPUTextureView target) const;

private:
    PipelineHandle mPipeline;       // 三角形的绑定格式、着色器及光栅状态
    BufferHandle mVertexBuffer;     // 一次上传的三个位置与颜色顶点
    WGPUColor mClearColor{};         // 已按目标颜色格式转换的背景色
};

}  // namespace tinyworld
