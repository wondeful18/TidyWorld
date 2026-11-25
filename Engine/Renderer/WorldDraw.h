#pragma once

#include "Engine/Platform/Window.h"
#include "Engine/Renderer/FrameAtmosphere.h"
#include "Engine/Renderer/WgpuHandle.h"
#include "Engine/Renderer/WorldVertex.h"
#include "Engine/Scene/RoamingCarMesh.h"

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <span>
#include <vector>

namespace tinyworld {

class WebGpuContext;
class RenderCoordinates;

inline constexpr auto kDepthFormat = WGPUTextureFormat_Depth24Plus; // 相机通道的深度格式

//————————————————————————————————————————————————————————————————————————————————
// 一个可绘制网格及其物体绑定。有索引的原型持有额外索引缓冲。
//————————————————————————————————————————————————————————————————————————————————
struct Drawable {
    BufferHandle vertices;    // GPU 顶点缓冲，启动时上传一次
    BufferHandle indices;     // glTF 原型自有索引缓冲
    std::uint32_t indexCount{}; // glTF 使用的索引数量
    BufferHandle uniform;     // 该物体单独使用的每帧常量缓冲
    BindGroupHandle binding;  // 将 uniform 连到着色器绑定槽
    std::uint32_t count{};    // 三角形列表的顶点数
};

//————————————————————————————————————————————————————————————————————————————————
// 按 WGSL 对齐顺序打包每个物体的矩阵与光照参数。
//————————————————————————————————————————————————————————————————————————————————
struct SurfaceParameters {
    std::uint32_t terrainMode{};  // 0 使用顶点颜色，其他模式见 MaterialView
    float showTrail{};            // 是否混入教学小径的碎石权重
    std::uint32_t roadSurface{}; // 非零表示碎石、土路或沥青
    std::uint32_t useSampleMaterial{}; // 材质球使用下方的测试参数
    glm::vec2 splatOrigin{0.0f};  // 权重纹理左上角的世界 XZ，单位为米
    float splatExtent{1.0f};      // 权重纹理覆盖的边长，单位为米
    float bakedIrradiance{};     // 光照图的线性辐照度范围
    float sampleMetallic{};      // 材质球金属度
    float sampleRoughness{};     // 材质球粗糙度
    float emission{};           // 提灯模型的自发光倍率，普通表面为 0
    float padding{};            // 填齐 WGSL 的对齐要求
};

struct DrawUniform {
    glm::mat4 viewProjection;  // 将世界坐标转换到 WebGPU 裁剪空间
    glm::mat4 renderModel;     // 相机投影使用的模型矩阵
    glm::mat4 model;           // 将模型顶点转换到世界空间
    glm::mat4 normalToWorld;   // 模型矩阵的逆转置，处理非均匀缩放
    glm::vec4 towardSun;       // 从表面指向太阳的世界方向
    glm::vec4 sunRadiance;     // 线性灯光颜色乘以强度
    glm::vec4 shadowControls;      // 依次为阴影开关、PCF 开关、基础深度偏移和保留槽
    glm::vec4 cameraAndReflection; // xyz 为世界相机位置，w 为车身反射开关
    SurfaceParameters material;            // 材质类别和对应的参数
    glm::vec4 coast;               // 海岸开关、水位 Y、岸坡过渡的 X 起点和终点
    FrameAtmosphere air;           // 物体、植被与水面共用的距离参数
    glm::vec4 lighting;             // 环境亮度、夜景混合、保留 HDR 能量、法线贴图开关
    glm::vec4 lampPositionAndPower{0.0f}; // xyz 为光源世界坐标，w 为相对功率
    glm::vec4 lampForwardAndCone{0.0f};   // xyz 为出光方向，w 为外锥余弦，点光用 -1
};

static_assert(sizeof(DrawUniform) == 464);

//————————————————————————————————————————————————————————————————————————————————
// 按统一布局创建物体绑定与颜色管线。
//————————————————————————————————————————————————————————————————————————————————
BindGroupLayoutHandle BuildObjectLayout(WebGpuContext& gpu);
PipelineHandle BuildPipeline(WebGpuContext& gpu, WGPUTextureFormat format, WGPUShaderModule shader,
                             WGPUBindGroupLayout objects, WGPUBindGroupLayout shadows,
                             WGPUBindGroupLayout environment, WGPUBindGroupLayout materials,
                             bool vegetation = false, bool gltf = false);

//————————————————————————————————————————————————————————————————————————————————
// 创建缓冲与网格，绘制时可替换共享原型的实例绑定。
//————————————————————————————————————————————————————————————————————————————————

BufferHandle MakeBuffer(WGPUDevice device, WGPUBufferUsage usage, std::uint64_t size, const char* label);
std::vector<WorldVertex> WithReflectivity(const std::vector<SceneVertex>& geometry,
                                          const CarSurfaceRanges& surfaces = {});
Drawable MakeDrawBinding(WebGpuContext& gpu, WGPUBindGroupLayout layout);
Drawable UploadMesh(WebGpuContext& gpu, std::span<const WorldVertex> vertices, WGPUBindGroupLayout layout, const char* name);
//————————————————————————————————————————————————————————————————————————————————
// glTF 可直接保留索引，程序网格则合并所有属性相同的顶点。
//————————————————————————————————————————————————————————————————————————————————
Drawable UploadIndexedMesh(WebGpuContext& gpu, std::span<const WorldVertex> vertices,
                           std::span<const std::uint32_t> indices, WGPUBindGroupLayout layout, const char* name);
Drawable UploadIndexedMesh(WebGpuContext& gpu, std::span<const WorldVertex> triangles,
                           WGPUBindGroupLayout layout, const char* name);
void DrawMesh(WGPURenderPassEncoder pass, const Drawable& mesh, WGPUBindGroup instance = nullptr);

//————————————————————————————————————————————————————————————————————————————————
// 将每帧共享的光照和相机状态写入每个 DrawUniform。
//————————————————————————————————————————————————————————————————————————————————

struct DrawUniformWriter {
    WGPUQueue queue;                        // 当前帧的上传队列
    const RenderCoordinates& coordinates;   // 当前帧的坐标选择，调用期间有效
    DrawUniform frame;                      // 每个物体共享的光照与相机值

    void Set(const Drawable& draw, const glm::mat4& model, const glm::vec4& shadows,
             const SurfaceParameters& surface = {}) const;
};

//————————————————————————————————————————————————————————————————————————————————
// 在尺寸变化时重建相机深度纹理。
//————————————————————————————————————————————————————————————————————————————————

class DepthTarget {
public:
    void Resize(WebGpuContext& gpu, PixelSize size);
    [[nodiscard]] WGPUTextureView View() const { return mView.get(); }

private:
    TextureHandle mTexture;  // 深度附件的拥有引用
    TextureViewHandle mView; // 管线借用的深度视图
    PixelSize mSize{};       // 当前附件的像素大小
};

}  // namespace tinyworld
