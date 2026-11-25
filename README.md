# TinyWorld · 第 20 章 · 分块加载与卸载

本分支构建出的 TinyWorld 默认打开第 20 章。也可显式传入 \`--chapter 20\`。

先按 [构建说明](https://github.com/wondeful18/TidyWorld/blob/main/README.md#构建与运行完整工程) 配置 CMake 和 vcpkg。Windows 在 Visual Studio 的 x64 开发者终端中用 \`windows-x64-debug\`，macOS 用 \`macos-arm64-debug\`。

[第 20 章讲义](https://github.com/wondeful18/TidyWorld/blob/main/Chapters/ch20/README.md)

## 源码入口

- [Engine/Terrain/ChunkStreamer.cpp](Engine/Terrain/ChunkStreamer.cpp)
- [Engine/Terrain/ChunkFile.cpp](Engine/Terrain/ChunkFile.cpp)
- [Engine/Renderer/TerrainStreaming.cpp](Engine/Renderer/TerrainStreaming.cpp)
- [Tools/WorldChunker/main.cpp](Tools/WorldChunker/main.cpp)
