#pragma once

#include "Editor/Application.h"
#include "Engine/Scene/WorldScene.h"

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 窗口标题和启动日志随章节预设变化。
//————————————————————————————————————————————————————————————————————————————————
struct ChapterPresentation {
    const char* title;  // GLFW 窗口标题
    const char* ready;  // GPU 管线就绪后写入日志的消息
};

//————————————————————————————————————————————————————————————————————————————————
// 根据章节与命令行选项创建初始场景。
//————————————————————————————————————————————————————————————————————————————————
WorldScene CreateWorldScene(const ApplicationOptions& options, const std::filesystem::path& root);
//————————————————————————————————————————————————————————————————————————————————
// 查找所选章节的窗口标题和就绪消息。
//————————————————————————————————————————————————————————————————————————————————
ChapterPresentation DescribeChapter(const ApplicationOptions& options);

}  // namespace tinyworld
