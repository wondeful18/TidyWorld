include(FetchContent)
find_package(glfw3 3.5.1 EXACT CONFIG REQUIRED)
find_package(glm 1.0.3 EXACT CONFIG REQUIRED)
find_path(CGLTF_INCLUDE_DIR cgltf.h REQUIRED)

set(wgpu_version v29.0.1.1)
if(APPLE AND CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
    set(wgpu_package wgpu-macos-aarch64-release.zip)
    set(wgpu_sha a5797a37b1adf720bcd5dcffb291edbbd5b7b14be0a3874c28e6393a655a7a3e)
    set(wgpu_runtime libwgpu_native.dylib)
elseif(WIN32 AND MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
    set(wgpu_package wgpu-windows-x86_64-msvc-release.zip)
    set(wgpu_sha 7e67d7445c42aeb85e30f88930fd8d7d83ee769e3390aeb1ada75ebf3cf78132)
    set(wgpu_runtime wgpu_native.dll)
else()
    message(FATAL_ERROR "TinyWorld requires the macOS arm64 or Windows x64 MSVC preset.")
endif()

# 头文件和库必须来自同一份发行包，避免 C API ABI 不匹配。
FetchContent_Declare(wgpu_native
    URL "https://github.com/gfx-rs/wgpu-native/releases/download/${wgpu_version}/${wgpu_package}"
    URL_HASH "SHA256=${wgpu_sha}"
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
)
FetchContent_Declare(dear_imgui
    URL "https://codeload.github.com/ocornut/imgui/zip/b916186e66aa3c02b72c6ca17455516512ed8049"
    URL_HASH "SHA256=bce023ef1cdd7af5217c76da5a54c9a1f30ea309f06a0cd113e1cefcb05587fa"
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
)
FetchContent_Declare(imguizmo
    URL "https://codeload.github.com/CedricGuillemet/ImGuizmo/zip/105e4fde45e4e74aae8973d98750820ef7aca7e2"
    URL_HASH "SHA256=754fca4e8c66c5a255f953239b5181385228923f646d1a39bdca5ba18acc7cfd"
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    SOURCE_SUBDIR tinyworld-source-only
)
FetchContent_MakeAvailable(wgpu_native dear_imgui imguizmo)

add_library(wgpu_native::wgpu_native SHARED IMPORTED GLOBAL)
set_target_properties(wgpu_native::wgpu_native PROPERTIES
    IMPORTED_LOCATION "${wgpu_native_SOURCE_DIR}/lib/${wgpu_runtime}"
    INTERFACE_INCLUDE_DIRECTORIES "${wgpu_native_SOURCE_DIR}/include"
)
if(WIN32)
    set_target_properties(wgpu_native::wgpu_native PROPERTIES
        IMPORTED_IMPLIB "${wgpu_native_SOURCE_DIR}/lib/wgpu_native.dll.lib"
    )
endif()

set(imgui_wgpu_source "${dear_imgui_SOURCE_DIR}/backends/imgui_impl_wgpu.cpp")
add_library(tinyworld_imgui STATIC
    "${dear_imgui_SOURCE_DIR}/imgui.cpp"
    "${dear_imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${dear_imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${dear_imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${dear_imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
    "${imgui_wgpu_source}"
)
target_include_directories(tinyworld_imgui SYSTEM PUBLIC
    "${dear_imgui_SOURCE_DIR}"
    "${dear_imgui_SOURCE_DIR}/backends"
)
target_compile_definitions(tinyworld_imgui PUBLIC IMGUI_IMPL_WEBGPU_BACKEND_WGPU)
target_link_libraries(tinyworld_imgui PUBLIC glfw wgpu_native::wgpu_native)
if(APPLE)
    # 官方后端的窗口辅助函数引用 Cocoa，单独以 Objective-C++ 编译。
    set_source_files_properties("${imgui_wgpu_source}" PROPERTIES LANGUAGE OBJCXX)
    target_link_libraries(tinyworld_imgui PRIVATE "-framework Cocoa" "-framework QuartzCore")
endif()

# 与同一个 ImGui Docking 源码目标链接，避免同进程混用两份 ImGui。
add_library(tinyworld_imguizmo STATIC "${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp")
target_include_directories(tinyworld_imguizmo SYSTEM PUBLIC "${imguizmo_SOURCE_DIR}/src")
target_link_libraries(tinyworld_imguizmo PUBLIC tinyworld_imgui)
