#include "Engine/Renderer/WaterRenderer.h"

#include "Engine/Renderer/Color.h"
#include "Engine/Renderer/EnvironmentMap.h"
#include "Engine/Renderer/ShaderModule.h"
#include "Engine/Renderer/WebGpuContext.h"
#include "Engine/Terrain/HeightField.h"

#include <glm/common.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/matrix.hpp>
#include <glm/vec4.hpp>

#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace tinyworld {
namespace {

//————————————————————————————————————————————————————————————————————————————————
// 正矩阵用于绘制水面，逆矩阵用于把相机深度重建成世界坐标。
//————————————————————————————————————————————————————————————————————————————————
struct WaterUniform {
    glm::mat4 viewProjection;     // 世界坐标到 WebGPU 裁剪坐标
    glm::mat4 inverseViewProjection;  // 除以重建后的 W 即得世界坐标
    glm::vec4 cameraAndTime;      // 相机 XYZ 与流水周期的弧度相位
    glm::vec4 controls;           // 依次为深度观察、反射开关、海岸开关和填充
    glm::vec4 horizon;            // 与天空共用的远海线性颜色，w 保留
    FrameAtmosphere air;
    glm::vec4 lighting;           // x 控制环境亮度，y 为共享夜景比例          // 与场景物体共用的距离雾
};

static_assert(sizeof(WaterUniform) == 224);

//————————————————————————————————————————————————————————————————————————————————
// 用同一条水带从溪谷经过湖泊。海岸模式把东端逐渐扩宽到整片海面。
//————————————————————————————————————————————————————————————————————————————————
std::vector<glm::vec3> MakeWaterMesh(bool coast) {
    const auto section = [coast](float x) {
        const float offset = (x - 10.0f) / 5.9f;
        const float spread = (x - 10.0f) / 5.8f;
        float center = -0.83f * std::sin(x * 0.19f) - 1.4f * std::exp(-offset * offset);
        // 带宽比湿岸更大，由相机深度准确确定坡面在哪里盖住水。
        float width = 1.30f * (1.48f + 5.85f * std::exp(-spread * spread));
        if (coast && x > WaterSettings::kCoastStart) {
            const float mouth = glm::smoothstep(WaterSettings::kCoastStart, 20.0f, x);
            const float bankX = glm::min(x, WaterSettings::kCoastEnd);
            const float mouthZ = -2.6f + 0.31f * (bankX - 14.0f) + 0.48f * std::sin((bankX - 13.0f) * 0.45f);
            center = glm::mix(center, mouthZ, mouth);
            // 海面覆盖远处视锥，岸坡仍靠不透明深度裁出可见轮廓。
            const float offshore = glm::smoothstep(17.5f, WaterSettings::kCoastEnd, x);
            width = glm::max(width, glm::mix(2.0f, 180.0f, offshore));
        }
        const float limit = HeightField::kHalfExtent;
        return std::array{glm::vec3{x, WaterSettings::kLevel, coast ? glm::max(-limit, center - width) : center - width},
                          glm::vec3{x, WaterSettings::kLevel, coast ? glm::min(limit, center + width) : center + width}};
    };

    std::vector<glm::vec3> vertices;
    auto left = section(-24.0f);
    // 岸边延续半米分段，在世界 X = 25 米处与海面拼接。
    const int strips = coast ? 98 : 96;
    for (int i = 1; i <= strips; ++i) {
        const auto right = section(-24.0f + 0.5f * i);
        vertices.insert(vertices.end(), {left[0], left[1], right[0], right[0], left[1], right[1]});
        left = right;
    }
    if (coast) {
        // 海面与南北两条延伸带只在边上相接，避免同像素重复混合。
        const auto addRectangle = [&](float x0, float x1, float z0, float z1) {
            constexpr float y = WaterSettings::kLevel;
            const glm::vec3 a{x0, y, z0}, b{x0, y, z1}, c{x1, y, z0}, d{x1, y, z1};
            vertices.insert(vertices.end(), {a, b, c, c, b, d});
        };
        constexpr float edge = WaterSettings::kCoastFar + 70.0f;
        constexpr float side = HeightField::kHalfExtent;
        constexpr float join = WaterSettings::kCoastEnd;
        addRectangle(join, edge, -edge, edge);
        addRectangle(-edge, join, -edge, -side);
        addRectangle(-edge, join, side, edge);
    }
    return vertices;
}

//————————————————————————————————————————————————————————————————————————————————
// 创建可更新的顶点或 uniform 缓冲。
//————————————————————————————————————————————————————————————————————————————————
BufferHandle MakeWaterBuffer(WGPUDevice device, WGPUBufferUsage usage, std::size_t bytes) {
    WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
    descriptor.usage = usage | WGPUBufferUsage_CopyDst;
    descriptor.size = bytes;
    BufferHandle buffer(wgpuDeviceCreateBuffer(device, &descriptor));
    if (!buffer) throw std::runtime_error("Cannot allocate water buffer.");
    return buffer;
}

}  // namespace

