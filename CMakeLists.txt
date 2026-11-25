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
find_package(Threads REQUIRED)
find_package(lz4 CONFIG REQUIRED)

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
    Engine/Renderer/FrameAtmosphere.h
    Engine/Renderer/GltfMaterials.cpp
    Engine/Renderer/GltfMaterials.h
    Engine/Renderer/RenderCoordinates.cpp
    Engine/Renderer/RenderCoordinates.h
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
    Engine/Renderer/WaterRenderer.cpp
    Engine/Renderer/WaterRenderer.h
    Engine/Renderer/WebGpuContext.cpp
    Engine/Renderer/WebGpuContext.h
    Engine/Renderer/WgpuHandle.h
    Engine/Renderer/PropRenderer.cpp
    Engine/Renderer/PropRenderer.h
    Engine/Renderer/TerrainRenderer.cpp
    Engine/Renderer/TerrainRenderer.h
    Engine/Renderer/TerrainStreaming.cpp
    Engine/Renderer/WorldDraw.cpp
    Engine/Renderer/WorldDraw.h
    Engine/Renderer/WorldRenderer.cpp
    Engine/Renderer/ToneMapping.cpp
    Engine/Renderer/WorldRenderer.h
    Engine/Renderer/WorldVertex.h
    Engine/Scene/AtmosphereSettings.h
    Engine/Scene/GltfAsset.cpp
    Engine/Scene/GltfAsset.h
    Engine/Scene/OrbitCamera.cpp
    Engine/Scene/OrbitCamera.h
    Engine/Scene/Capstone.cpp
    Engine/Scene/Capstone.h
    Engine/Scene/DayCycle.cpp
    Engine/Scene/DayCycle.h
    Engine/Scene/Lightmap.cpp
    Engine/Scene/Lightmap.h
    Engine/Scene/PlacedProp.cpp
    Engine/Scene/PlacedProp.h
    Engine/Scene/PrimitiveMesh.h
    Engine/Scene/RobotMesh.cpp
    Engine/Scene/RobotMesh.h
    Engine/Scene/RoamingCar.cpp
    Engine/Scene/RoamingCar.h
    Engine/Scene/RoamingCarMesh.cpp
    Engine/Scene/RoamingCarMesh.h
    Engine/Scene/SandboxDrive.cpp
    Engine/Scene/SceneFile.cpp
    Engine/Scene/SceneFile.h
    Engine/Scene/SceneMesh.cpp
    Engine/Scene/SceneMesh.h
    Engine/Scene/Transform.cpp
    Engine/Scene/Transform.h
    Engine/Scene/WaterSettings.h
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
    Engine/Terrain/ChunkCache.cpp
    Engine/Terrain/ChunkCache.h
    Engine/Terrain/ChunkFile.cpp
    Engine/Terrain/ChunkFile.h
    Engine/Terrain/WorldChunkPacking.cpp
    Engine/Terrain/WorldChunkPacking.h
    Engine/Terrain/ChunkStreamer.cpp
    Engine/Terrain/ChunkStreamer.h
    Engine/Terrain/TerrainVisibility.cpp
    Engine/Terrain/TerrainVisibility.h
    Engine/Terrain/TerrainMesh.cpp
    Engine/Terrain/TerrainMesh.h
    Engine/Terrain/Vegetation.cpp
    Engine/Terrain/Vegetation.h
    Runtime/WalkController.cpp
    Runtime/WalkController.h
)
target_include_directories(TinyWorldEngine PUBLIC "${PROJECT_SOURCE_DIR}")
target_include_directories(TinyWorldEngine SYSTEM PRIVATE "${Stb_INCLUDE_DIR}" "${CGLTF_INCLUDE_DIR}")
target_link_libraries(TinyWorldEngine
    PUBLIC glfw glm::glm-header-only wgpu_native::wgpu_native
    PRIVATE nlohmann_json::nlohmann_json Threads::Threads lz4::lz4
)
if(APPLE)
    target_link_libraries(TinyWorldEngine PRIVATE "-framework Cocoa" "-framework QuartzCore")
