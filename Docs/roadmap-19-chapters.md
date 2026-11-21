# TinyWorld · 19 章课程路线

课程目标、AI 协作入口和适用于所有章节的中文代码注释规范见项目总览。 [课程总览](../COURSE_OVERVIEW.md)

## 课程目标与结业作品

面向学过基本 C++ 语法、希望理解实时渲染和引擎内部实现的读者。第一章从安装编译器、手动编译及链接开始，再引入构建工具。RAII、向量、矩阵、GPU 数据布局和渲染调试在首次使用时讲解。课程采用 C++20、CMake、vcpkg、GLFW、wgpu-native C API、WGSL 和 Dear ImGui Docking。

结业作品是一张原创的风格化山谷与海岸地图。玩家从村庄出发，经岔路、草地和林地到达湖泊、溪流和瀑布，最后走到海岸。沿途演示地形编辑、可复现的植被生成、村庄静态光照烘焙、动态日照、天气切换和分块加载。建议从约 256 × 256 米的垂直切片起步，结业地图先以约 1 × 1 千米为制作上限，再根据内容和性能实测调整。角色能力以行走、跑步、跳跃和浅水交互为主，村庄只需一个可交互 NPC 和少量可复用建筑。

平台基线选 macOS arm64 / Metal 和 Windows x64 / MSVC / D3D12。Linux x64 可在主线稳定后选一个窗口系统做移植实验。每个平台单独记录配置、编译链接、创建设备、画面和交互实测。课程参考美术可以研究自然环境的清晰色块和远近层次，资产和世界设计使用原创或明确授权的作品。

## 原方案的主要调整

| 原方案中的风险 | 修订后的决定 |
| --- | --- |
| 把工具安装、GPU 初始化和完整编辑器堆在开头 | 每步留下可运行结果，按手动编译、CMake 构建、空窗口、GPU 清屏、直接三角形、编辑器视口的顺序推进。Hierarchy 和 Inspector 在第 2 章接入场景对象。 |
| GLFW 被描述为负责 WebGPU Surface，且各依赖可能各自升级 | GLFW 负责窗口与输入。引擎平台层用 GLFW 提供的原生窗口句柄绑定 WebGPU Surface。wgpu-native 发行包、ImGui 核心与官方后端作为一组验证后升级。 |
| 第 4 章同时承担序列化、脚本与通用物理的预期 | 拆为三个小节，先存档，再实现 C++ 固定步长角色，最后绑定一条 Lua 事件。首版碰撞从地平面开始，第 5 章接高度场，第 11 章再补桥面和少量静态阻挡。 |
| 高度场、道路、PCG 和串流各自定义世界 | 第 5 章统一块坐标、边缘采样和高度查询。第 8 章让道路输出路面网格与遮罩，第 9 章消费这些数据，第 19 章复用同一块键加载。 |
| 多叉路口接近一套道路拓扑编辑器 | 第 8 章选同一平面上的共享节点三岔路口，要求一段可反复编辑的示例。四岔、任意角度拼接和立交列为实验。 |
| Lightmap 同时要求实现直接光、AO、路径追踪、CPU 并行和 GPU 计算 | 第 12 章先做可重复的 CPU 静态光烘焙，交付 UV2、直接光可见性、padding 与运行时采样。AO、并行和间接光分开追加。第 16 章使用同一照明协议。 |
| 深度缓冲、透明合成、色彩空间等前置技术出现过晚 | 第 2 章约定矩阵、坐标和色彩。第 13 章先梳理不透明深度与水体透明阶段。第 17 章实现 HDR 到显示输出的统一转换。 |
| 风场、天气、水雾和屏幕特效有多个时间源 | 从第 6 章开始使用同一个模拟时间。第 7 章把植被交互叠加到全局风，第 16 章给天气系统提供统一的风速与风向输入。 |
| CSM 和性能工作集中在后期，最终规模也未定义 | 第 6 章比较草实例化前后，第 10 章给级联阴影设预算并提供调试视图，第 19 章用同一路线比较裁剪和串流。移动 GPU 优化单列为扩展。 |
| glTF、OBJ、ImPlot 和节点编辑器都可能成为主线依赖 | 第 2 章用程序生成几何，第 11 章引入 glTF 静态子集，第 18 章才扩展到蒙皮。节点编辑器、ImPlot、OBJ 导入按实验需要选用。 |

## 19 章主线

表中的扩展实验独立于章节验收。每章交付一个可运行的工程状态，一个可以快速打开的演示入口，以及一组能用截图、数值、日志或对照操作验证的结果。

