# TinyWorld

Small Engine, Complete World. 一个从原生空窗口逐步构建到可探索山谷与海岸的 C++20 / WebGPU 教学工程。当前可运行第 01 章和第 02 章。

![第二章场景的 GPU 回读画面，草地上有一间小屋和一棵树](Chapters/ch02/image/scene.png)

## 课程入口

| 章节 | 内容 | 教程 | PDF |
| --- | --- | --- | --- |
| 01 | 从空窗口到第一个三角形 | [从 Step 1 开始](Chapters/ch01/README.md) · [分步源码与工程入口](Chapters/ch01/src/README.md) | [第 01 章](Chapters/ch01/ch01.pdf) |
| 02 | 第一间小屋，第一棵树 | [教程与运行步骤](Chapters/ch02/README.md) | [第 02 章](Chapters/ch02/ch02.pdf) |

[课程总览](COURSE_OVERVIEW.md) · [19 章详细路线](Docs/roadmap-19-chapters.md) · [项目架构](Docs/architecture.md)

## 构建与运行完整工程

按第 01 章准备编译工具、CMake 与 vcpkg，并设置 `VCPKG_ROOT`。也可以安装 Ninja，在 macOS arm64 从仓库根目录使用以下命令。 [Visual Studio 和 Xcode 的工程生成方法](Chapters/ch01/README.md#step-1--用-cmake-打开第一扇窗口)

```sh
cmake --preset macos-arm64-debug
cmake --build --preset macos-arm64-debug
out/build/macos-arm64-debug/bin/TinyWorld --chapter 01
out/build/macos-arm64-debug/bin/TinyWorld --chapter 02
```

第 01 章加 `--direct` 可直接呈现三角形。第 02 章在场景视口内右键拖动可环绕，滚轮可缩放。任一章节加 `--capture output.ppm` 可读取 GPU 绘制结果。

第 02 章点 `Hierarchy` 中的 `Drive car` 或加 `--preset drive` 即可开小车。视口获得焦点后，用 **WASD** 或方向键驾驶，按 **R** 或 **Home** 回到起点。

Windows x64 使用 `windows-x64-debug` 构建 preset，详细环境和启动命令见章节教程。各平台的实测范围记录在对应章节。

### Visual Studio 2022

安装 Visual Studio 2022 的「使用 C++ 的桌面开发」工作负载。按第一章准备 vcpkg 后，在仓库根目录生成解决方案。

```powershell
$env:VCPKG_ROOT = (Resolve-Path .tools\vcpkg).Path
cmake --preset windows-x64-vs2022
```

打开 `out/build/windows-x64-vs2022/TinyWorld.sln`，选择 `Debug | x64`。启动项目是 `TinyWorld`，默认运行第 02 章。在 `Editor/main.cpp` 或 `Engine/Renderer/WorldRenderer.cpp` 设置断点后按 **F5** 调试。按 **Ctrl+F5** 直接运行。要切换章节，在 `TinyWorld` 项目的「属性 → 调试 → 命令参数」中将 `--chapter 02` 改为 `--chapter 01`。

## 目录

- `Engine/` 和 `Editor/` 保存引擎与编辑器源码。
- `Assets/` 保存着色器和课程模型，`Tools/` 保存离线工具。
- `Chapters/` 保存 Markdown 教程、PDF 和插图，`Docs/` 保存路线与技术决策。
- 本机工具装在 `.tools/`，preset 将构建产物写到 `out/`。两个目录都由 Git 忽略。
