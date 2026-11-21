cmake_minimum_required(VERSION 3.24)
project(TinyWorld VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
get_property(multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(multi_config)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>")
endif()
if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    set(CMAKE_OBJCXX_EXTENSIONS OFF)
    set(platform_surface Engine/Platform/SurfaceMac.mm)
elseif(WIN32)
    set(platform_surface Engine/Platform/SurfaceWin.cpp)
endif()

include(cmake/Dependencies.cmake)

add_library(TinyWorldEngine STATIC
    Engine/Core/Log.cpp
    Engine/Core/Log.h
    Engine/Platform/ExecutablePath.cpp
    Engine/Platform/ExecutablePath.h
    Engine/Platform/Window.cpp
    Engine/Platform/Window.h
    ${platform_surface}
    Engine/Platform/Surface.h
    Engine/Renderer/Color.h
    Engine/Renderer/RenderPass.cpp
    Engine/Renderer/RenderPass.h
    Engine/Renderer/ShaderModule.cpp
    Engine/Renderer/ShaderModule.h
    Engine/Renderer/TriangleRenderer.cpp
    Engine/Renderer/TriangleRenderer.h
    Engine/Renderer/WebGpuContext.cpp
    Engine/Renderer/WebGpuContext.h
    Engine/Renderer/WgpuHandle.h
    Engine/Renderer/WorldRenderer.cpp
    Engine/Renderer/WorldRenderer.h
    Engine/Scene/OrbitCamera.cpp
    Engine/Scene/OrbitCamera.h
    Engine/Scene/RoamingCar.cpp
    Engine/Scene/RoamingCar.h
    Engine/Scene/RoamingCarMesh.cpp
    Engine/Scene/RoamingCarMesh.h
    Engine/Scene/SceneMesh.cpp
    Engine/Scene/SceneMesh.h
    Engine/Scene/Transform.cpp
    Engine/Scene/Transform.h
    Engine/Scene/WorldScene.cpp
    Engine/Scene/WorldScene.h
)
target_include_directories(TinyWorldEngine PUBLIC "${PROJECT_SOURCE_DIR}")
target_link_libraries(TinyWorldEngine PUBLIC glfw glm::glm-header-only wgpu_native::wgpu_native)
if(APPLE)
    target_link_libraries(TinyWorldEngine PRIVATE "-framework Cocoa" "-framework QuartzCore")
endif()

add_executable(TinyWorld
    Editor/Application.cpp
    Editor/Application.h
    Editor/EditorUi.cpp
    Editor/EditorUi.h
    Editor/FrameCapture.cpp
    Editor/FrameCapture.h
    Editor/SceneViewport.cpp
    Editor/SceneViewport.h
    Editor/SmokeTest.cpp
    Editor/SmokeTest.h
    Editor/WorldEditor.cpp
    Editor/WorldEditor.h
    Editor/main.cpp
    Assets/Shaders/triangle.wgsl
    Assets/Shaders/world.wgsl
)
target_link_libraries(TinyWorld PRIVATE TinyWorldEngine tinyworld_imguizmo)

if(CMAKE_GENERATOR MATCHES "^Visual Studio")
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT TinyWorld)
    set_target_properties(TinyWorld PROPERTIES
        VS_DEBUGGER_COMMAND_ARGUMENTS "--chapter 02"
        VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:TinyWorld>"
    )
    foreach(target TinyWorldEngine TinyWorld)
        get_target_property(sources ${target} SOURCES)
        source_group(TREE "${PROJECT_SOURCE_DIR}" FILES ${sources})
    endforeach()
elseif(CMAKE_GENERATOR STREQUAL "Xcode")
    set_target_properties(TinyWorld PROPERTIES XCODE_GENERATE_SCHEME TRUE)
endif()

add_executable(TinyWorldMeshPacker Tools/AssetTools/PackChapter02.cpp)
target_include_directories(TinyWorldMeshPacker PRIVATE "${PROJECT_SOURCE_DIR}")
target_include_directories(TinyWorldMeshPacker SYSTEM PRIVATE "${CGLTF_INCLUDE_DIR}")
set_target_properties(TinyWorldMeshPacker PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools")

foreach(target TinyWorldEngine TinyWorld TinyWorldMeshPacker)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()

foreach(shader triangle world)
    set(shader_source "${PROJECT_SOURCE_DIR}/Assets/Shaders/${shader}.wgsl")
    set(shader_output "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/${shader}.wgsl")
    add_custom_command(OUTPUT "${shader_output}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${shader_source}" "${shader_output}"
        DEPENDS "${shader_source}"
        VERBATIM
    )
    list(APPEND deployed_assets "${shader_output}")
endforeach()

# GLB 原件留在 Assets，运行程序只读取打包后的小型顶点列表。
set(world_model_dir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/models/ch02")
foreach(model ground tree house)
    set(input "${PROJECT_SOURCE_DIR}/Assets/Models/ch02/${model}.glb")
    set(output "${world_model_dir}/${model}.twmesh")
    add_custom_command(OUTPUT "${output}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${world_model_dir}"
        COMMAND $<TARGET_FILE:TinyWorldMeshPacker> "${input}" "${output}.tmp"
        COMMAND ${CMAKE_COMMAND} -E rename "${output}.tmp" "${output}"
        DEPENDS TinyWorldMeshPacker "${input}"
        VERBATIM
    )
    list(APPEND deployed_assets "${output}")
endforeach()
add_custom_target(TinyWorldAssets DEPENDS ${deployed_assets})
add_dependencies(TinyWorld TinyWorldAssets)

# 将运行时库部署成独立构建产物，删除它后也能单独补齐。
set(wgpu_runtime_output "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${wgpu_runtime}")
add_custom_command(OUTPUT "${wgpu_runtime_output}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:wgpu_native::wgpu_native>" "${wgpu_runtime_output}"
    DEPENDS "$<TARGET_FILE:wgpu_native::wgpu_native>"
    VERBATIM
)
add_custom_target(TinyWorldRuntime DEPENDS "${wgpu_runtime_output}")
add_dependencies(TinyWorld TinyWorldRuntime)
if(APPLE)
    set_target_properties(TinyWorld PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "@loader_path")
endif()
