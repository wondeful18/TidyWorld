# TinyWorld · 第 04 章 · 天空盒与环境贴图

本分支构建出的 TinyWorld 默认打开第 04 章。也可显式传入 \`--chapter 04\`。

先按 [构建说明](https://github.com/wondeful18/TidyWorld/blob/main/README.md#构建与运行完整工程) 配置 CMake 和 vcpkg。Windows 在 Visual Studio 的 x64 开发者终端中用 \`windows-x64-debug\`，macOS 用 \`macos-arm64-debug\`。

[第 04 章讲义](https://github.com/wondeful18/TidyWorldBook/blob/main/Chapters/ch04/README.md)

## 源码入口

- [Engine/Renderer/EnvironmentMap.cpp](Engine/Renderer/EnvironmentMap.cpp)
- [Engine/Renderer/SkyRenderer.cpp](Engine/Renderer/SkyRenderer.cpp)
- [Assets/Shaders/sky.wgsl](Assets/Shaders/sky.wgsl)
