#pragma once

#include "Engine/Scene/OrbitCamera.h"
#include "Engine/Scene/Transform.h"

#include <array>

namespace tinyworld {

enum class WorldPreset { Default, Overlap, Evening };  // 第二章的演示场景

//————————————————————————————————————————————————————————————————————————————————
// 场景中一个可选中的静态网格实例。
//————————————————————————————————————————————————————————————————————————————————
struct WorldObject {
    const char* name;      // 层级面板中的显示名称
    const char* meshFile;  // 构建时部署的顶点数据文件名
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
// 第二章的完整场景数据。GPU 对象和 ImGui 交互各自由对应模块持有。
//————————————————————————————————————————————————————————————————————————————————
struct WorldScene {
    static constexpr std::size_t kObjectCount = 3; // 地面、树、房子各一个实例
    std::array<WorldObject, kObjectCount> objects{{
        {"Ground", "ground.twmesh", {}},
        {"Tree", "tree.twmesh", {{2.54f, 0.0f, 0.52f}}},
        {"Cottage", "house.twmesh", {{-1.78f, 0.0f, 0.16f}}},
    }};                        // 绘制顺序，重叠演示中的树先于房子提交
    OrbitCamera camera;        // 当前相机参数
    DirectionalSun sun;        // 当前太阳参数

    //————————————————————————————————————————————————————————————————————————————————
    // 重置场景，然后应用指定的演示布局。
    //————————————————————————————————————————————————————————————————————————————————
    void ApplyPreset(WorldPreset preset);
};

}  // namespace tinyworld