### 第一篇 · 图形骨架与可游玩的场景

| 章 | 先修 | 必做与验收 | 扩展实验 |
| --- | --- | --- | --- |
| 01 从 Hello World 到第一个三角形 | C++ 基本语法 | 从空目录手动编译 `main.cpp`，再加 CMake 和 Ninja。用 vcpkg 引入 GLFW 打开空窗口，接入 WebGPU 后先清屏，再加顶点缓冲和 WGSL。最后把同一个三角形画到 ImGui 的 Scene Viewport。每一步有源码或程序入口，分别验收终端输出、窗口、GPU 呈现和面板。 | 用独立的小程序做离屏像素回读。 |
| 02 第一个 3D 世界 | 01 | 先运行上一章的直接三角形，依次增加相机和深度、网格与 Lambert 光照、Transform 编辑。场景由房子、树和地面组成。Hierarchy 可选对象，Inspector 可编辑 Transform，ImGuizmo 提供平移和旋转。最后用小车验收前进、倒车、转向、跟随和复位。 | 完整材质编辑、网格吸附。 |
| 03 阳光、阴影与天空 | 02 | 第 1 节实现方向光深度通道、单张 Shadow Map、PCF 与偏移，用固定机位对比阴影伪影。第 2 节用 Cubemap 天空盒与等距柱状全景天空球讲清采样方向、相机平移和深度状态，验收地平线朝向、接缝与前景遮挡。第 3 节用反射方向采样环境 Cubemap，将车身的环境反射做成开关，沿固定漫游路线观察变化。 | Normal bias 对比、更大的 PCF 核、全景图转 Cubemap。 |
| 04 保存场景，加入第一个玩家 | 02 | 第 1 节写含版本字段的 JSON 场景与稳定对象 ID。第 2 节用 C++ 固定步长做胶囊参数、地面接触、移动跳跃和第三人称跟随。第 3 节让 Lua 响应一个现有游戏事件。验收保存并重开场景、测试坏文件诊断，在不同渲染帧率下取得接近的跳跃结果。 | F5 事务式重载，脚本载入失败时保留上次有效实例。 |

### 第二篇 · 从可编辑地形到可复现生态

| 章 | 先修 | 必做与验收 | 扩展实验 |
| --- | --- | --- | --- |
| 05 雕刻山地 | 04 | 用同一高度源构造分块网格、射线拾取和角色地面查询，实现 Raise、Lower、Smooth、Flatten 和笔刷衰减。保存高度图与尺度参数。验收跨块连续笔刷、边界无裂缝、重开后地形和地面碰撞同步。 | 一次笔划一级撤销、笔刷曲线、ImPlot。 |
| 06 从草叶到草海 | 05 | 用同一地块比较几何草与交叉卡片，讲解 Alpha Test、实例化、地表颜色混合和基础风摆，使用同一相机和实例数记录 Draw Call 与帧时。 | 近中远草密度、Alpha To Coverage。 |
| 07 树冠与可交互的风 | 05、06 | 建立树干与叶片卡片，加入树冠法线和共享全局风参数。在玩家周围更新有限范围的交互纹理，草叶被拨开后平滑恢复。验收固定大风与局部交互两种场景。 | 远景树 Billboard、局部阵风、GPU 计算更新。 |
| 08 会贴着地形走的道路 | 05 | Catmull–Rom 路径、带 UV 的路面网格、地形贴合和路基平整。用共享端点实现一个平面三岔路口，输出 PCG 可查询的道路遮罩。验收移动控制点后路面、路基和遮罩同步更新。 | 四岔路口、可撤销道路修改、不同坡度的分支拼接。 |
| 09 一键生成森林与山石 | 05、07、08 | 使用固定种子、泊松圆盘采样、海拔、坡度、密度和道路遮罩，由一条 Lua 规则选择资产或调整密度。按块替换生成结果并保留手摆对象。验收同一平台、同一输入生成相同对象 ID 与位置，路面无生成对象，重复生成不堆叠。 | 跨平台量化一致性、生态权重噪声、节点编辑器。 |
| 10 地平线上的阴影 | 03、06、07 | 实现三级 CSM、分段调试色、过渡和 texel 稳定化。沿同一路径比较单张和多级阴影，记录每级分辨率、参与绘制的实例数及帧时变化。 | 四级级联、按级联裁剪实例、动态阴影距离。 |

### 第三篇 · 村庄与照明数据

