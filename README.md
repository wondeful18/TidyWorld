# TinyWorld

Small Engine, Complete World. 一个从原生空窗口逐步构建到可探索山谷与海岸的 C++20 / WebGPU 教学工程。当前可运行第 01–15 章。

![碎石路跨过木桥并沿河口通向海岬](Chapters/ch15/image/coast-overview.jpg)

## 课程入口

| 章节 | 内容 | 教程 | PDF |
| --- | --- | --- | --- |
| 01 | 从空窗口到第一个三角形 | [从 Step 1 开始](Chapters/ch01/README.md) · [分步源码与工程入口](Chapters/ch01/src/README.md) | [第 01 章](Chapters/ch01/ch01.pdf) |
| 02 | 第一间小屋，第一棵树 | [教程与运行步骤](Chapters/ch02/README.md) | [第 02 章](Chapters/ch02/ch02.pdf) |
| 03 | 阳光与阴影 | [教程与回读对照](Chapters/ch03/README.md) | [第 03 章](Chapters/ch03/ch03.pdf) |
| 04 | 天空盒与环境贴图 | [教程与回读对照](Chapters/ch04/README.md) | [第 04 章](Chapters/ch04/ch04.pdf) |
| 05 | 保存场景与步行检查 | [教程与运行步骤](Chapters/ch05/README.md) | [第 05 章](Chapters/ch05/ch05.pdf) |
| 06 | 分块高度场与地面行走 | [教程与跨块检查](Chapters/ch06/README.md) | [第 06 章](Chapters/ch06/ch06.pdf) |
| 07 | 雕刻山脊与河谷 | [教程与山谷存档](Chapters/ch07/README.md) | [第 07 章](Chapters/ch07/ch07.pdf) |
| 08 | 地表材质与 PBR | [教程与材质对照](Chapters/ch08/README.md) | [第 08 章](Chapters/ch08/ch08.pdf) |
| 09 | 顺着山势铺路 | [教程与避让遮罩](Chapters/ch09/README.md) | [第 09 章](Chapters/ch09/ch09.pdf) |
| 10 | 在山谷里摆放场景物体 | [教程与三轴手柄](Chapters/ch10/README.md) | [第 10 章](Chapters/ch10/ch10.pdf) |
| 11 | 草地、林缘与实例化 | [教程与实例化对照](Chapters/ch11/README.md) | [第 11 章](Chapters/ch11/ch11.pdf) |
| 12 | 按地形规则生成生态 | [教程与道路避让](Chapters/ch12/README.md) | [第 12 章](Chapters/ch12/ch12.pdf) |
| 13 | 用 glTF 搭村庄与桥 | [教程与跨桥验收](Chapters/ch13/README.md) | [第 13 章](Chapters/ch13/ch13.pdf) |
| 14 | 湖泊、溪流与岸边 | [教程与水面对照](Chapters/ch14/README.md) | [第 14 章](Chapters/ch14/ch14.pdf) |
| 15 | 河口与海岸 | [教程、河口与步行验收](Chapters/ch15/README.md) | [第 15 章](Chapters/ch15/ch15.pdf) |

[课程总览](COURSE_OVERVIEW.md) · [课程详细路线](Docs/roadmap.md) · [项目架构](Docs/architecture.md)

## 构建与运行完整工程

