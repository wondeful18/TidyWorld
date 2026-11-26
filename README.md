# TinyWorld · 第 19 章 · 地形 LOD 与可见块裁剪

本分支构建出的 TinyWorld 默认打开第 19 章。也可显式传入 \`--chapter 19\`。

先按 [构建说明](https://github.com/wondeful18/TidyWorld/blob/main/README.md#构建与运行完整工程) 配置 CMake 和 vcpkg。Windows 在 Visual Studio 的 x64 开发者终端中用 \`windows-x64-debug\`，macOS 用 \`macos-arm64-debug\`。

[第 19 章讲义](https://github.com/wondeful18/TidyWorldBook/blob/main/Chapters/ch19/README.md)

## 源码入口

- [Engine/Terrain/TerrainVisibility.cpp](Engine/Terrain/TerrainVisibility.cpp)
- [Engine/Renderer/WorldRenderer.cpp](Engine/Renderer/WorldRenderer.cpp)
- [Tests/TerrainVisibilityChecks.cpp](Tests/TerrainVisibilityChecks.cpp)
