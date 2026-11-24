#pragma once

#include "Engine/Scene/AtmosphereSettings.h"
#include "Engine/Scene/OrbitCamera.h"
#include "Engine/Scene/PlacedProp.h"
#include "Engine/Scene/RoamingCar.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Scene/WaterSettings.h"
#include "Engine/Terrain/HeightField.h"
#include "Engine/Terrain/TerrainVisibility.h"
#include "Engine/Terrain/MaterialSplat.h"
#include "Engine/Terrain/RoadSpline.h"
#include "Engine/Terrain/Vegetation.h"
#include "Runtime/WalkController.h"

#include <array>
#include <optional>

namespace tinyworld {

enum class WorldPreset { Default, Overlap, Evening, Drive, Walk, Materials, Shore, Porch };  // 布局和游览预设
enum class MaterialView { Lit, Weights, Roughness, VertexColor };  // 地表材质的诊断通道
enum class SkyMode { Cubemap, Panorama };  // 第四章的两种天空投影

//————————————————————————————————————————————————————————————————————————————————
// 场景中一个可选中的静态网格实例。
//————————————————————————————————————————————————————————————————————————————————
struct WorldObject {
    const char* id;        // 文件中的稳定 ID，与面板显示名称独立
    const char* name;      // 层级面板中的显示名称
    const char* asset;     // 相对部署目录的网格路径，或程序生成资产的标识
    Transform transform;  // 物体当前的变换
};

//————————————————————————————————————————————————————————————————————————————————
// 保存方向光的朝向和线性能量，供面板读取和渲染器使用。
//————————————————————————————————————————————————————————————————————————————————
struct DirectionalSun {
    float azimuth{-39.0f};               // 光源水平方位，单位为度
    float elevation{38.0f};              // 光源仰角，单位为度
    float intensity{0.92f};              // 直接光的倍率
    glm::vec3 color{1.0f, 0.95f, 0.86f}; // 线性的太阳 RGB

    //————————————————————————————————————————————————————————————————————————————————
    // 求从表面指向太阳的单位向量，与表面法线做点积。
    //————————————————————————————————————————————————————————————————————————————————
    [[nodiscard]] glm::vec3 TowardLight() const;
};

//————————————————————————————————————————————————————————————————————————————————
// 第十八章的场景时刻与播放状态。存档仅保存时刻。
//————————————————————————————————————————————————————————————————————————————————
struct DayCycleSettings {
    float hour{12.0f};  // 小时，午夜同属周期起点
    bool playing{};     // 拖动时间轴后暂停
};

//————————————————————————————————————————————————————————————————————————————————
// 第三章可编辑的太阳阴影参数。深度偏移用光源投影的 0–1 深度单位。
//————————————————————————————————————————————————————————————————————————————————
struct ShadowSettings {
    static constexpr float kMaxDepthBias = 0.01f;  // 演示阴影脱离时允许的最大偏移
    bool enabled{true};       // 是否绘制并采样 Shadow Map
    bool pcf{true};           // 使用 3×3 比较滤波，关闭后只比较一次
    float depthBias{0.0015f}; // 基础接收深度偏移，随表面倾斜度增加
};

//————————————————————————————————————————————————————————————————————————————————
// 控制背景投影和车身反射。两种天空共用同一份环境内容。
//————————————————————————————————————————————————————————————————————————————————
struct EnvironmentSettings {
    SkyMode sky{SkyMode::Cubemap}; // 背景使用天空盒或全景天空球
    bool carReflection{true};     // 车漆和玻璃是否采样环境 Cubemap
};

//————————————————————————————————————————————————————————————————————————————————
// 第八章材质观察选项，关掉观察后可继续雕刻或步行。
//————————————————————————————————————————————————————————————————————————————————
struct MaterialPreview {
    MaterialView view{MaterialView::Lit};  // 当前显示的地表通道
    bool spheres{};                        // 是否查看固定机位的材质球
};

//————————————————————————————————————————————————————————————————————————————————
// 小屋场景的 CPU 数据。GPU 对象和 ImGui 交互各自由对应模块持有。
//————————————————————————————————————————————————————————————————————————————————
struct WorldScene {
    static constexpr std::array<OrbitCamera, 4> kTerrainRoute{{
        {{-13.0f, 2.0f, 12.0f}, 27.0f, 17.0f, 12.0f},
        {{-2.0f, 0.6f, 5.0f}, 12.0f, 20.0f, 10.0f},
        {{5.0f, 0.7f, -3.0f}, 235.0f, 22.0f, 12.0f},
        {{17.0f, 0.5f, -12.0f}, 225.0f, 22.0f, 10.0f},
    }};  // 西南坡、桥头、河口、海岬的固定机位
    static constexpr OrbitCamera kDayCamera{{9.0f, 1.4f, 1.6f}, -30.0f, 15.0f, 28.0f};
    static constexpr OrbitCamera kPorchCamera{{3.55f, 1.17f, 7.75f}, 24.0f, 22.0f, 9.0f};
    static constexpr OrbitCamera kShoreCamera{{18.4f, -0.55f, -1.8f}, -91.0f, 24.0f, 14.0f};  // 近岸对照机位
    static constexpr std::size_t kGroundIndex = 0;  // 草台是平地步行的参考坐标系
    static constexpr std::size_t kCarIndex = 3;  // 小车排在三件常驻物体后面
    static constexpr std::size_t kObjectCount = kCarIndex + 1;  // 包含小车的最大物体数
    std::array<WorldObject, kObjectCount> objects{{
        {"ground", "Ground", "models/ch02/ground.twmesh", {}},
        {"tree", "Tree", "models/ch02/tree.twmesh", {{2.54f, 0.0f, 0.52f}}},
        {"cottage", "Cottage", "models/ch02/house.twmesh", {{-1.78f, 0.0f, 0.16f}}},
        {"car", "Car", "builtin:car", RoamingCar::kDefaultStart},
    }};                        // 绘制顺序，重叠演示中的树先于房子提交
    OrbitCamera camera;        // 当前相机参数
    DirectionalSun sun;        // 当前太阳参数
    std::optional<ShadowSettings> shadows;  // 仅在支持太阳阴影的章节中设置
    std::optional<EnvironmentSettings> environment;  // 仅在有天空演示的章节中设置
    std::optional<WalkSpawn> spawn;  // 第五章的步行入口，支持持久化
    std::optional<HeightField> terrain;  // 第六章的全局高度样点
    bool showChunkBorders{};  // 是否在颜色通道叠画地形的内部边界
    bool capstone{};                     // 第 21 章允许加载较大的场景布局
    bool streamTerrain{};                 // 第二十章分块准备地形和植被实例
    std::optional<TerrainPreview> terrainPreview; // 第十九章的独立 LOD 和视锥开关
    bool canPreviewLod{};  // 第六章可以切换远处块的教学 LOD
    bool previewLod{};  // 是否按距离使用粗网格
    bool canSculpt{};  // 第七章启用编辑和第二版场景文件
    std::optional<MaterialPreview> materials;  // 第八章的地表材质观察选项
    std::optional<MaterialSplat> paint;  // 第八章的共享 RGBA 权重图
    std::optional<RoadSpline> road;  // 第九章保存道路控制点与宽度
    bool canPlaceProps{};  // 第十章启用物体摆放
    std::vector<PlacedProp> props;  // 保存的手摆资产实例
    bool canUseGltfProps{};  // 第十三章开启 glTF 原型与桥面查询
    std::optional<VegetationSettings> vegetation;  // 由样地或规则生成的植被参数
    std::optional<WaterSettings> water;  // 水面调试及运行时间
    std::optional<DayCycleSettings> dayCycle;  // 第十八章驱动实时太阳与环境光
    std::optional<bool> lightmapEnabled;  // 第十七章开启图集，值本身用于实时对照
    std::optional<AtmosphereSettings> atmosphere;  // 第十六章的雾和阴影距离预览

