#include "Engine/Renderer/SkyRenderer.h"

#include "Engine/Renderer/Color.h"
#include "Engine/Renderer/ShaderModule.h"
#include "Engine/Renderer/WebGpuContext.h"

#include <glm/gtc/constants.hpp>
#include <glm/mat3x3.hpp>
#include <glm/vec4.hpp>

#include <cmath>
#include <stdexcept>
#include <vector>

namespace tinyworld {
namespace {

//————————————————————————————————————————————————————————————————————————————————
// 与 sky.wgsl 的 SkyUniform 对应，vec4 用来满足统一布局对齐。
//————————————————————————————————————————————————————————————————————————————————
struct SkyUniform {
    glm::mat4 rotationProjection;  // 去掉相机平移后的视图投影
    glm::vec4 options;             // x 为天空球开关，其余保留
    glm::vec4 horizon;             // 海岸天际线的线性 RGB，w 为开关
};

static_assert(sizeof(SkyUniform) == 96);
static_assert(sizeof(glm::vec3) == 3 * sizeof(float));

//————————————————————————————————————————————————————————————————————————————————
// 向六个面各添加两个三角形。单位立方体中心是观看者。
//————————————————————————————————————————————————————————————————————————————————
std::vector<glm::vec3> MakeSkyBox() {
    constexpr std::array<glm::vec3, 8> corners{{
        {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
        {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1},
    }};
    constexpr std::array<int, 36> indices{{
        0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7, 0, 4, 7, 0, 7, 3,
        1, 5, 6, 1, 6, 2, 0, 1, 5, 0, 5, 4, 3, 2, 6, 3, 6, 7,
    }};
    std::vector<glm::vec3> vertices;
    vertices.reserve(indices.size());
    for (const int index : indices) {
        vertices.push_back(corners[index]);
    }
    return vertices;
}

//————————————————————————————————————————————————————————————————————————————————
// 纬线和经线的交点落在单位球上，极点用精确坐标封口。
//————————————————————————————————————————————————————————————————————————————————
std::vector<glm::vec3> MakeSkySphere() {
    constexpr int bands = 24;
    constexpr int slices = 48;
    const auto point = [](int row, int column) -> glm::vec3 {
        if (row == 0 || row == bands) {
            return {0.0f, row == 0 ? 1.0f : -1.0f, 0.0f};
        }
        const float latitude = glm::half_pi<float>() - glm::pi<float>() * row / bands;
        const float longitude = glm::two_pi<float>() * (column % slices) / slices;
        return {std::cos(latitude) * std::sin(longitude), std::sin(latitude),
                std::cos(latitude) * std::cos(longitude)};
    };
    std::vector<glm::vec3> vertices;
    vertices.reserve(bands * slices * 6);
    for (int row = 0; row < bands; ++row) {
        for (int column = 0; column < slices; ++column) {
            const auto a = point(row, column);
            const auto b = point(row, column + 1);
            const auto c = point(row + 1, column);
            const auto d = point(row + 1, column + 1);
            if (row != 0) {
                vertices.insert(vertices.end(), {a, b, c});
            }
            if (row + 1 != bands) {
                vertices.insert(vertices.end(), {b, d, c});
            }
        }
    }
    return vertices;
}

//————————————————————————————————————————————————————————————————————————————————
// 只允许天空覆盖相机远平面尚未被场景写入的像素。
//————————————————————————————————————————————————————————————————————————————————
PipelineHandle MakeSkyPipeline(WebGpuContext& gpu, WGPUTextureFormat colorFormat, WGPUTextureFormat depthFormat,
                               WGPUShaderModule shader, WGPUBindGroupLayout view, WGPUBindGroupLayout environment) {
    const std::array layouts{view, environment};
    WGPUPipelineLayoutDescriptor description = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    description.bindGroupLayoutCount = layouts.size();
    description.bindGroupLayouts = layouts.data();
    PipelineLayoutHandle layout(wgpuDeviceCreatePipelineLayout(gpu.Device(), &description));
    if (!layout) {
        return PipelineHandle{};
    }

    WGPUVertexAttribute position = WGPU_VERTEX_ATTRIBUTE_INIT;
    position.format = WGPUVertexFormat_Float32x3;
    WGPUVertexBufferLayout vertices = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
    vertices.arrayStride = sizeof(glm::vec3);
    vertices.stepMode = WGPUVertexStepMode_Vertex;
    vertices.attributeCount = 1;
    vertices.attributes = &position;
    WGPUDepthStencilState depth = WGPU_DEPTH_STENCIL_STATE_INIT;
    depth.format = depthFormat;
    depth.depthCompare = WGPUCompareFunction_LessEqual;
    depth.depthWriteEnabled = WGPUOptionalBool_False;

    WGPUColorTargetState color = WGPU_COLOR_TARGET_STATE_INIT;
    color.format = colorFormat;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = shader;
    fragment.entryPoint = {IsSrgbFormat(colorFormat) ? "fsSkyLinear" : "fsSkyDisplay", WGPU_STRLEN};
    fragment.targetCount = 1;
    fragment.targets = &color;

    WGPURenderPipelineDescriptor pipeline = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    pipeline.label = {"Chapter 04 skybox and panoramic sphere", WGPU_STRLEN};
    pipeline.layout = layout.get();
    pipeline.vertex.module = shader;
    pipeline.vertex.entryPoint = {"vsSky", WGPU_STRLEN};
    pipeline.vertex.bufferCount = 1;
    pipeline.vertex.buffers = &vertices;
    pipeline.primitive.cullMode = WGPUCullMode_None;
    pipeline.depthStencil = &depth;
    pipeline.fragment = &fragment;
    return PipelineHandle(wgpuDeviceCreateRenderPipeline(gpu.Device(), &pipeline));
}

//————————————————————————————————————————————————————————————————————————————————
// 分配可经由队列填充的天空顶点或常量缓冲。
//————————————————————————————————————————————————————————————————————————————————
BufferHandle MakeSkyBuffer(WGPUDevice device, WGPUBufferUsage usage, std::uint64_t bytes) {
    WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
    descriptor.size = bytes;
    descriptor.usage = usage | WGPUBufferUsage_CopyDst;
    return BufferHandle(wgpuDeviceCreateBuffer(device, &descriptor));
}

}  // namespace

//————————————————————————————————————————————————————————————————————————————————
// 创建常量绑定和两种天空网格，两个模式共用一条渲染管线。
//————————————————————————————————————————————————————————————————————————————————
SkyRenderer::SkyRenderer(WebGpuContext& gpu, WGPUTextureFormat colorFormat,
                         WGPUTextureFormat depthFormat, const std::filesystem::path& assetRoot)
    : mGpu(gpu), mEnvironment(gpu, assetRoot / "environments" / "ch04" / "golden-gate-hills.jpg") {
    const auto shader = LoadWgslModule(gpu, assetRoot / "shaders" / "sky.wgsl");
    WGPUBindGroupLayoutEntry constants = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    constants.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    constants.buffer.type = WGPUBufferBindingType_Uniform;
    constants.buffer.minBindingSize = sizeof(SkyUniform);
    WGPUBindGroupLayoutDescriptor viewDescription = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    viewDescription.entryCount = 1;
    viewDescription.entries = &constants;
    wgpuDevicePushErrorScope(gpu.Device(), WGPUErrorFilter_Validation);
    const BindGroupLayoutHandle viewLayout(wgpuDeviceCreateBindGroupLayout(gpu.Device(), &viewDescription));
    mUniform = MakeSkyBuffer(gpu.Device(), WGPUBufferUsage_Uniform, sizeof(SkyUniform));
    gpu.CheckValidation("chapter 04 sky view layout and uniform");
    if (!viewLayout || !mUniform) {
        throw std::runtime_error("Cannot create sky uniform resources");
    }

    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.buffer = mUniform.get();
    entry.size = sizeof(SkyUniform);
    WGPUBindGroupDescriptor binding = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    binding.layout = viewLayout.get();
    binding.entryCount = 1;
    binding.entries = &entry;
    wgpuDevicePushErrorScope(gpu.Device(), WGPUErrorFilter_Validation);
    mViewBinding.reset(wgpuDeviceCreateBindGroup(gpu.Device(), &binding));
    mPipeline = MakeSkyPipeline(gpu, colorFormat, depthFormat, shader.get(), viewLayout.get(), mEnvironment.Layout());
    gpu.CheckValidation("chapter 04 sky color pipeline");
    if (!mViewBinding || !mPipeline) {
        throw std::runtime_error("Cannot create sky drawing pipeline");
    }

    const std::array meshes{MakeSkyBox(), MakeSkySphere()};
    for (std::size_t index = 0; index < meshes.size(); ++index) {
        const auto& vertices = meshes[index];
        const auto bytes = vertices.size() * sizeof(glm::vec3);
        mMeshes[index] = MakeSkyBuffer(gpu.Device(), WGPUBufferUsage_Vertex, bytes);
        if (!mMeshes[index]) {
            throw std::runtime_error("Cannot create sky vertex buffer");
        }
        wgpuQueueWriteBuffer(gpu.Queue(), mMeshes[index].get(), 0, vertices.data(), bytes);
        mCounts[index] = static_cast<std::uint32_t>(vertices.size());
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 保留相机旋转，使天空随视线转向，平移时始终以相机为中心。
//————————————————————————————————————————————————————————————————————————————————
void SkyRenderer::Draw(WGPURenderPassEncoder pass, const glm::mat4& projection,
                       const glm::mat4& view, SkyMode mode, glm::vec4 horizon) const {
    const bool panorama = mode == SkyMode::Panorama;
    const std::size_t index = panorama ? 1 : 0;
    const SkyUniform constants{projection * glm::mat4(glm::mat3(view)),
                               glm::vec4{panorama ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f}, horizon};
    wgpuQueueWriteBuffer(mGpu.Queue(), mUniform.get(), 0, &constants, sizeof(constants));
    wgpuRenderPassEncoderSetPipeline(pass, mPipeline.get());
    wgpuRenderPassEncoderSetBindGroup(pass, 0, mViewBinding.get(), 0, nullptr);
    wgpuRenderPassEncoderSetBindGroup(pass, 1, mEnvironment.Binding(), 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, mMeshes[index].get(), 0, mCounts[index] * sizeof(glm::vec3));
    wgpuRenderPassEncoderDraw(pass, mCounts[index], 1, 0, 0);
}

}  // namespace tinyworld
