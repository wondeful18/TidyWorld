#include "Engine/Renderer/WorldDraw.h"

#include "Engine/Renderer/Color.h"
#include "Engine/Renderer/RenderCoordinates.h"
#include "Engine/Renderer/VegetationRenderer.h"
#include "Engine/Renderer/WebGpuContext.h"

#include <glm/matrix.hpp>

#include <array>
#include <bit>
#include <map>
#include <stdexcept>

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 区分车漆与玻璃，其他网格顶点的环境反射权重为零。
//————————————————————————————————————————————————————————————————————————————————
std::vector<WorldVertex> WithReflectivity(const std::vector<SceneVertex>& geometry,
                                         const CarSurfaceRanges& surfaces) {
    std::vector<WorldVertex> vertices;
    vertices.reserve(geometry.size());
    for (std::size_t i = 0; i < geometry.size(); ++i) {
        float reflection = 0.0f;
        if (i < surfaces.reflectiveEnd) {
            const bool glass = i >= surfaces.glassBegin && i < surfaces.glassEnd;
            reflection = glass ? 1.0f : 0.47f;
        }
        vertices.push_back({geometry[i], reflection});
    }
    return vertices;
}

//————————————————————————————————————————————————————————————————————————————————
// 显式定义物体布局，使太阳深度和颜色管线可共用每个物体的绑定。
//————————————————————————————————————————————————————————————————————————————————
BindGroupLayoutHandle BuildObjectLayout(WebGpuContext& gpu) {
    WGPUBindGroupLayoutEntry uniform = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    uniform.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    uniform.buffer.type = WGPUBufferBindingType_Uniform;
    uniform.buffer.minBindingSize = sizeof(DrawUniform);

    WGPUBindGroupLayoutDescriptor description = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    description.entryCount = 1;
    description.entries = &uniform;
    wgpuDevicePushErrorScope(gpu.Device(), WGPUErrorFilter_Validation);
    BindGroupLayoutHandle layout(wgpuDeviceCreateBindGroupLayout(gpu.Device(), &description));
    gpu.CheckValidation("world object bind group layout");
    if (!layout) {
        throw std::runtime_error("Cannot create world object layout");
    }
    return layout;
}

