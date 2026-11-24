#pragma once

#include "Engine/Platform/Window.h"
#include "Engine/Renderer/GltfMaterials.h"
#include "Engine/Renderer/SkyRenderer.h"
#include "Engine/Renderer/SunShadowMap.h"
#include "Engine/Renderer/SurfaceMaterials.h"
#include "Engine/Renderer/VegetationRenderer.h"
#include "Engine/Renderer/WaterRenderer.h"
#include "Engine/Renderer/WgpuHandle.h"
#include "Engine/Renderer/WorldVertex.h"
#include "Engine/Scene/RobotMesh.h"
#include "Engine/Scene/WorldScene.h"
#include "Engine/Terrain/ChunkStreamer.h"

#include <array>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace tinyworld {

class WebGpuContext;

//————————————————————————————————————————————————————————————————————————————————
// 持有小屋场景的网格、颜色管线和相机深度目标。
// 第三章在颜色通道之前绘制太阳深度图。
//————————————————————————————————————————————————————————————————————————————————
class WorldRenderer {
public:
    //————————————————————————————————————————————————————————————————————————————————
    // 从可执行文件邻近的资源目录载入网格和 WGSL，建立 GPU 状态。
    //————————————————————————————————————————————————————————————————————————————————
    WorldRenderer(WebGpuContext& gpu, WGPUTextureFormat colorFormat,
                  const WorldScene& scene, const std::filesystem::path& assetRoot);
    [[nodiscard]] MaterialPreviews PaintPreviews() const {
        return mMaterials ? mMaterials->PreviewViews() : MaterialPreviews{};
    }
    [[nodiscard]] const TerrainStreamStats& StreamStatistics() const { return mStreamStats; }
    [[nodiscard]] bool StreamingSettled() const;
    void WaitForChunkData();
    [[nodiscard]] const TerrainDrawStats& TerrainStatistics() const { return mTerrainStats; }
    [[nodiscard]] const VegetationDrawStats& VegetationStatistics() const { return mVegetationStats; }

    //————————————————————————————————————————————————————————————————————————————————
    // 按目标像素尺寸准备相机深度缓冲，再编码本帧需要的通道。
    //————————————————————————————————————————————————————————————————————————————————
    void Encode(WGPUCommandEncoder encoder, WGPUTextureView target, PixelSize size,
                const WorldScene& scene);

private:
    //————————————————————————————————————————————————————————————————————————————————
    // 一个物体对应常驻的几何和独立的 uniform。
    //————————————————————————————————————————————————————————————————————————————————
    struct Drawable {
        BufferHandle vertices;    // GPU 顶点缓冲，启动时上传一次
        BufferHandle uniform;     // 该物体单独使用的每帧常量缓冲
        BindGroupHandle binding;  // 将 uniform 连到着色器绑定槽
        std::uint32_t count{};    // 三角形列表的顶点数
    };

    //————————————————————————————————————————————————————————————————————————————————
    // 仅在颜色目标的像素尺寸变化时替换深度纹理与视图。
    //————————————————————————————————————————————————————————————————————————————————
    void ResizeDepth(PixelSize size);

    //————————————————————————————————————————————————————————————————————————————————
    // 上传一个顶点列表，建立与颜色、太阳深度通道共用的物体绑定。
    //————————————————————————————————————————————————————————————————————————————————
    [[nodiscard]] Drawable UploadMesh(std::span<const WorldVertex> vertices, WGPUBindGroupLayout layout,
                                      const char* name);
    [[nodiscard]] Drawable MakeDrawBinding(WGPUBindGroupLayout layout);
    //————————————————————————————————————————————————————————————————————————————————
    // 按场景实例数分配每物体 uniform，几何与材质一直由原型持有。
    //————————————————————————————————————————————————————————————————————————————————
    void SyncProps(std::size_t count);

    //————————————————————————————————————————————————————————————————————————————————
    // 上传课程的 glTF 原型。场景重新打开时只需要交换实例记录。
    //————————————————————————————————————————————————————————————————————————————————
    void LoadGltfProps(const std::filesystem::path& root, bool withLightmaps);

    //————————————————————————————————————————————————————————————————————————————————
    // 阴影通道提交全部物体。颜色通道把内置和 glTF 物体分批调用。
    //————————————————————————————————————————————————————————————————————————————————
    enum class PropPass { BuiltinColor, GltfColor, Shadows };
    void DrawProps(WGPURenderPassEncoder pass, const std::vector<PlacedProp>& props, PropPass type) const;

    //————————————————————————————————————————————————————————————————————————————————
    // 高度变化后刷新受影响的固定大小顶点缓冲。
    //————————————————————————————————————————————————————————————————————————————————
    void SyncTerrain(const HeightField& field, bool showBorders, bool paintTrail);
    void SyncTerrainBorders(const HeightField& field, bool showBorders);
    void SyncStreamedTerrain(const WorldScene& scene);
    [[nodiscard]] std::uint32_t DrawVegetation(WGPURenderPassEncoder pass, bool instanced, bool shadows) const;

