cmake_minimum_required(VERSION 3.24)
set(VCPKG_MANIFEST_DIR "${CMAKE_CURRENT_LIST_DIR}/../step3")
project(FirstTriangle LANGUAGES CXX)

include(../common/DirectStep.cmake)
add_direct_step(first_triangle)
target_sources(first_triangle PRIVATE
    "${tinyworld_root}/Engine/Platform/ExecutablePath.cpp"
    "${tinyworld_root}/Engine/Renderer/ShaderModule.cpp"
    "${tinyworld_root}/Engine/Renderer/TriangleRenderer.cpp"
)

# 每次构建检查 WGSL，修改着色器也会更新可执行文件旁的副本。
add_custom_target(first_triangle_shader
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:first_triangle>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${tinyworld_root}/Assets/Shaders/triangle.wgsl" "$<TARGET_FILE_DIR:first_triangle>/triangle.wgsl"
    VERBATIM
)
add_dependencies(first_triangle first_triangle_shader)
