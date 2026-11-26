# TinyWorld · 第 17 章 · 手写简单的光照贴图烘焙

本分支构建出的 TinyWorld 默认打开第 17 章。也可显式传入 \`--chapter 17\`。

先按 [构建说明](https://github.com/wondeful18/TidyWorld/blob/main/README.md#构建与运行完整工程) 配置 CMake 和 vcpkg。Windows 在 Visual Studio 的 x64 开发者终端中用 \`windows-x64-debug\`，macOS 用 \`macos-arm64-debug\`。

[第 17 章讲义](https://github.com/wondeful18/TidyWorldBook/blob/main/Chapters/ch17/README.md)

## 源码入口

- [Tools/LightmapBaker/Bake.cpp](Tools/LightmapBaker/Bake.cpp)
- [Engine/Scene/Lightmap.cpp](Engine/Scene/Lightmap.cpp)
- [Assets/Shaders/world.wgsl](Assets/Shaders/world.wgsl)
