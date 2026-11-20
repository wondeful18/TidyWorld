# TinyWorld

Small Engine, Complete World. 一个从彩色三角形逐步构建到可探索山谷与海岸的 C++20 / WebGPU 教学工程。当前可运行第 01 章和第 02 章。

![第二章场景的 GPU 回读画面，草地上有一间小屋和一棵树](Chapters/ch02/image/scene.png)

## 课程入口

| 章节 | 内容 | 教程 | PDF |
| --- | --- | --- | --- |
| 01 | 一个三角形，两种呈现路径 | [教程与环境准备](Chapters/ch01/README.md) | [第 01 章](Chapters/ch01/ch01.pdf) |
| 02 | 第一间小屋，第一棵树 | [教程与运行步骤](Chapters/ch02/README.md) | [第 02 章](Chapters/ch02/ch02.pdf) |

[课程总览](COURSE_OVERVIEW.md) · [19 章详细路线](Docs/roadmap-19-chapters.md) · [项目架构](Docs/architecture.md)

## 构建与运行

按第 01 章准备 CMake 3.24 或更新版本、Ninja、C++20 编译器与 vcpkg，并设置 `VCPKG_ROOT`。在 macOS arm64 从仓库根目录使用以下命令。

```sh
cmake --preset macos-arm64-debug
cmake --build --preset macos-arm64-debug
out/build/macos-arm64-debug/bin/TinyWorld --chapter 01
out/build/macos-arm64-debug/bin/TinyWorld --chapter 02
```

第 01 章加 `--direct` 可直接呈现三角形。第 02 章在场景视口内右键拖动可环绕，滚轮可缩放。任一章节加 `--capture output.ppm` 可读取 GPU 绘制结果。

Windows x64 使用 `windows-x64-debug` 构建 preset，详细环境和启动命令见章节教程。各平台的实测范围记录在对应章节。

## 目录

- `Engine/` 和 `Editor/` 保存引擎与编辑器源码。
- `Assets/` 保存着色器和课程模型，`Tools/` 保存离线工具。
- `Chapters/` 保存 Markdown 教程、PDF 和插图，`Docs/` 保存路线与技术决策。
- 本机工具装在 `.tools/`，preset 将构建产物写到 `out/`。两个目录都由 Git 忽略。