//————————————————————————————————————————————————————————————————————————————————
// 水面不写深度。片元用同像素的不透明深度判断可见性，直接输出层叠颜色。
//————————————————————————————————————————————————————————————————————————————————
WaterRenderer::WaterRenderer(WebGpuContext& gpu, WGPUTextureFormat format, const EnvironmentMap& environment,
                             const std::filesystem::path& shaderPath, bool coast)
    : mGpu(gpu), mEnvironment(environment) {
    const auto shader = LoadWgslModule(gpu, shaderPath, shaderPath.parent_path() / "fog.wgsl");
    std::array<WGPUBindGroupLayoutEntry, 2> entries{
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT, WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
    };
    entries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;
    entries[0].buffer.minBindingSize = sizeof(WaterUniform);
    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Fragment;
    entries[1].texture.sampleType = WGPUTextureSampleType_Depth;
    entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
    WGPUBindGroupLayoutDescriptor binding = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    binding.entries = entries.data();
    binding.entryCount = entries.size();

    WGPUVertexAttribute position = WGPU_VERTEX_ATTRIBUTE_INIT;
    position.format = WGPUVertexFormat_Float32x3;
    WGPUVertexBufferLayout vertices = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
    vertices.arrayStride = sizeof(glm::vec3);
    vertices.attributeCount = 1;
    vertices.attributes = &position;

    WGPUBlendState blend = WGPU_BLEND_STATE_INIT;
    blend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.alpha.srcFactor = WGPUBlendFactor_One;
    blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    WGPUColorTargetState color = WGPU_COLOR_TARGET_STATE_INIT;
    color.format = format;
    color.blend = &blend;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = shader.get();
    fragment.entryPoint = {IsSrgbFormat(format) ? "fsWaterLinear" : "fsWaterDisplay", WGPU_STRLEN};
    fragment.targetCount = 1;
    fragment.targets = &color;

    wgpuDevicePushErrorScope(gpu.Device(), WGPUErrorFilter_Validation);
    mLayout.reset(wgpuDeviceCreateBindGroupLayout(gpu.Device(), &binding));
    const std::array layouts{mLayout.get(), environment.Layout()};
    WGPUPipelineLayoutDescriptor layout = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    layout.bindGroupLayoutCount = layouts.size();
    layout.bindGroupLayouts = layouts.data();
    PipelineLayoutHandle pipelineLayout(wgpuDeviceCreatePipelineLayout(gpu.Device(), &layout));
    WGPURenderPipelineDescriptor pipeline = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    pipeline.label = {"Water depth and reflection", WGPU_STRLEN};
    pipeline.layout = pipelineLayout.get();
    pipeline.vertex.module = shader.get();
    pipeline.vertex.entryPoint = {"vsWater", WGPU_STRLEN};
    pipeline.vertex.bufferCount = 1;
    pipeline.vertex.buffers = &vertices;
    pipeline.fragment = &fragment;
    mPipeline.reset(wgpuDeviceCreateRenderPipeline(gpu.Device(), &pipeline));
    gpu.CheckValidation("water render pipeline");
    if (!mLayout || !pipelineLayout || !mPipeline) throw std::runtime_error("Cannot create water pipeline.");

    const auto mesh = MakeWaterMesh(coast);
    const auto bytes = mesh.size() * sizeof(glm::vec3);
    mVertexCount = static_cast<std::uint32_t>(mesh.size());
    mVertices = MakeWaterBuffer(gpu.Device(), WGPUBufferUsage_Vertex, bytes);
    wgpuQueueWriteBuffer(gpu.Queue(), mVertices.get(), 0, mesh.data(), bytes);
    mUniform = MakeWaterBuffer(gpu.Device(), WGPUBufferUsage_Uniform, sizeof(WaterUniform));
}