    //————————————————————————————————————————————————————————————————————————————————
    // 路线或所贴地形变化时重新裁路面；几何由颜色通道使用。
    //————————————————————————————————————————————————————————————————————————————————
    void SyncRoad(const HeightField& field, const RoadSpline& road);

    //————————————————————————————————————————————————————————————————————————————————
    // 提交地形块与已上传变换的物体，供两个绘制通道共用。
    //————————————————————————————————————————————————————————————————————————————————
    void DrawObjects(WGPURenderPassEncoder pass, std::size_t visibleCount, bool showSpheres, bool shadows);

    //————————————————————————————————————————————————————————————————————————————————
    // 绑定一个网格的顶点与 uniform，提交一次非索引绘制。
    //————————————————————————————————————————————————————————————————————————————————
    static void DrawMesh(WGPURenderPassEncoder pass, const Drawable& mesh, WGPUBindGroup instance = nullptr);

    //————————————————————————————————————————————————————————————————————————————————
    // 原型的一个三角形批次，带与其他实例共享的材质绑定。
    //————————————————————————————————————————————————————————————————————————————————
    struct GltfDraw {
        Drawable mesh;             // 原型顶点和三角形顶点数
        std::size_t material{};    // 对应同一原型的 GPU 材质索引
    };

    //————————————————————————————————————————————————————————————————————————————————
    // 一份 glTF 原型的数据，多次摆放也只上传一次。
    //————————————————————————————————————————————————————————————————————————————————
    struct GltfPrototype {
        std::vector<GltfDraw> meshes;           // 按材质划分的网格批次
        std::vector<BindGroupHandle> materials; // 底色、金属粗糙度与系数
    };

    WebGpuContext& mGpu;                                   // 借用 GPU 上下文，覆盖所有资源的生命期
    BindGroupLayoutHandle mObjectLayout;                    // 保留动态路面上传所需的物体布局
    std::optional<SunShadowMap> mShadowMap;                // 仅由支持阴影的章节创建
    std::optional<SkyRenderer> mSky;                       // 第四章背景与共享环境纹理
    std::optional<WaterRenderer> mWater;  // 读取当前相机深度的半透明水面
    std::optional<SurfaceMaterials> mMaterials;            // 第八章底色与粗糙度数组
    std::optional<VegetationRenderer> mVegetation;
    Drawable mVegetationUniform;
    PipelineHandle mVegetationPipeline;
    VegetationDrawStats mVegetationStats;
    PipelineHandle mPipeline;                             // 方向光与深度测试管线
    std::array<Drawable, WorldScene::kObjectCount + 1> mDraws;  // 场景物体与步行角色的绘制资源
    std::array<Drawable, kRobotLimbCount> mRobotLimbs;
    Drawable mRock;
    Drawable mCabin;
    std::vector<Drawable> mPropDraws;  // 手摆实例独立的世界变换常量
    std::optional<GltfMaterials> mGltfMaterials;  // 第十三章导入材质布局与采样器
    PipelineHandle mGltfPipeline;    // 含 UV 输入并复用 GGX 的颜色管线
    std::array<GltfPrototype, kPropAssets.size() - kBuiltinPropCount> mGltfProps;  // 课程 glTF 资产原型
    std::vector<Drawable> mTerrainDraws;                    // 每个整数块键对应一次绘制
    std::vector<Drawable> mCoarseTerrainDraws;              // 第六章同一批块的 LOD1 网格
    std::array<TerrainBounds, HeightField::kChunkCount> mTerrainBounds{}; // 两档网格共用的包围盒
    std::array<bool, HeightField::kChunkCount> mTerrainVisible{}; // 仅限制相机通道
    TerrainDrawStats mTerrainStats;
    TerrainStreamStats mStreamStats;
    std::array<std::size_t, HeightField::kChunkCount> mChunkBytes{}; // 地形顶点和植被实例的提交字节
    std::optional<ChunkStreamer> mStream;
    std::array<Drawable, HeightField::kChunkCount> mStreamRoadDraws;
    std::array<bool, HeightField::kChunkCount> mCoarseChoice{};  // 本帧两个通道共用的等级
    std::array<Drawable, 4> mMaterialSpheres;                // 成对比较粗糙度与金属度的球
    std::optional<Drawable> mChunkBorders;                  // 仅供颜色通道绘制的边界提示
    std::array<std::uint64_t, HeightField::kChunkCount> mChunkRevisions{};  // 缓存的每块高度修订号
    std::uint64_t mBorderRevision{};                        // 贴地边界使用的高度修订号
    std::optional<Drawable> mRoadMesh;                      // 路面三角形及绘制资源
    std::optional<RoadSpline> mLastRoad;                     // 最近一次生成几何时的控制点和宽度
    std::uint64_t mRoadTerrainRevision{};                    // 最近一次裁剪时的高度修订号
    TextureHandle mDepthTexture;                          // 深度纹理的拥有引用
    TextureViewHandle mDepthView;                         // 当前深度附件的视图
    PixelSize mDepthSize{};                                // 深度目标的像素宽高
    WGPUColor mClearColor{};                               // 与颜色目标编码匹配的背景色
};

}  // namespace tinyworld
