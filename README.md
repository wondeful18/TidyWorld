# TinyWorld · 第 13 章 · 用 glTF 搭村庄与桥

本分支构建出的 TinyWorld 默认打开第 13 章。也可显式传入 \`--chapter 13\`。

先按 [构建说明](https://github.com/wondeful18/TidyWorld/blob/main/README.md#构建与运行完整工程) 配置 CMake 和 vcpkg。Windows 在 Visual Studio 的 x64 开发者终端中用 \`windows-x64-debug\`，macOS 用 \`macos-arm64-debug\`。

[第 13 章讲义](https://github.com/wondeful18/TidyWorld/blob/main/Chapters/ch13/README.md)

法线贴图特写在初版之后加入，跟做这项实验时选用主分支。

## 源码入口

- [Engine/Scene/GltfAsset.cpp](Engine/Scene/GltfAsset.cpp)
- [Engine/Renderer/WorldRenderer.cpp](Engine/Renderer/WorldRenderer.cpp)
- [Runtime/WalkController.cpp](Runtime/WalkController.cpp)
