# TinyWorld 第一章 · Codex 开工提示词

你是一名 C++ 游戏引擎开发工程师、实时渲染工程师和技术教程作者。在当前工作区实现 TinyWorld 的第 1 章。先读课程总览和代码规范，再查第一章的路线与技术决策。 [课程总览](../COURSE_OVERVIEW.md) · [19 章路线图](roadmap-19-chapters.md)

## 目标

用 C++20、CMake、vcpkg、GLFW、wgpu-native C API、WGSL 和 Dear ImGui Docking，在一个 1280 × 720 逻辑尺寸的原生窗口中显示彩色三角形。先直接呈现到窗口，完成一次独立验证，再让三角形在 DockSpace 的 Scene Viewport 中通过离屏纹理显示。交付可构建工程、真实运行证据和可复现的章节教程。

优先验证 macOS arm64 / Metal 和 Windows x64 / MSVC / D3D12。按平台和 CPU 架构选择依赖包。建立 Windows 构建入口，分别记录实际验证的阶段和待验证阶段。Linux 排入后续移植。渲染使用 WebGPU，窗口桥接允许使用 Cocoa/Win32 原生窗口句柄及 macOS 的 `CAMetalLayer`。

## 工作约束

- 先检查目录、已有文件、仓库状态、编译器、构建工具、安装权限、GPU 和可用图形会话。目录名称以工作区为准，工程、程序与教程统一用 TinyWorld。保留已有用户文件和变更。远程仓库、push 和 Release 发布由用户另行授权。
- 第一章只使用当前里程碑需要的依赖。采用路线中的候选版本，并按固定版本的头文件和官方源码验证接口。wgpu-native 头与库随同一官方发行包导入，按目标校验 SHA-256。ImGui Docking 核心和两个官方后端来自同一 commit，GLFW 由锁定 baseline 的 vcpkg manifest 安装。记录许可证、来源、工具实际版本和验证结果。只有候选依赖确实存在无法化解的兼容问题时，再调整版本或使用最小补丁并记录理由。
- `Engine` 管窗口、Surface、GPU 上下文、日志和场景绘制，`Editor` 管 ImGui 后端和面板。用小型 RAII 所有权模型处理 WebGPU 句柄和退出顺序。独立保存 WGSL，由构建系统部署资源及动态库，运行不依赖进程当前工作目录。
- 按职责拆分窗口、WebGPU 上下文、平台桥接、三角形绘制和场景视口的源文件。自有代码的类和函数在中文多行头注释上下各放一整行 `//————————————————————————————————————————————————————————————————————————————————`。关键逻辑加中文注释，每个类数据成员的声明行尾附中文说明。 [格式与示例](../COURSE_OVERVIEW.md)
- 把本轮工程范围限制为第一章。Scene Viewport、Console 与 Statistics 有可验证功能。Statistics 显示 CPU 帧时、帧率和渲染尺寸。Hierarchy 与 Inspector 提供后续接入点。后续的场景对象、资产浏览、Lua、地形、脚本和物理沿课程路线加入。

## Milestone 0 · 建立构建基线

先报告实际环境、候选依赖、工程边界与首个可运行验证步骤，然后在当前任务中继续实施。在 `CMakeLists.txt`、`CMakePresets.json` 和 `vcpkg.json` 中定义可复现构建，提供按平台区分的 Debug、Release preset。为 wgpu-native 建立导入目标和运行时部署规则。macOS 将需要 Cocoa 的官方 ImGui 后端源码按 Objective-C++ 编译，Windows 启用相同的官方 WebGPU 后端配置并检查 DLL 部署。

从全新构建目录实际配置并编译链接一个最小目标，验证候选依赖可以找到并使用。执行 Apple/Windows 各自可运行环境下的最小 GPU 初始化探查。工具缺失时优先采用项目本地的可复现安装路径，再依据可用性报告确切阻塞。补齐根 README、`Docs/architecture.md` 与 `Docs/Decisions/` 中对应的依赖及架构记录，同步维护既有课程路线。

## Milestone 1–3 · 窗口到直接呈现

1. 创建 `GLFW_NO_API` 窗口并处理关闭、输入、framebuffer resize 和 DPI。分别记录窗口逻辑尺寸和 framebuffer 像素尺寸。零尺寸期间跳过 Surface 获取和绘制。
2. 通过独立平台桥接创建 Surface，依次请求 Adapter、Device、Queue，查询 Surface 能力，校验 WGSL 和 Render Pipeline。给异步请求设置错误报告和有限等待，保证回调状态生命周期正确。初始化失败时记录有用信息并返回非零退出码。
3. 用顶点缓冲和 WGSL 清屏、绘制并呈现三角形。按发行包定义处理获取、配置及呈现错误。区分 timeout、outdated、Surface lost、Device lost 和正常销毁。Surface 可恢复状态尝试有界恢复，Device Lost 记录后有序退出。真实检查直接三角形、拖动窗口和最小化恢复，之后再接 UI。

## Milestone 4 · DockSpace 与离屏视口

使用官方 GLFW 和 WebGPU 后端启用单窗口 Docking。在同一 GPU 设备上，先把三角形绘制到具备 `RenderAttachment` 与 `TextureBinding` usage 的离屏纹理，再通过 ImGui 合成通道呈现。按固定版本的绑定契约将 `WGPUTextureView` 提交给 ImGui，视图在 UI 命令编码与提交期间保持有效。

按视口逻辑区域与 framebuffer 缩放得到离屏像素尺寸，尺寸变化才重建纹理。处理折叠、零面积与恢复。明确颜色纹理格式和输出转换。根据 ImGui capture 和视口焦点区分 UI 输入与场景输入。验证调整主窗口和 Dock 边界、最小化恢复、关闭退出，保存真实截图。

## Milestone 5 · 教程与交付证据

在 `Chapters/ch01/README.md` 说明先修知识、安装和构建命令、运行方法、目录、GPU 对象关系、直接与离屏呈现的代码、WGSL、排错、练习及截图。将真实运行命令和可复现验证步骤写入教程。每个里程碑完成后立即构建并执行当阶段对应检查，修复问题后再进入下一阶段。

最终报告按平台分别列出配置编译、Adapter/Device、直接呈现、Dock 内呈现和交互检查的实测状态，注明 GPU 后端和设备。附工程入口、截图与已知限制。通过本章验收后，在已有 Git 约定允许且工作树范围明确时保存本地 `ch01` 里程碑。下次工作从第 2 章设计接续。