//————————————————————————————————————————————————————————————————————————————————
// Load 保留不透明颜色。深度只作纹理绑定，窗口缩放后改用新的视图。
//————————————————————————————————————————————————————————————————————————————————
void WaterRenderer::Encode(WGPUCommandEncoder encoder, WGPUTextureView target, WGPUTextureView depthView,
                           const glm::mat4& viewProjection, glm::vec3 camera, const WaterSettings& settings,
                           glm::vec3 horizon, const FrameAtmosphere& air, glm::vec4 lighting) {
    if (depthView != mBoundDepth) {
        std::array<WGPUBindGroupEntry, 2> entries{WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT};
        entries[0].buffer = mUniform.get();
        entries[0].size = sizeof(WaterUniform);
        entries[1].binding = 1;
        entries[1].textureView = depthView;
        WGPUBindGroupDescriptor binding = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        binding.layout = mLayout.get();
        binding.entries = entries.data();
        binding.entryCount = entries.size();
        mBinding.reset(wgpuDeviceCreateBindGroup(mGpu.Device(), &binding));
        if (!mBinding) throw std::runtime_error("Cannot bind opaque depth for water.");
        mBoundDepth = depthView;
    }
    const float phase = settings.seconds * (glm::two_pi<float>() / WaterSettings::kLoopSeconds);
    const WaterUniform values{viewProjection, glm::inverse(viewProjection), glm::vec4(camera, phase),
                             {settings.view == WaterView::Depth ? 1.0f : 0.0f,
                              settings.view == WaterView::NoReflection ? 0.0f : 1.0f,
                              settings.coast ? 1.0f : 0.0f, 0.0f}, glm::vec4(horizon, 0.0f), air, lighting};
    wgpuQueueWriteBuffer(mGpu.Queue(), mUniform.get(), 0, &values, sizeof(values));

    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view = target;
    color.loadOp = WGPULoadOp_Load;
    color.storeOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor descriptor = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    descriptor.label = {"Water after opaque scene", WGPU_STRLEN};
    descriptor.colorAttachmentCount = 1;
    descriptor.colorAttachments = &color;
    PassHandle pass(wgpuCommandEncoderBeginRenderPass(encoder, &descriptor));
    if (!pass) throw std::runtime_error("Cannot begin water color pass.");
    wgpuRenderPassEncoderSetPipeline(pass.get(), mPipeline.get());
    wgpuRenderPassEncoderSetBindGroup(pass.get(), 0, mBinding.get(), 0, nullptr);
    wgpuRenderPassEncoderSetBindGroup(pass.get(), 1, mEnvironment.Binding(), 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(pass.get(), 0, mVertices.get(), 0, mVertexCount * sizeof(glm::vec3));
    wgpuRenderPassEncoderDraw(pass.get(), mVertexCount, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass.get());
}

}  // namespace tinyworld
