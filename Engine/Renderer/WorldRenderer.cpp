#include "Engine/Renderer/WorldRenderer.h"

#include "Engine/Renderer/Color.h"
#include "Engine/Renderer/RenderPass.h"
#include "Engine/Renderer/ShaderModule.h"
#include "Engine/Renderer/WebGpuContext.h"
#include "Engine/Scene/RoamingCarMesh.h"
#include "Engine/Scene/SceneMesh.h"

#include <glm/matrix.hpp>
#include <glm/vec4.hpp>

#include <cstddef>
#include <stdexcept>

namespace tinyworld {
namespace {

constexpr auto kDepthFormat = WGPUTextureFormat_Depth24Plus;

//————————————————————————————————————————————————————————————————————————————————
// 按 WGSL 对齐顺序打包每个物体的矩阵与光照参数。
//————————————————————————————————————————————————————————————————————————————————
struct DrawUniform {
    glm::mat4 viewProjection;  // 将世界坐标转换到 WebGPU 裁剪空间
    glm::mat4 model;           // 将模型顶点转换到世界空间
    glm::mat4 normalToWorld;   // 模型矩阵的逆转置，处理非均匀缩放
    glm::vec4 towardSun;       // 从表面指向太阳的世界方向
    glm::vec4 sunRadiance;     // 线性灯光颜色乘以强度
};

static_assert(sizeof(DrawUniform) == 224);

//————————————————————————————————————————————————————————————————————————————————
// 声明位置、法线、底色三组顶点属性，并为近处三角形开启深度写入。
//————————————————————————————————————————————————————————————————————————————————
PipelineHandle BuildPipeline(WebGpuContext& gpu, WGPUTextureFormat format, const std::filesystem::path& path) {
    const auto shader = LoadWgslModule(gpu, path);
    std::array<WGPUVertexAttribute, 3> attributes{};
    constexpr std::array offsets{offsetof(SceneVertex, position), offsetof(SceneVertex, normal),
                                 offsetof(SceneVertex, color)};
    for (std::uint32_t i = 0; i < attributes.size(); ++i) {
        attributes[i] = WGPU_VERTEX_ATTRIBUTE_INIT;
        attributes[i].format = WGPUVertexFormat_Float32x3;
        attributes[i].offset = offsets[i];
        attributes[i].shaderLocation = i;
    }
    WGPUVertexBufferLayout layout = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
    layout.arrayStride = sizeof(SceneVertex);
    layout.stepMode = WGPUVertexStepMode_Vertex;
    layout.attributeCount = attributes.size();
    layout.attributes = attributes.data();

    WGPUColorTargetState color = WGPU_COLOR_TARGET_STATE_INIT;
    color.format = format;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = shader.get();
    fragment.entryPoint = {IsSrgbFormat(format) ? "fsLinear" : "fsDisplay", WGPU_STRLEN};
    fragment.targetCount = 1;
    fragment.targets = &color;

    WGPUDepthStencilState depth = WGPU_DEPTH_STENCIL_STATE_INIT;
    depth.format = kDepthFormat;
    depth.depthCompare = WGPUCompareFunction_Less;
    depth.depthWriteEnabled = WGPUOptionalBool_True;

    WGPURenderPipelineDescriptor description = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    description.label = {"Chapter 02 Lambert and depth", WGPU_STRLEN};
    description.vertex.module = shader.get();
    description.vertex.entryPoint = {"vsWorld", WGPU_STRLEN};
    description.vertex.bufferCount = 1;
    description.vertex.buffers = &layout;
    description.primitive.cullMode = WGPUCullMode_Back;
    description.depthStencil = &depth;
    description.fragment = &fragment;

    wgpuDevicePushErrorScope(gpu.Device(), WGPUErrorFilter_Validation);
    PipelineHandle pipeline(wgpuDeviceCreateRenderPipeline(gpu.Device(), &description));
    gpu.CheckValidation("chapter 02 Lambert pipeline");
    if (!pipeline) {
        throw std::runtime_error("Cannot create chapter 02 render pipeline");
    }
    return pipeline;
}

//————————————————————————————————————————————————————————————————————————————————
// 创建可由 Queue 上传数据的缓冲，并检查分配结果。
//————————————————————————————————————————————————————————————————————————————————
BufferHandle MakeBuffer(WGPUDevice device, WGPUBufferUsage usage, std::uint64_t size, const char* label) {
    WGPUBufferDescriptor description = WGPU_BUFFER_DESCRIPTOR_INIT;
    description.label = {label, WGPU_STRLEN};
    description.usage = usage | WGPUBufferUsage_CopyDst;
    description.size = size;
    BufferHandle buffer(wgpuDeviceCreateBuffer(device, &description));
    if (!buffer) {
        throw std::runtime_error("Cannot create chapter 02 GPU buffer");
    }
    return buffer;
}

}  // namespace

//————————————————————————————————————————————————————————————————————————————————
// 每个模型上传一次，并单独绑定一个 uniform 以支持编辑器变换。
//————————————————————————————————————————————————————————————————————————————————
WorldRenderer::WorldRenderer(WebGpuContext& gpu, WGPUTextureFormat format,
                             const WorldScene& scene, const std::filesystem::path& root)
    : mGpu(gpu), mPipeline(BuildPipeline(gpu, format, root / "shaders" / "world.wgsl")) {
    mClearColor = {0.89, 0.925, 0.941, 1.0};
    if (IsSrgbFormat(format)) {
        mClearColor.r = DisplayToLinear(mClearColor.r);
        mClearColor.g = DisplayToLinear(mClearColor.g);
        mClearColor.b = DisplayToLinear(mClearColor.b);
    }

    BindGroupLayoutHandle layout(wgpuRenderPipelineGetBindGroupLayout(mPipeline.get(), 0));
    if (!layout) {
        throw std::runtime_error("Cannot get chapter 02 uniform layout");
    }
    for (size_t i = 0; i < mDraws.size(); ++i) {
        const auto& object = scene.objects[i];
        auto& draw = mDraws[i];
        const auto vertices = object.meshFile
            ? LoadSceneMesh(root / "models" / "ch02" / object.meshFile)
            : MakeRoamingCarMesh();
        draw.count = static_cast<std::uint32_t>(vertices.size());
        const auto bytes = vertices.size() * sizeof(SceneVertex);
        draw.vertices = MakeBuffer(gpu.Device(), WGPUBufferUsage_Vertex, bytes, object.name);
        wgpuQueueWriteBuffer(gpu.Queue(), draw.vertices.get(), 0, vertices.data(), bytes);
        draw.uniform = MakeBuffer(gpu.Device(), WGPUBufferUsage_Uniform, sizeof(DrawUniform), "World object uniform");

        WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
        entry.buffer = draw.uniform.get();
        entry.size = sizeof(DrawUniform);
        WGPUBindGroupDescriptor binding = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        binding.layout = layout.get();
        binding.entryCount = 1;
        binding.entries = &entry;
        draw.binding.reset(wgpuDeviceCreateBindGroup(gpu.Device(), &binding));
        if (!draw.binding) {
            throw std::runtime_error("Cannot bind chapter 02 object uniform");
        }
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 深度附件的尺寸须与场景颜色纹理完全相同。
//————————————————————————————————————————————————————————————————————————————————
void WorldRenderer::ResizeDepth(PixelSize size) {
    if (mDepthSize == size) {
        return;
    }
    WGPUTextureDescriptor description = WGPU_TEXTURE_DESCRIPTOR_INIT;
    description.label = {"Chapter 02 depth", WGPU_STRLEN};
    description.size = {size.width, size.height, 1};
    description.format = kDepthFormat;
    description.usage = WGPUTextureUsage_RenderAttachment;
    TextureHandle texture(wgpuDeviceCreateTexture(mGpu.Device(), &description));
    if (!texture) {
        throw std::runtime_error("Cannot allocate chapter 02 depth texture");
    }
    TextureViewHandle view(wgpuTextureCreateView(texture.get(), nullptr));
    if (!view) {
        throw std::runtime_error("Cannot create chapter 02 depth view");
    }
    mDepthView = std::move(view);
    mDepthTexture = std::move(texture);
    mDepthSize = size;
}

//————————————————————————————————————————————————————————————————————————————————
// 从当前场景取相机、变换和太阳参数，使用一个深度通道绘制整个世界。
//————————————————————————————————————————————————————————————————————————————————
void WorldRenderer::Encode(WGPUCommandEncoder encoder, WGPUTextureView target, PixelSize size,
                           const WorldScene& scene) {
    ResizeDepth(size);
    const auto& camera = scene.ActiveCamera();
    const glm::mat4 vp = camera.Projection(static_cast<float>(size.width) / size.height) * camera.View();
    const glm::vec4 direction(scene.sun.TowardLight(), 0.0f);
    const glm::vec4 sun(scene.sun.color * scene.sun.intensity, 0.0f);
    auto pass = BeginColorPass(encoder, target, mClearColor, "Chapter 02 world pass", mDepthView.get());
    wgpuRenderPassEncoderSetPipeline(pass.get(), mPipeline.get());
    const std::size_t visibleCount = scene.IsDriving() ? mDraws.size() : WorldScene::kCarIndex;
    for (std::size_t i = 0; i < visibleCount; ++i) {
        const auto& draw = mDraws[i];
        const glm::mat4 model = scene.objects[i].transform.Matrix();
        const DrawUniform uniforms{vp, model, glm::transpose(glm::inverse(model)), direction, sun};
        wgpuQueueWriteBuffer(mGpu.Queue(), draw.uniform.get(), 0, &uniforms, sizeof(uniforms));
        wgpuRenderPassEncoderSetBindGroup(pass.get(), 0, draw.binding.get(), 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(pass.get(), 0, draw.vertices.get(), 0, draw.count * sizeof(SceneVertex));
        wgpuRenderPassEncoderDraw(pass.get(), draw.count, 1, 0, 0);
    }
    wgpuRenderPassEncoderEnd(pass.get());
}

}  // namespace tinyworld