//————————————————————————————————————————————————————————————————————————————————
// 创建相机颜色管线。第三章额外绑定太阳深度图，第二章沿用 Lambert 入口。
//————————————————————————————————————————————————————————————————————————————————
PipelineHandle BuildPipeline(WebGpuContext& gpu, WGPUTextureFormat format, WGPUShaderModule shader,
                             WGPUBindGroupLayout objects, WGPUBindGroupLayout shadows,
                             WGPUBindGroupLayout environment, WGPUBindGroupLayout materials,
                             bool vegetation, bool gltf) {
    const std::array layouts{objects, shadows, environment, materials};
    WGPUPipelineLayoutDescriptor pipelineLayout = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    pipelineLayout.bindGroupLayoutCount = materials ? 4 : (environment ? 3 : (shadows ? 2 : 1));
    pipelineLayout.bindGroupLayouts = layouts.data();

    std::array<WGPUVertexAttribute, 6> attributes{};
    constexpr auto geometry = offsetof(WorldVertex, geometry);
    constexpr std::array offsets{geometry + offsetof(SceneVertex, position),
                                 geometry + offsetof(SceneVertex, normal),
                                 geometry + offsetof(SceneVertex, color),
                                 offsetof(WorldVertex, reflectivity), offsetof(WorldVertex, uv),
                                 offsetof(WorldVertex, lightmapUv)};
    for (std::uint32_t i = 0; i < attributes.size(); ++i) {
        attributes[i] = WGPU_VERTEX_ATTRIBUTE_INIT;
        attributes[i].format = WGPUVertexFormat_Float32x3;
        if (i == 3) attributes[i].format = WGPUVertexFormat_Float32;
        if (i >= 4) attributes[i].format = WGPUVertexFormat_Float32x2;
        attributes[i].offset = offsets[i];
        attributes[i].shaderLocation = i;
    }
    WGPUVertexBufferLayout layout = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
    layout.arrayStride = sizeof(WorldVertex);
    layout.stepMode = WGPUVertexStepMode_Vertex;
    layout.attributeCount = gltf ? attributes.size() : (environment ? 4 : 3);
    layout.attributes = attributes.data();

    WGPUColorTargetState color = WGPU_COLOR_TARGET_STATE_INIT;
    color.format = format;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = shader;
    const char* entry = "fsWorld";
    if (shadows) {
        entry = "fsShadowed";
    }
    if (environment) {
        entry = "fsEnvironment";
    }
    if (materials) {
        entry = "fsMaterials";
    }
    if (gltf) {
        entry = "fsGltf";
    }
    if (vegetation) {
        entry = "fsVegetation";
    }
    fragment.entryPoint = {entry, WGPU_STRLEN};
    const auto encoding = UnormEncoding(format);
    fragment.constantCount = 1;
    fragment.constants = &encoding;
    fragment.targetCount = 1;
    fragment.targets = &color;

    WGPUDepthStencilState depth = WGPU_DEPTH_STENCIL_STATE_INIT;
    depth.format = kDepthFormat;
    depth.depthCompare = WGPUCompareFunction_Less;
    depth.depthWriteEnabled = WGPUOptionalBool_True;

    WGPURenderPipelineDescriptor description = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    description.label = {vegetation ? "Vegetation color pipeline" : "World color pipeline", WGPU_STRLEN};
    description.vertex.module = shader;
    const char* vertexEntry = shadows ? "vsShadowedWorld" : "vsWorld";
    if (environment) vertexEntry = "vsEnvironmentWorld";
    if (gltf) vertexEntry = "vsGltf";
    description.vertex.entryPoint = {vertexEntry, WGPU_STRLEN};
    description.vertex.bufferCount = 1;
    description.vertex.buffers = &layout;
    description.primitive.cullMode = WGPUCullMode_Back;
    const VegetationVertexLayout vegetationLayout;
    if (vegetation) {
        description.vertex.entryPoint = {"vsVegetation", WGPU_STRLEN};
        description.vertex.bufferCount = vegetationLayout.buffers.size();
        description.vertex.buffers = vegetationLayout.buffers.data();
        description.primitive.cullMode = WGPUCullMode_None;
    }
    description.depthStencil = &depth;
    description.fragment = &fragment;

    wgpuDevicePushErrorScope(gpu.Device(), WGPUErrorFilter_Validation);
    PipelineLayoutHandle bindLayouts(wgpuDeviceCreatePipelineLayout(gpu.Device(), &pipelineLayout));
    description.layout = bindLayouts.get();
    PipelineHandle pipeline(bindLayouts ? wgpuDeviceCreateRenderPipeline(gpu.Device(), &description) : nullptr);
    gpu.CheckValidation("world color pipeline");
    if (!bindLayouts || !pipeline) {
        throw std::runtime_error("Cannot create world color pipeline");
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
        throw std::runtime_error("Cannot create world GPU buffer");
    }
    return buffer;
}

//————————————————————————————————————————————————————————————————————————————————
// 同一上传路径接收房屋、小车、机器人和地形的顶点数据。
//————————————————————————————————————————————————————————————————————————————————
Drawable UploadMesh(WebGpuContext& gpu, std::span<const WorldVertex> vertices,
                                                   WGPUBindGroupLayout layout, const char* name) {
    Drawable draw;
    if (layout) draw = MakeDrawBinding(gpu, layout);
    draw.count = static_cast<std::uint32_t>(vertices.size());
    const auto bytes = vertices.size_bytes();
    draw.vertices = MakeBuffer(gpu.Device(), WGPUBufferUsage_Vertex, bytes, name);
    wgpuQueueWriteBuffer(gpu.Queue(), draw.vertices.get(), 0, vertices.data(), bytes);
    return draw;
}

Drawable MakeDrawBinding(WebGpuContext& gpu, WGPUBindGroupLayout layout) {
    Drawable draw;
    draw.uniform = MakeBuffer(gpu.Device(), WGPUBufferUsage_Uniform, sizeof(DrawUniform), "World object uniform");

    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.buffer = draw.uniform.get();
    entry.size = sizeof(DrawUniform);
    WGPUBindGroupDescriptor binding = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    binding.layout = layout;
    binding.entryCount = 1;
    binding.entries = &entry;
    draw.binding.reset(wgpuDeviceCreateBindGroup(gpu.Device(), &binding));
    if (!draw.binding) {
        throw std::runtime_error("Cannot bind world object uniform");
    }
    return draw;
}