endif()

add_executable(TinyWorld
    Editor/Benchmarks.cpp
    Editor/Benchmarks.h
    Editor/ChapterPresets.cpp
    Editor/ChapterPresets.h
    Editor/InspectorPanels.cpp
    Editor/PropTool.cpp
    Editor/RoadTool.cpp
    Editor/SandboxPresets.cpp
    Editor/SandboxPresets.h
    Editor/ScenePanels.cpp
    Editor/TerrainTool.cpp
    Editor/ViewportTools.cpp
    Editor/ViewportTools.h
    Editor/WorldDemo.h
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
    Engine/Renderer/SandboxRenderer.cpp
    Engine/Renderer/SandboxRenderer.h
    Assets/Shaders/triangle.wgsl
    Assets/Shaders/sky.wgsl
    Assets/Shaders/world.wgsl
    Assets/Shaders/output_color.wgsl
    Assets/Shaders/tone_mapping.wgsl
    Assets/Shaders/shadow.wgsl
    Assets/Shaders/pbr.wgsl
    Assets/Shaders/terrain.wgsl
    Assets/Shaders/prop_gltf.wgsl
    Assets/Shaders/vegetation.wgsl
    Assets/Shaders/water.wgsl
    Assets/Shaders/sandbox.wgsl
    Assets/Shaders/fog.wgsl
)
target_link_libraries(TinyWorld PRIVATE TinyWorldEngine tinyworld_imguizmo)