按第 01 章准备编译工具、CMake 与 vcpkg，并设置 `VCPKG_ROOT`。也可以安装 Ninja，在 macOS arm64 从仓库根目录使用以下命令。 [Visual Studio 和 Xcode 的工程生成方法](Chapters/ch01/README.md#step-1--用-cmake-打开第一扇窗口)

```sh
cmake --preset macos-arm64-debug
cmake --build --preset macos-arm64-debug
out/build/macos-arm64-debug/bin/TinyWorld --chapter 01
out/build/macos-arm64-debug/bin/TinyWorld --chapter 02
out/build/macos-arm64-debug/bin/TinyWorld --chapter 03
out/build/macos-arm64-debug/bin/TinyWorld --chapter 04
out/build/macos-arm64-debug/bin/TinyWorld --chapter 05
out/build/macos-arm64-debug/bin/TinyWorld --chapter 06
out/build/macos-arm64-debug/bin/TinyWorld --chapter 07
out/build/macos-arm64-debug/bin/TinyWorld --chapter 08
out/build/macos-arm64-debug/bin/TinyWorld --chapter 09
out/build/macos-arm64-debug/bin/TinyWorld --chapter 10
out/build/macos-arm64-debug/bin/TinyWorld --chapter 11
out/build/macos-arm64-debug/bin/TinyWorld --chapter 12
out/build/macos-arm64-debug/bin/TinyWorld --chapter 13
out/build/macos-arm64-debug/bin/TinyWorld --chapter 14
out/build/macos-arm64-debug/bin/TinyWorld --chapter 15
```

第 01 章加 `--direct` 可直接呈现三角形。第 02 章在场景视口内右键拖动可环绕，滚轮可缩放。任一章节加 `--capture output.ppm` 可读取 GPU 绘制结果。

第 02–05 章点 `Hierarchy` 中的 `Drive car` 或加 `--preset drive` 即可开小车。视口获得焦点后，用 **WASD** 或方向键驾驶，按 **R** 或 **Home** 回到起点。第 03–05 章在 `Sun` 属性里调整方向光、阴影开关、PCF 和深度偏移。第 04–05 章在 `Environment` 中切换天空盒、全景天空球和车身反射。

第 05 章在 `Hierarchy` 中编辑 `Spawn`，用 `Save scene` 和 `Open scene` 保存或重开 JSON。点 `Walk from spawn` 进入第三人称步行，按 **W/S** 前进或后退，按 **A/D** 转向，按 **R** 或 **Home** 回到出生点。

第 06 章用小机器人检查坡面。点 `Walk from spawn` 或使用 `--chapter 06 --preset walk`，勾选 `Chunk borders` 查看分块边界，勾选 `LOD preview` 比较粗细网格。

第 07 章用视口工具栏雕刻坡地并撤销或重做。第 08 章可用 `Paint layers` 绘制 RGBA 材质权重，加 `--preset materials` 比较材质球。第 09 章在 `Road` 拖动控制点、切换路面和设置丁字或十字路口，再用 `Export avoidance PGM` 导出避让遮罩。

第 13 章用 `--preset walk` 从村口走上木桥。第 14 章在 `Water` 属性中切换水深、反射和隐藏水面的对照。

第 15 章沿碎石路跨桥抵达海岬，使用 `--preset shore` 看河口近景。

Windows x64 使用 `windows-x64-debug` 构建 preset，详细环境和启动命令见章节教程。各平台的实测范围记录在对应章节。

### Visual Studio 2022

安装 Visual Studio 2022 的「使用 C++ 的桌面开发」工作负载。按第一章准备 vcpkg 后，在仓库根目录生成解决方案。

```powershell
$env:VCPKG_ROOT = (Resolve-Path .tools\vcpkg).Path
cmake --preset windows-x64-vs2022
```

打开 `out/build/windows-x64-vs2022/TinyWorld.sln`，选择 `Debug | x64`。启动项目是 `TinyWorld`，默认运行第 02 章。在 `Editor/main.cpp` 或 `Engine/Renderer/WorldRenderer.cpp` 设置断点后按 **F5** 调试。按 **Ctrl+F5** 直接运行。在 `TinyWorld` 项目的「属性 → 调试 → 命令参数」中修改 `--chapter` 的章号即可切换。

## 目录

- `Engine/` 和 `Editor/` 保存引擎与编辑器源码，`Runtime/` 保存步行控制器。
- `Assets/` 保存着色器和课程模型，`Tools/` 保存离线工具。
- `Chapters/` 保存 Markdown 教程、PDF 和插图，`Docs/` 保存路线与技术决策。
- 本机工具装在 `.tools/`，preset 将构建产物写到 `out/`。两个目录都由 Git 忽略。
