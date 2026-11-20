# TinyWorld · 第一章架构

第一章由 `TinyWorldEngine` 静态库和 `TinyWorld` 可执行程序组成。原生渲染后端由锁定版本的 wgpu-native 提供。ImGui 和它的后端只由可执行程序使用。

| 层 | 职责 |
| --- | --- |
| `Engine/Core` | 向终端和编辑器提供线程安全日志 |
| `Engine/Platform` | GLFW 生命周期、原生 Surface 桥接与可执行程序路径 |
| `Engine/Renderer` | GPU 对象所有权、Surface 状态与三角形通道 |
| `Editor` | 应用主循环、单窗口 Dock、场景视口与验证入口 |
| `Assets/Shaders` | 由构建系统部署到程序旁的 WGSL |

应用在主线程收集 GLFW 事件，以 framebuffer 像素尺寸获取 Surface 纹理。直接路径将场景绘入 Surface。编辑器路径先绘入 `SceneViewport` 持有的纹理，再由 UI 通道采样它并合成到 Surface。两条路径都使用同一类 `TriangleRenderer`。回读入口复用相同的命令编码，用可复制的颜色目标代替 Surface 视图。

`Window` 比 `WebGpuContext` 长寿，Device 与 Queue 又比渲染器、离屏目标和 ImGui 后端长寿。回调状态随上下文存在。GPU 回调只写入诊断与状态，由主循环统一结束失败的帧。

后续章节在此工程中逐步加入场景、Runtime 和离线 Tools。 [课程总览](../COURSE_OVERVIEW.md) · [第一章教程](../Chapters/ch01/README.md)

## 第 2 章扩展

`Engine/Scene` 维护可编辑的 CPU 场景、相机和变换。`WorldRenderer` 上传模型并写入每物体的 uniform，按场景像素尺寸匹配深度附件。`WorldEditor` 修改同一份场景，并在原有 ImGui 视口中放置 ImGuizmo。构建阶段的 `TinyWorldMeshPacker` 将原始 GLB 转为顶点包，程序从可执行文件邻近目录加载。应用用 `--chapter 01|02` 选择内容，两章复用窗口、事件循环、UI 合成和回读。 [第 02 章教程](../Chapters/ch02/README.md)
