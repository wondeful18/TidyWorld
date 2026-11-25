# TinyWorld · 第 16 章 · 用阳光和雾做远近层次

本分支构建出的 TinyWorld 默认打开第 16 章。也可显式传入 \`--chapter 16\`。

先按 [构建说明](https://github.com/wondeful18/TidyWorld/blob/main/README.md#构建与运行完整工程) 配置 CMake 和 vcpkg。Windows 在 Visual Studio 的 x64 开发者终端中用 \`windows-x64-debug\`，macOS 用 \`macos-arm64-debug\`。

[第 16 章讲义](https://github.com/wondeful18/TidyWorld/blob/main/Chapters/ch16/README.md)

## 源码入口

- [Engine/Renderer/FrameAtmosphere.h](Engine/Renderer/FrameAtmosphere.h)
- [Assets/Shaders/fog.wgsl](Assets/Shaders/fog.wgsl)
- [Engine/Renderer/WorldRenderer.cpp](Engine/Renderer/WorldRenderer.cpp)
