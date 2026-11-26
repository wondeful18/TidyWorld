# TinyWorld · 第 18 章 · 让山谷从白天走到夜晚

本分支构建出的 TinyWorld 默认打开第 18 章。也可显式传入 \`--chapter 18\`。

先按 [构建说明](https://github.com/wondeful18/TidyWorld/blob/main/README.md#构建与运行完整工程) 配置 CMake 和 vcpkg。Windows 在 Visual Studio 的 x64 开发者终端中用 \`windows-x64-debug\`，macOS 用 \`macos-arm64-debug\`。

[第 18 章讲义](https://github.com/wondeful18/TidyWorldBook/blob/main/Chapters/ch18/README.md)

跟随角色的提灯和 HDR 曝光在初版之后加入，跟做这两项实验时选用主分支。

## 源码入口

- [Engine/Scene/DayCycle.cpp](Engine/Scene/DayCycle.cpp)
- [Engine/Renderer/EnvironmentMap.cpp](Engine/Renderer/EnvironmentMap.cpp)
- [Engine/Renderer/WorldRenderer.cpp](Engine/Renderer/WorldRenderer.cpp)