//————————————————————————————————————————————————————————————————————————————————
// 深度附件的尺寸须与场景颜色纹理完全相同。
//————————————————————————————————————————————————————————————————————————————————
void DepthTarget::Resize(WebGpuContext& gpu, PixelSize size) {
    if (mSize == size) {
        return;
    }
    WGPUTextureDescriptor description = WGPU_TEXTURE_DESCRIPTOR_INIT;
    description.label = {"Chapter 02 depth", WGPU_STRLEN};
    description.size = {size.width, size.height, 1};
    description.format = kDepthFormat;
    description.usage = WGPUTextureUsage_RenderAttachment;
    description.usage |= WGPUTextureUsage_TextureBinding;
    TextureHandle texture(wgpuDeviceCreateTexture(gpu.Device(), &description));
    if (!texture) {
        throw std::runtime_error("Cannot allocate chapter 02 depth texture");
    }
    TextureViewHandle view(wgpuTextureCreateView(texture.get(), nullptr));
    if (!view) {
        throw std::runtime_error("Cannot create chapter 02 depth view");
    }
    mView = std::move(view);
    mTexture = std::move(texture);
    mSize = size;
}

//————————————————————————————————————————————————————————————————————————————————
// 原型只上传一次，颜色和深度通道共享同一张索引表。
//————————————————————————————————————————————————————————————————————————————————
Drawable UploadIndexedMesh(WebGpuContext& gpu, std::span<const WorldVertex> vertices,
                           std::span<const std::uint32_t> indices, WGPUBindGroupLayout layout, const char* name) {
    auto mesh = UploadMesh(gpu, vertices, layout, name);
    mesh.indices = MakeBuffer(gpu.Device(), WGPUBufferUsage_Index, indices.size_bytes(), "Model indices");
    wgpuQueueWriteBuffer(gpu.Queue(), mesh.indices.get(), 0, indices.data(), indices.size_bytes());
    mesh.indexCount = static_cast<std::uint32_t>(indices.size());
    return mesh;
}

//————————————————————————————————————————————————————————————————————————————————
// 只有位置、法线、颜色与 UV 全部相同才合并，硬边和材质边界仍保留独立顶点。
//————————————————————————————————————————————————————————————————————————————————
Drawable UploadIndexedMesh(WebGpuContext& gpu, std::span<const WorldVertex> triangles,
                           WGPUBindGroupLayout layout, const char* name) {
    using VertexBits = std::array<std::uint32_t, 14>;
    std::map<VertexBits, std::uint32_t> unique;
    std::vector<WorldVertex> vertices;
    std::vector<std::uint32_t> indices;
    indices.reserve(triangles.size());
    for (const auto& vertex : triangles) {
        const auto bits = std::bit_cast<VertexBits>(vertex);
        const auto [entry, inserted] = unique.try_emplace(bits, static_cast<std::uint32_t>(vertices.size()));
        if (inserted) vertices.push_back(vertex);
        indices.push_back(entry->second);
    }
    return UploadIndexedMesh(gpu, vertices, indices, layout, name);
}

//————————————————————————————————————————————————————————————————————————————————
// 提交与一个对象或地形块对应的三角形列表。
//————————————————————————————————————————————————————————————————————————————————
void DrawMesh(WGPURenderPassEncoder pass, const Drawable& mesh, WGPUBindGroup instance) {
    wgpuRenderPassEncoderSetBindGroup(pass, 0, instance ? instance : mesh.binding.get(), 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, mesh.vertices.get(), 0, mesh.count * sizeof(WorldVertex));
    if (mesh.indices) {
        wgpuRenderPassEncoderSetIndexBuffer(pass, mesh.indices.get(), WGPUIndexFormat_Uint32, 0,
                                            mesh.indexCount * sizeof(std::uint32_t));
        wgpuRenderPassEncoderDrawIndexed(pass, mesh.indexCount, 1, 0, 0, 0);
    } else {
        wgpuRenderPassEncoderDraw(pass, mesh.count, 1, 0, 0);
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 光栅化采用相对模型矩阵，光照继续使用地图内的模型矩阵。
//————————————————————————————————————————————————————————————————————————————————

void DrawUniformWriter::Set(const Drawable& draw, const glm::mat4& model, const glm::vec4& shadows,
                             const SurfaceParameters& surface) const {
    auto values = frame;
    values.renderModel = coordinates.Model(model);
    values.model = model;
    values.normalToWorld = glm::transpose(glm::inverse(model));
    values.shadowControls = shadows;
    values.material = surface;
    wgpuQueueWriteBuffer(queue, draw.uniform.get(), 0, &values, sizeof(values));
}

}  // namespace tinyworld
