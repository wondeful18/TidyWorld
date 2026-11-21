include("${CMAKE_CURRENT_LIST_DIR}/WgpuNative.cmake")
find_package(glfw3 3.5.1 EXACT CONFIG REQUIRED)
find_package(glm 1.0.3 EXACT CONFIG REQUIRED)
find_path(CGLTF_INCLUDE_DIR cgltf.h REQUIRED)

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
FetchContent_MakeAvailable(dear_imgui imguizmo)

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
