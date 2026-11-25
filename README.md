# TinyWorld · 第 15 章 · 河口与海岸

本分支构建出的 TinyWorld 默认打开第 15 章。也可显式传入 \`--chapter 15\`。

先按 [构建说明](https://github.com/wondeful18/TidyWorld/blob/main/README.md#构建与运行完整工程) 配置 CMake 和 vcpkg。Windows 在 Visual Studio 的 x64 开发者终端中用 \`windows-x64-debug\`，macOS 用 \`macos-arm64-debug\`。

[第 15 章讲义](https://github.com/wondeful18/TidyWorld/blob/main/Chapters/ch15/README.md)

## 源码入口

- [Tools/AssetTools/shape_coast.py](Tools/AssetTools/shape_coast.py)
- [Engine/Renderer/WaterRenderer.cpp](Engine/Renderer/WaterRenderer.cpp)
- [Assets/Shaders/water.wgsl](Assets/Shaders/water.wgsl)