    //————————————————————————————————————————————————————————————————————————————————
    // 借用可选的高度场。只在本次地面查询或网格生成期间使用。
    //————————————————————————————————————————————————————————————————————————————————
    [[nodiscard]] const HeightField* Terrain() const { return terrain ? &*terrain : nullptr; }

    //————————————————————————————————————————————————————————————————————————————————
    // 重置场景，然后应用指定的演示布局。
    //————————————————————————————————————————————————————————————————————————————————
    void ApplyPreset(WorldPreset preset);

    //————————————————————————————————————————————————————————————————————————————————
    // 雕刻地面并按高度差移动地标，保留原先的离地量。
    //————————————————————————————————————————————————————————————————————————————————
    void SculptTerrain(glm::vec2 center, const TerrainBrush& brush, float seconds);

    //————————————————————————————————————————————————————————————————————————————————
    // 恢复编辑历史中的样点，并保留布景相对地面的高度偏移。
    //————————————————————————————————————————————————————————————————————————————————
    void RestoreTerrain(const HeightField::Samples& samples);

    //————————————————————————————————————————————————————————————————————————————————
    // 进入或退出漫游，保留编辑器相机和三件常驻物体的状态。
    //————————————————————————————————————————————————————————————————————————————————
    void SetDriving(bool active);

    //————————————————————————————————————————————————————————————————————————————————
    // 查询当前是否需要绘制并控制小车。
    //————————————————————————————————————————————————————————————————————————————————
    [[nodiscard]] bool IsDriving() const { return mCar.has_value(); }

    //————————————————————————————————————————————————————————————————————————————————
    // 在漫游期间更新车身变换和相机，时间以秒计。
    //————————————————————————————————————————————————————————————————————————————————
    void UpdateDrive(CarInput input, float deltaSeconds);

    //————————————————————————————————————————————————————————————————————————————————
    // 开始步行时从出生点创建临时角色，结束后回到先前的编辑视角。
    //————————————————————————————————————————————————————————————————————————————————
    void SetWalking(bool active);

    //————————————————————————————————————————————————————————————————————————————————
    // 查询步行模式是否活跃。
    //————————————————————————————————————————————————————————————————————————————————
    [[nodiscard]] bool IsWalking() const { return mWalker.has_value(); }

    //————————————————————————————————————————————————————————————————————————————————
    // 将视口输入交给固定时间步控制器。
    //————————————————————————————————————————————————————————————————————————————————
    void UpdateWalk(WalkInput input, double deltaSeconds);

    //————————————————————————————————————————————————————————————————————————————————
    // 编辑时返回出生预览，步行时返回当前脚底变换。仅在 spawn 有值时调用。
    //————————————————————————————————————————————————————————————————————————————————
    [[nodiscard]] Transform WalkerPose() const;
    [[nodiscard]] WalkGait WalkerGait() const { return mWalker ? mWalker->Gait() : WalkGait{}; }

    //————————————————————————————————————————————————————————————————————————————————
    // 返回当前模式使用的相机。
    //————————————————————————————————————————————————————————————————————————————————
    [[nodiscard]] const OrbitCamera& ActiveCamera() const;

private:
    void UpdateGroundedObjects(const HeightField& previous);
    std::optional<RoamingCar> mCar;  // 仅在漫游模式下保存驾驶和跟随状态
    std::optional<Transform> mParkedCar;  // 第五章进入驾驶前的编辑变换
    std::optional<WalkController> mWalker;  // 仅在步行检查中存在，退出即丢弃
};

}  // namespace tinyworld
