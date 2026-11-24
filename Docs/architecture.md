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

`Engine/Scene` 维护可编辑的 CPU 场景、相机和变换。`WorldRenderer` 上传模型并写入每物体的 uniform，按场景像素尺寸匹配深度附件。`WorldEditor` 修改同一份场景，并在原有 ImGui 视口中放置 ImGuizmo。构建阶段的 `TinyWorldMeshPacker` 将原始 GLB 转为顶点包，程序从可执行文件邻近目录加载。应用用 `--chapter` 选择内容，各章复用窗口、事件循环、UI 合成和回读。 [第 02 章教程](../Chapters/ch02/README.md)

## 第 3 章扩展

`WorldRenderer` 在支持阴影的场景里持有 `SunShadowMap`。太阳深度通道先写入固定尺寸的可采样深度纹理，颜色通道随后读取。两条通道共享物体绑定，只有颜色通道绑定比较采样器与深度视图。小车、太阳和阴影参数来自同一份 CPU 场景，直接呈现与编辑器离屏绘制共用此顺序。 [第 03 章教程](../Chapters/ch03/README.md)

## 第 4 章扩展

`EnvironmentMap` 在启动时解码实拍全景 JPEG，上传等距柱状原图并重投影到六层 Cubemap。`SkyRenderer` 在物体绘制后选择立方体或球形网格，只保留相机旋转，以远平面深度填充背景。`WorldRenderer` 的环境入口复用 Cubemap，给车漆和玻璃加入不同强度的反射。旧的 `.twmesh` 顶点在上传时扩展为带反射权重的运行时格式。 [第 04 章教程](../Chapters/ch04/README.md)

## 第 13–14 章扩展

`GltfAsset` 将静态 glTF 的节点变换烘入网格，按 primitive 保留 UV 与材质引用。`WorldRenderer` 为资产原型上传网格与材质，为场景实例保留各自的 uniform。JSON 保存相对路径和变换，`WalkController` 用同一个桥实例查局部桥面与引道高度。 [第 13 章教程](../Chapters/ch13/README.md)

`WorldRenderer` 在水可见时保留相机深度。`WaterRenderer` 在单独的通道采样该深度，用逆视图投影重建水下落点，再把半透明水面叠加到已有的颜色。环境 Cubemap 与天空共享。 [第 14 章教程](../Chapters/ch14/README.md)

## 第 15 章扩展

海岸场景继续共享高度样点、路网和桥面查询。东侧岸坡由高度和法线决定沙与岩的自动权重，近岸水带与外海网格共边。海浪扰动海面法线，天空与远海共享同一份低空色。 [第 15 章教程](../Chapters/ch15/README.md)

## 第 16 章扩展

`EnvironmentMap` 从全景图提取线性低空色，天空、远海和距离雾共用。`FrameAtmosphere` 保存每帧的雾参数和阴影接收范围。地表、植被、导入模型和水面在显示编码前调用同一份 WGSL 雾函数。太阳方向继续同时驱动直射光和固定的太阳正交投影，阴影接收端按相机距离平滑淡出。 [第 16 章教程](../Chapters/ch16/README.md)