if(CMAKE_GENERATOR MATCHES "^Visual Studio")
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT TinyWorld)
    set_target_properties(TinyWorld PROPERTIES
        VS_DEBUGGER_COMMAND_ARGUMENTS "--chapter 20"
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

foreach(shader triangle world sky water fog output_color tone_mapping shadow pbr terrain prop_gltf vegetation sandbox)
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
    scenes/ch13/village.json
    scenes/ch15/coast.json
    scenes/ch17/porch.json
    scenes/ch21/tour.json
    models/ch17/porch.glb
    models/ch13/house.glb models/ch13/fence.glb models/ch13/bridge.glb
    materials/ch08/grass-color.png materials/ch08/grass-rough.png
    materials/ch08/gravel-color.png materials/ch08/gravel-rough.png
    materials/ch08/rock-color.png materials/ch08/rock-rough.png
    materials/ch08/sand-color.png materials/ch08/sand-rough.png
)
foreach(asset IN LISTS sample_assets)
    string(REGEX REPLACE "^scenes/" "Scenes/" source_asset "${asset}")
    string(REGEX REPLACE "^materials/" "Materials/" source_asset "${source_asset}")
    string(REGEX REPLACE "^models/" "Models/" source_asset "${source_asset}")
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
# 烘焙器走同一个 glTF 导入路径，模型变动时自动重建线性图集。
add_executable(TinyWorldLightmapBaker Tools/LightmapBaker/main.cpp Tools/LightmapBaker/Bake.cpp)
target_link_libraries(TinyWorldLightmapBaker PRIVATE TinyWorldEngine)
target_compile_options(TinyWorldLightmapBaker PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
set(porch_model "${PROJECT_SOURCE_DIR}/Assets/Models/ch17/porch.glb")
set(lightmap_dir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/lightmaps/ch17")
set(lightmap_output "${lightmap_dir}/porch.twlm")
add_custom_command(OUTPUT "${lightmap_output}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${lightmap_dir}"
    COMMAND $<TARGET_FILE:TinyWorldLightmapBaker> "${porch_model}" "${lightmap_output}.tmp"
    COMMAND ${CMAKE_COMMAND} -E rename "${lightmap_output}.tmp" "${lightmap_output}"
    DEPENDS TinyWorldLightmapBaker "${porch_model}"
    VERBATIM
)
list(APPEND deployed_assets "${lightmap_output}")
add_executable(WorldChunker Tools/WorldChunker/main.cpp)
target_link_libraries(WorldChunker PRIVATE TinyWorldEngine)
target_compile_options(WorldChunker PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

# 构建时从同一份教学素材得到两个可独立打开的世界目录。
foreach(world valley exploration)
    if(world STREQUAL "exploration")
        set(input "${PROJECT_SOURCE_DIR}/Assets/Scenes/ch21/tour.json")
        set(option --exploration)
        set(keys -16 -15 -14 -13 -12 -11 -10 -9 -8 -7 -6 -5 -4 -3 -2 -1
            0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15)
    else()
        set(input "${PROJECT_SOURCE_DIR}/Assets/Scenes/ch17/porch.json")
        unset(option)
        set(keys -3 -2 -1 0 1 2)
    endif()
    set(folder "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/worlds/${world}")
    set(output "${folder}/manifest.json")
    set(chunk_files)
    foreach(z IN LISTS keys)
        foreach(x IN LISTS keys)
            foreach(axis x z)
                if(${axis} LESS 0)
                    set(sign "-")
                else()
                    set(sign "+")
                endif()
                if(sign STREQUAL "-")
                    math(EXPR magnitude "-(${${axis}})")
                else()
                    set(magnitude "${${axis}}")
                endif()
                if(magnitude LESS 10)
                    set(magnitude "0${magnitude}")
                endif()
                set(key_${axis} "${sign}${magnitude}")
            endforeach()
            list(APPEND chunk_files "${folder}/chunks/c_${key_x}_${key_z}.twchunk")
        endforeach()
    endforeach()
    add_custom_command(OUTPUT "${output}" ${chunk_files}
        COMMAND $<TARGET_FILE:WorldChunker> "${input}" "${folder}" ${option}
        DEPENDS WorldChunker "${input}" COMMENT "Packing ${world} world chunks" VERBATIM)
    list(APPEND deployed_assets "${output}" ${chunk_files})
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
add_dependencies(TinyWorldLightmapBaker TinyWorldRuntime)
add_dependencies(WorldChunker TinyWorldRuntime)
if(APPLE)
    set_target_properties(TinyWorld TinyWorldLightmapBaker WorldChunker PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "@loader_path")
endif()

include(CTest)
if(BUILD_TESTING)
    foreach(check Village Coast Lightmap DayCycle TerrainVisibility TerrainStreaming Capstone WorldPackage)
        set(check_target TinyWorld${check}Checks)
        add_executable(${check_target} Tests/${check}Checks.cpp)
        target_link_libraries(${check_target} PRIVATE TinyWorldEngine)
        target_compile_options(${check_target} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
        add_dependencies(${check_target} TinyWorldAssets TinyWorldRuntime)
        if(APPLE)
            set_target_properties(${check_target} PROPERTIES
                BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "@loader_path")
        endif()
    endforeach()
    add_test(NAME VillageChecks
        COMMAND TinyWorldVillageChecks "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}" "${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME WorldPackageChecks
        COMMAND TinyWorldWorldPackageChecks "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}" "${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME CapstoneChecks
        COMMAND TinyWorldCapstoneChecks "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}" "${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME TerrainStreamingChecks COMMAND TinyWorldTerrainStreamingChecks)
    add_test(NAME TerrainVisibilityChecks COMMAND TinyWorldTerrainVisibilityChecks)
    add_test(NAME CoastChecks COMMAND TinyWorldCoastChecks "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    target_sources(TinyWorldLightmapChecks PRIVATE Tools/LightmapBaker/Bake.cpp)
    add_test(NAME LightmapChecks
        COMMAND TinyWorldLightmapChecks "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}" "${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME DayCycleChecks
        COMMAND TinyWorldDayCycleChecks "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}" "${CMAKE_CURRENT_BINARY_DIR}")
endif()