| 章 | 先修 | 必做与验收 | 扩展实验 |
| --- | --- | --- | --- |
| 11 从资产拼出山谷村庄 | 02、04、08 | 导入预先约定子集的 glTF/GLB 静态模型和纹理，建立最小资产列表、Prefab 实例、吸附和桥面碰撞。用房屋、围栏和桥梁搭出小村。验收场景重开后仍引用同一资产，角色可从道路走过桥。 | 资产热重载、额外 glTF 材质扩展、简易摆放撤销。 |
| 12 手写 CPU Lightmap Baker | 03、11 | 离线工具用 xatlas 生成 UV2，按 texel 取表面样本，以三角形求交和 BVH 判断静态灯直射可见性。输出烘焙图集、padding 和模型映射，运行时采样。验收一张含遮挡和 UV 岛边界的小场景，记录烘焙耗时并比较烘焙开关。 | AO、CPU 并行、间接光路径追踪、GPU Compute，各自独立。 |

### 第四篇 · 一条从山到海的水系

| 章 | 先修 | 必做与验收 | 扩展实验 |
| --- | --- | --- | --- |
| 13 静湖与溪流 | 03、05、11 | 实现湖泊和窄溪 Mesh，先绘不透明场景，再用可采样深度计算水深色与岸边泡沫。湖面复用环境 Cubemap，用 Fresnel 调节反射强度，溪流采用双相位 Flow Map。固定相机验证浅深变化、泡沫沿岸、流向连续，并从低角度观察天空反射。 | 折射颜色输入、屏幕空间反射。 |
| 14 瀑布与水雾 | 13 | 由样条生成瀑布曲面，叠加滚动纹理和落点白沫，用有数量上限的 CPU 粒子发射器表现水雾。验收沿水方向移动、落点固定、发射器可重置。 | 水面动态波纹、GPU 粒子。 |
| 15 海面与海岸 | 13、14 | 在有限海岸视野内用少量 Gerstner 波叠加、顶点位移与法线扰动，加上深度或岸线遮罩控制的泡沫。验收海面运动连续，泡沫主要集中在近岸。 | 远海网格环、FFT、浪涌上岸。 |

### 第五篇 · 从风格化场景到可探索世界

| 章 | 先修 | 必做与验收 | 扩展实验 |
| --- | --- | --- | --- |
| 16 昼夜与天气 | 03、07、10、12、14 | 接续第 3 章的天空背景。统一世界时间，太阳驱动实时直射光和 CSM，天空渐变、简化月亮或星空与高度雾跟随时间变化。实现晴天和阴天两个预设平滑切换，天气驱动风与雾。验收日出、正午、夜晚三张固定机位图，同一处静态灯烘焙在各时段正常叠加。 | 大气散射、体积云、雨、SH 探针。 |
| 17 HDR 与风格化后处理 | 13、16 | 线性 HDR 场景、可调手动曝光、Bloom、ACES 近似色调映射和显示输出转换。替换第 3 章的环境图，用 HDR 图观察反射亮部如何随曝光变化。用同一场景对比开关，检查 UI 清晰且显色逻辑只有一次显示编码。 | 漫反射辐照度与按粗糙度预滤波的 IBL，自动曝光、屏幕空间 God Rays、LUT 调色。 |
| 18 角色与世界交互 | 04、11、13 | 导入一套受约束的 glTF 蒙皮与动画片段，做 Idle、Walk、Run、Jump 状态切换。由控制器决定位置和速度，动画跟随。使用水体查询实现浅水减速，Lua 驱动一个 NPC 交互。验收走跑跳切换、桥上下通行、浅水退出和一次 NPC 交互。 | Root Motion、足部 IK、游泳、NPC 导航。 |
| 19 大世界串流与结业漫游 | 05、09、10、15–18 | 按块键在后台读 CPU 数据，在渲染线程按预算上传，支持卸载并限制驻留。完成两档地形 LOD 与接缝处理、CPU 视锥裁剪。沿固定路线比较启用优化前后的帧时、峰值驻留块数、上传开销和 Draw Call。验收跨多块往返、快速回头无丢块，保存重开能继续探索。 | 自适应 Quadtree、GPU 实例裁剪、移动设备实测。 |

## 项目结构与接口引入顺序

