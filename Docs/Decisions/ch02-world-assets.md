# 第 02 章 · 模型与依赖决策

小屋、树和草地的原件是项目内原创 GLB，分别位于 `Assets/Models/ch02/house.glb`、`tree.glb` 和 `ground.glb`。统一右手系、Y 轴朝上、米为单位。`TinyWorldMeshPacker` 在构建阶段展开这三份静态网格，保留位置、单位法线和线性材质底色，并将输出部署到程序旁。打包后的顶点逐项对照过 GLB 原件。

| 依赖 | 锁定版本 | 用途与许可 |
| --- | --- | --- |
| GLM | `1.0.3`，延用 vcpkg baseline `9eec002…` | C++ 矩阵与向量运算，MIT。 [GLM](https://github.com/g-truc/glm/tree/1.0.3) |
| cgltf | `1.15`，延用 vcpkg baseline `9eec002…` | 离线解析课程 GLB，MIT。 [cgltf](https://github.com/jkuhlmann/cgltf/tree/v1.15) |
| ImGuizmo | `105e4fde45e4e74aae8973d98750820ef7aca7e2`，SHA-256 在 CMake 固定 | 编辑器变换手柄，MIT。 [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo/tree/105e4fde45e4e74aae8973d98750820ef7aca7e2) |

ImGuizmo 直接与第一章锁定的 ImGui Docking 目标共用一个 context。场景使用 `[0, 1]` 裁剪深度，手柄使用 `[-1, 1]` 投影约定。相同相机、视场角与纵横比使两条路径的二维投影一致。

GLB 转换作为构建产物生成，运行时通过 `SceneVertex` 布局读取 `.twmesh`。包的头部为四字节 `TWM2`、小端 `uint32` 顶点数，之后为紧密排列的九个 `float32`，依次为 XYZ、法线 XYZ 和线性 RGB。通用 glTF 数据导入在第 11 章扩展。 [章节教程](https://github.com/wondeful18/TidyWorldBook/blob/main/Chapters/ch02/README.md)
