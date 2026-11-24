#pragma once

#include "Engine/Scene/WorldScene.h"

#include <filesystem>
#include <optional>

namespace tinyworld {

enum class Chapter { Triangle, World, SunShadows, Sky, Walk, Terrain, Sculpt, Materials, Road, Props, Meadow, Ecology, Village, Water, Coast, Atmosphere };

//————————————————————————————————————————————————————————————————————————————————
// 命令行选择演示内容、输出路径和验证方式。
//————————————————————————————————————————————————————————————————————————————————
struct ApplicationOptions {
    Chapter chapter{Chapter::Triangle};             // 默认维持第一章启动行为
    bool direct{};                                  // 跳过 ImGui，直接画入输出目标
    bool smokeTest{};                               // 检查真实窗口的尺寸变化与恢复
    bool benchmarkVegetation{};                     // 离屏对比逐个绘制与实例化的开销
    bool chunkBorders{};                            // 第六章启动时显示内部块边界
    bool lodPreview{};                               // 第六章按距离切换地形网格
    bool sculptTools{};                             // 进入地形编辑工具
    std::filesystem::path capturePath;               // 非空时回读到指定 PPM 路径
    std::filesystem::path scenePath;                 // 第五章可在启动时读取的场景文件
    std::filesystem::path roadMaskPath;              // 第九章导出遮罩后直接退出的文件路径
    WorldPreset worldPreset{WorldPreset::Default};  // 小屋场景的初始布局和日照
    std::optional<ShadowSettings> shadowOverrides;   // 第三、四章命令行提供的阴影覆盖值
    std::optional<float> waterTime;                 // 第十四章开始时的流水时间，单位为秒
    std::optional<WaterView> waterView;             // 第十四章固定机位对照的显示模式
    std::optional<bool> fogEnabled;                 // 第十六章固定机位的雾开关
    std::optional<float> shadowDistance;            // 第十六章阴影完全淡出的相机距离，米
    std::optional<EnvironmentSettings> environmentOverrides; // 第四章的背景模式和反射开关
};

//————————————————————————————————————————————————————————————————————————————————
// 启动指定章节，复用窗口、GPU 与绘制主循环。
//————————————————————————————————————————————————————————————————————————————————
int RunApplication(const ApplicationOptions& options);

}  // namespace tinyworld
