# 第 01 章 · 构建与依赖决策

使用 C++20、CMake 3.24+ 和 Ninja。首个构建矩阵是 macOS arm64 和 Windows x64 MSVC，各提供 Debug 与 Release preset。依赖下载和编译输出留在本地工具目录与构建目录。

| 依赖 | 锁定方式 | 许可证与来源 |
| --- | --- | --- |
| wgpu-native | 官方 `v29.0.1.1` 二进制包，头与动态库同包导入 | MIT 或 Apache-2.0。[版本与许可声明](https://github.com/gfx-rs/wgpu-native/blob/v29.0.1.1/Cargo.toml) |
| Dear ImGui Docking | `v1.93.0-docking`，commit `b916186e66aa3c02b72c6ca17455516512ed8049` | MIT。[固定版本许可证](https://github.com/ocornut/imgui/blob/b916186e66aa3c02b72c6ca17455516512ed8049/LICENSE.txt) |
| GLFW | vcpkg 的 `glfw3` 3.5.1 | Zlib。[GLFW 许可证](https://github.com/glfw/glfw/blob/3.5.1/LICENSE.md) |
| vcpkg | `2026.10.16`，registry baseline `9eec002df1cd109f8f104cd966a763daf9751489` | MIT。[vcpkg 许可证](https://github.com/microsoft/vcpkg/blob/2026.10.16/LICENSE.txt) |

Release 包的版本、架构和完整 SHA-256 见 CMake 依赖文件。macOS arm64 和 Windows x64 MSVC 两个官方归档均已下载核对摘要。ImGui 的后端以 `IMGUI_IMPL_WEBGPU_BACKEND_WGPU` 编译，macOS 下 WebGPU 后端源文件按 Objective-C++ 编译。项目自己的 Cocoa 和 Win32 桥接使用同包 WebGPU C API 的 Surface 描述符。 [官方发行包](https://github.com/gfx-rs/wgpu-native/releases/tag/v29.0.1.1) · [CMake 依赖文件](../../cmake/Dependencies.cmake)

本机构建工具为 Xcode 26.6 / Apple Clang 21、CMake 4.4.5、Ninja 1.13.2 与 pkgconf 2.5.1。vcpkg 从锁定仓库启动，报告工具版本 `2026-09-26-51bf87ca6`。实际 GPU 为 Apple M4 Pro / Metal。 [第一章实测记录](https://github.com/wondeful18/TidyWorldBook/blob/main/Chapters/ch01/README.md)