```text
TinyWorld/
  COURSE_OVERVIEW.md
  CMakeLists.txt
  CMakePresets.json
  vcpkg.json
  README.md
  cmake/                      # 平台、依赖和资源部署规则
  Engine/
    Core/                     # 日志与基础生命周期
    Platform/                 # GLFW、平台窗口句柄和 Surface 桥接
    Renderer/                 # WebGPU 上下文与渲染通道
  Editor/                     # 可执行程序、ImGui 和编辑工具
  Assets/Shaders/             # 与工程一同构建部署的 WGSL
  Chapters/ch01/              # 教程、截图和本章验证步骤
    src/                      # step1 至 step6，从 Hello World 到编辑器
  Docs/
    architecture.md
    roadmap-19-chapters.md
    Decisions/
```

上述为第一章目录，随后按需加入 `Engine/Scene`、`Runtime/`、`Scripts/`、`Tools/LightmapBaker` 和规范资产目录。`Editor`、`Runtime` 与离线 `Tools` 分别使用 `Engine`。编辑器面板与 ImGui 后端归 `Editor`，真实运行和离线工具可以独立使用引擎。第 2 章从简单场景容器和 Transform 开始，第 4 章增加稳定对象 ID 和小型 JSON schema。

- **第 2 章 · 约定空间。** 世界坐标右手系、Y 向上、单位为米，相机局部前向为 −Z。投影深度使用 WebGPU 的 0–1 范围。用一张图说明世界、裁剪、屏幕和纹理坐标，用简单几何验算投影、正面绕序和相机方向。讲清线性计算、sRGB 颜色纹理与线性数据纹理。glTF 导入时分别处理场景轴约定与资产自身朝向。 [WebGPU 规范](https://www.w3.org/TR/webgpu/) · [glTF 2.0 规范](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html)
- **第 4–9 章 · 共享世界数据。** 场景只保存稳定 ID、相对资产引用和源参数。地形网格、道路网格和 PCG 实例都有明确的重建来源。高度场、编辑器拾取和角色采样使用同一份数据。PCG 生成物有自己的所有权标记，与手摆对象可独立更新。
- **第 12–17 章 · 共享照明协议。** 静态灯烘焙到静态物体的 lightmap 通道，动态太阳由实时阴影驱动。同一束直射光只进入一个通道。动态角色用实时太阳光和环境色，探针是后续实验。水体在不透明绘制完成后使用其深度，首次实现可用独立深度输入；后处理发生在场景绘制之后、编辑器 UI 之前。
- **第 19 章 · 限制串流范围。** 第一版只异步读入 CPU 数据，GPU 上传由渲染线程控制。使用明确的块状态、有限请求队列和驻留上限。跨块高度图共边通过第 5 章的采样约定共享，需要时用边缘缝合或 skirt 处理不同 LOD。回归路径包含掉头、离开后返回和跨块跳跃。

## 第一章的开发关卡

| Milestone | 实施 | 通过证据 |
| --- | --- | --- |
| 0 环境与构建基线 | 先从空目录编译一个 C++ 文件，再用 CMake 和 Ninja 构建同一份源码。随后准备 vcpkg、按平台拆分的正式工程 preset，以及后续阶段需要的 wgpu-native 导入与部署。 | 手动与 CMake 构建都能打印 Hello World。从新构建目录验证正式工程，记录工具版本、目标架构、依赖来源和许可证。 |
| 1 窗口和事件 | 主线程创建 1280 × 720 逻辑尺寸的 `GLFW_NO_API` 窗口。处理关闭、输入、framebuffer 尺寸及比例变化。 | 窗口打开和关闭各一次，记录逻辑尺寸与 framebuffer 像素尺寸。验证缩放输入与最小化恢复。 |
| 2 WebGPU 初始化 | 平台桥接创建 Surface，基于 Surface 请求 Adapter，取得 Device 和 Queue，查询能力并配置 Surface。先录制一条清屏通道，再提交并呈现。 | 原生窗口填成蓝色。日志给出 Adapter、原生后端、Surface 格式和像素尺寸。 |
| 3 直接绘制 | 沿用清屏帧循环，加入顶点缓冲、WGSL 和 Pipeline。按固定版本的 Surface 状态定义处理 resize、timeout、outdated、lost 和错误。 | 在原生窗口看到稳定三角形，验证调整大小、最小化恢复和正常退出。着色器错误可定位到文件，失败路径给出非零退出码。 |
| 4 Docking 与离屏显示 | 同一设备上先绘离屏颜色纹理，再在 Surface 上合成 UI。离屏纹理有 `RenderAttachment` 和 `TextureBinding` usage。Scene Viewport 使用实际 framebuffer 缩放计算离屏尺寸，资源切换跟随帧生命周期。实现 Console 和 CPU Statistics，Hierarchy 与 Inspector 作为后续接入点。 | 场景视口内显示三角形；可拖动 Dock 边界，调整主窗口、折叠与恢复面板；界面输入与场景输入路由正确。记录一张真实截图。 |
| 5 教程与可复现验证 | 解释两条呈现路径、WGSL 及各对象的所有权。编写工具安装、构建、运行、排错和练习。检查从全新构建目录到运行的完整流程。 | 教程包含实际命令、退出码、平台矩阵、截图和本章已知限制。未运行的 Windows GPU 或 Linux 检查各自显示待测。 |

第一章帧顺序为收集事件、开始 ImGui 帧、测量视口并准备纹理、提交 3D 通道、合成 UI、呈现。测量结果为零时暂停对应工作，避免零尺寸 Surface 或纹理。纹理作为后端约定的 `WGPUTextureView` 提交，视图需在本帧 UI 编码及提交过程中有效。固定版本将用户纹理的 bind group 按帧清理。对可恢复的 Surface 问题采用有限次恢复，Device Lost 单独记录并有序退出。所有回调状态在相关回调处理与资源释放期间保持有效。输入以 ImGui 的 capture 状态和视口焦点为依据。 [GLFW 窗口指南](https://www.glfw.org/docs/latest/window_guide.html) · [锁定版本的 ImGui WebGPU 后端头文件](https://github.com/ocornut/imgui/blob/b916186e66aa3c02b72c6ca17455516512ed8049/backends/imgui_impl_wgpu.h) · [锁定版本的后端实现](https://github.com/ocornut/imgui/blob/b916186e66aa3c02b72c6ca17455516512ed8049/backends/imgui_impl_wgpu.cpp)

### Milestone 0 候选版本与探查结果

环境检查日期为 2026-11-20。工作区检查时为空且未初始化 Git。执行环境为 macOS 26.5、Apple M4 Pro、Xcode 26.6、Apple Clang 21.0.0。`cmake`、`ninja`、`vcpkg` 未在 `PATH` 中找到，`VCPKG_ROOT` 未设置。开工时先补齐项目构建工具，优先使用项目本地或已有安装并记录实际版本。

| 依赖 | 候选锁定方式 | 当前证据 |
| --- | --- | --- |
| wgpu-native | `v29.0.1.1`，按 OS 与 CPU 架构选 Release ZIP，以官方 SHA-256 校验，将包内头与对应库一起导入 CMake。 | macOS arm64 ZIP 摘要已核对。已链接官方动态库并成功取得 `Apple M4 Pro / Metal` Adapter、Device 和 Queue。 |
| Dear ImGui | `v1.93.0-docking`，commit `b916186e66aa3c02b72c6ca17455516512ed8049`，核心和 GLFW/WebGPU 后端取自同一源码快照。 | WebGPU 后端使用 `IMGUI_IMPL_WEBGPU_BACKEND_WGPU` 并按 Objective-C++ 编译成功；按纯 C++ 编译会在 Cocoa 导入处失败。GLFW 后端配合 GLFW 3.5.1 头文件编译成功。 |
| GLFW / vcpkg | `glfw3` 3.5.1，registry baseline `9eec002df1cd109f8f104cd966a763daf9751489`（vcpkg `2026.10.16`）。同时记录工具版本和目标 triplet。 | 已通过 vcpkg 安装 `arm64-osx` 目标包，macOS Debug 与 Release 均已配置和链接。 |
| 原生窗口桥接 | 用独立的小文件处理 Cocoa/Win32，并用包内 WebGPU Surface 描述符。本机 Cocoa 源使用 Objective-C++。 | `glfw3webgpu v1.2.0` 与候选头文件的编译检查失败，涉及旧的 Surface 类型和 `label` 类型。自有桥接的呈现验证安排在 Milestone 2–3。 |

| wgpu-native 官方 Release ZIP | SHA-256 | 验证状态 |
| --- | --- | --- |
| `wgpu-macos-aarch64-release.zip` | `a5797a37b1adf720bcd5dcffb291edbbd5b7b14be0a3874c28e6393a655a7a3e` | 已本地校验 |
| `wgpu-windows-x86_64-msvc-release.zip` | `7e67d7445c42aeb85e30f88930fd8d7d83ee769e3390aeb1ada75ebf3cf78132` | 已本地校验 |

第一章依赖许可证为 wgpu-native 的 MIT 或 Apache-2.0、Dear ImGui 的 MIT、GLFW 的 Zlib 和 vcpkg 的 MIT。发行快照按实际分发内容保留许可证及 notices。 [wgpu-native 许可证声明](https://github.com/gfx-rs/wgpu-native/blob/v29.0.1.1/Cargo.toml) · [Dear ImGui 许可证](https://github.com/ocornut/imgui/blob/b916186e66aa3c02b72c6ca17455516512ed8049/LICENSE.txt) · [GLFW 许可证](https://github.com/glfw/glfw/blob/3.5.1/LICENSE.md) · [vcpkg 许可证](https://github.com/microsoft/vcpkg/blob/2026.10.16/LICENSE.txt)

以上记录保留开工时的依赖探查。第一章的实际构建和运行结果见章节教程。主线后续依赖按章加入并锁定，GLM 与 ImGuizmo 在第 2 章，JSON 库、Lua 5.4 和 sol2 在第 4 章，glTF 导入和所需的图像库在第 11 章，xatlas 在第 12 章。 [第一章实测记录](../Chapters/ch01/README.md) · [wgpu-native 发行包](https://github.com/gfx-rs/wgpu-native/releases/tag/v29.0.1.1) · [ImGui 的 wgpu-native 29 修订](https://github.com/ocornut/imgui/commit/6d1f88f) · [GLFW 3.5.1](https://github.com/glfw/glfw/releases/tag/3.5.1) · [vcpkg 版本锁定文档](https://learn.microsoft.com/en-us/vcpkg/consume/lock-package-versions) · [候选 GLFW port](https://raw.githubusercontent.com/microsoft/vcpkg/9eec002df1cd109f8f104cd966a763daf9751489/ports/glfw3/vcpkg.json)

Milestone 0 的构建契约建议使用 Ninja，并为 macOS arm64 和 Windows x64 分开命名配置 preset，每个平台提供 Debug 与 Release。CMake 将 WGSL 和需运行时加载的库部署到可执行程序对应的输出目录，教程从不同当前目录启动以验证路径。Windows 验证显式检查 DLL 部署及 MSVC 架构，macOS 检查动态库加载路径。优先用已校验的官方二进制包，必要时另行记录 Rust 源码构建的可复现方式。

## 课程交付与验收节奏

- **课内结构。** 每节从上一节的可运行结果起步，增加一组相关能力，并给出一个可观察的验收结果。第 1 章用六步完成从空目录到编辑器的过渡，第 3、4、5、8、12、16、19 章建议分两到三个可运行小节。按第 1、5、12、19 章分别建立构建、地形编辑、静态烘焙和最终漫游的阶段演示。
- **章节历史。** `main` 保持最新可构建的教程工程。通过验收才建立 `ch01` 到 `ch19` 的 annotated tag。已发布标签保留，勘误用补丁标签或文档链接。章节目录保存教程、截图与轻量验证场景，共享资产和源代码引用项目统一目录。需要给读者下载快照时，再生成与标签对应的 Release。
- **验证层级。** 先验证全新构建，再验证带图形后端的启动，再验证场景与交互。CI 先承担具备条件的构建、纯 CPU 测试和场景数据检查。真实 GPU 跑通由对应环境记录。物理、PCG 和资源串流使用可复现输入，视觉章节至少提供固定相机的验收画面。
- **性能采样。** 第 6 章记录首次基线，第 10 章复用相同场景判断 CSM 成本，第 19 章固定镜头路线、分辨率、实例规模、后端和 Release 构建，比较帧时中位数与 P95、Draw Call、驻留块数和峰值上传。1080p/60 fps 的 16.7 ms 可用作测量参考，最终阈值由实测参考机器与场景确定。GPU 时间戳仅在后端支持时启用，CPU 计时和 GPU 计时分别标明。可按需用 Tracy 分析 CPU，RenderDoc 用于受支持的 Windows/Linux 图形后端，macOS Metal 抓帧用 Xcode。 [RenderDoc 平台与 API 支持](https://github.com/baldurk/renderdoc#api-support) · [Xcode Metal 抓帧](https://developer.apple.com/documentation/xcode/capturing-a-metal-workload-in-xcode)
- **内容制作。** 从第 5 章开始维护一条固定的山谷路线，每个新系统都能在小验证场景中单独展示，并能逐步进入山谷。为第三方资产记录来源、授权及再分发条件。最终以同一场景的漫游、可编辑演示和性能记录完成课程。
