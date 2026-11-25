#pragma once

#include "Engine/Scene/WorldScene.h"

#include <filesystem>

namespace tinyworld {

enum class Chapter { Triangle, World };  // 同一应用保留两个可独立复现的章节入口

//————————————————————————————————————————————————————————————————————————————————
// 命令行选择演示内容、输出路径和验证方式。
//————————————————————————————————————————————————————————————————————————————————
struct ApplicationOptions {
    Chapter chapter{Chapter::World};  // 本分支的默认演示
    bool direct{};                                  // 跳过 ImGui，直接画入输出目标
    bool smokeTest{};                               // 检查真实窗口的尺寸变化与恢复
    std::filesystem::path capturePath;               // 非空时回读到指定 PPM 路径
    WorldPreset worldPreset{WorldPreset::Default};  // 第二章初始布局和日照
};

//————————————————————————————————————————————————————————————————————————————————
// 启动指定章节，复用窗口、GPU 与绘制主循环。
//————————————————————————————————————————————————————————————————————————————————
int RunApplication(const ApplicationOptions& options);

}  // namespace tinyworld
