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
    Engine/Renderer/EnvironmentMap.cpp
    Engine/Renderer/EnvironmentMap.h
    Engine/Renderer/RenderPass.cpp
    Engine/Renderer/RenderPass.h
    Engine/Renderer/ShaderModule.cpp
    Engine/Renderer/ShaderModule.h
    Engine/Renderer/SkyRenderer.cpp
    Engine/Renderer/SkyRenderer.h
    Engine/Renderer/SurfaceMaterials.cpp
    Engine/Renderer/SurfaceMaterials.h
    Engine/Renderer/SunShadowMap.cpp
    Engine/Renderer/SunShadowMap.h
    Engine/Renderer/TriangleRenderer.cpp
    Engine/Renderer/TriangleRenderer.h
    Engine/Renderer/VegetationRenderer.cpp
    Engine/Renderer/VegetationRenderer.h
    Engine/Renderer/WebGpuContext.cpp
    Engine/Renderer/WebGpuContext.h
    Engine/Renderer/WgpuHandle.h
    Engine/Renderer/WorldRenderer.cpp
    Engine/Renderer/WorldRenderer.h
    Engine/Renderer/WorldVertex.h
    Engine/Scene/OrbitCamera.cpp
    Engine/Scene/OrbitCamera.h
    Engine/Scene/PlacedProp.cpp
    Engine/Scene/PlacedProp.h
    Engine/Scene/PrimitiveMesh.h
    Engine/Scene/RobotMesh.cpp
    Engine/Scene/RobotMesh.h
    Engine/Scene/RoamingCar.cpp
    Engine/Scene/RoamingCar.h
    Engine/Scene/RoamingCarMesh.cpp
    Engine/Scene/RoamingCarMesh.h
    Engine/Scene/SceneMesh.cpp
    Engine/Scene/SceneMesh.h
    Engine/Scene/SceneFile.cpp
    Engine/Scene/SceneFile.h
    Engine/Scene/Transform.cpp
    Engine/Scene/Transform.h
    Engine/Scene/WorldScene.cpp
    Engine/Scene/WorldScene.h
    Engine/Scene/WalkerMesh.cpp
    Engine/Scene/WalkerMesh.h
    Engine/Terrain/HeightField.cpp
    Engine/Terrain/HeightField.h
    Engine/Terrain/MaterialSplat.cpp
    Engine/Terrain/MaterialSplat.h
    Engine/Terrain/RoadSpline.cpp
    Engine/Terrain/RoadSpline.h
    Engine/Terrain/TerrainMesh.cpp
    Engine/Terrain/TerrainMesh.h
    Engine/Terrain/Vegetation.cpp
    Engine/Terrain/Vegetation.h
    Runtime/WalkController.cpp
    Runtime/WalkController.h
)
target_include_directories(TinyWorldEngine PUBLIC "${PROJECT_SOURCE_DIR}")
target_include_directories(TinyWorldEngine SYSTEM PRIVATE "${Stb_INCLUDE_DIR}")
target_link_libraries(TinyWorldEngine
    PUBLIC glfw glm::glm-header-only wgpu_native::wgpu_native
    PRIVATE nlohmann_json::nlohmann_json
)
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
    Assets/Shaders/sky.wgsl
    Assets/Shaders/world.wgsl
)
target_link_libraries(TinyWorld PRIVATE TinyWorldEngine tinyworld_imguizmo)

if(CMAKE_GENERATOR MATCHES "^Visual Studio")
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT TinyWorld)
    set_target_properties(TinyWorld PROPERTIES
        VS_DEBUGGER_COMMAND_ARGUMENTS "--chapter 11"
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

foreach(shader triangle world sky)
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

# 第四章将全景原图部署到可执行文件旁，采样时不需要联网。
set(panorama_source "${PROJECT_SOURCE_DIR}/Assets/Environments/ch04/golden-gate-hills.jpg")
set(panorama_dir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/environments/ch04")
set(panorama_output "${panorama_dir}/golden-gate-hills.jpg")
add_custom_command(OUTPUT "${panorama_output}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${panorama_dir}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${panorama_source}" "${panorama_output}"
    DEPENDS "${panorama_source}"
    VERBATIM
)
list(APPEND deployed_assets "${panorama_output}")

# 第八章和后续章节从第七章的同一份山谷开始，并部署原创建材贴图。
set(sample_assets
    scenes/ch07/valley.json
    materials/ch08/grass-color.png materials/ch08/grass-rough.png
    materials/ch08/gravel-color.png materials/ch08/gravel-rough.png
    materials/ch08/rock-color.png materials/ch08/rock-rough.png
    materials/ch08/sand-color.png materials/ch08/sand-rough.png
)
foreach(asset IN LISTS sample_assets)
    string(REGEX REPLACE "^scenes/" "Scenes/" source_asset "${asset}")
    string(REGEX REPLACE "^materials/" "Materials/" source_asset "${source_asset}")
    set(input "${PROJECT_SOURCE_DIR}/Assets/${source_asset}")
    set(output "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${asset}")
    get_filename_component(asset_dir "${output}" DIRECTORY)
    add_custom_command(OUTPUT "${output}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${asset_dir}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${input}" "${output}"
        DEPENDS "${input}"
        VERBATIM
    )
    list(APPEND deployed_assets "${output}")
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
