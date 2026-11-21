get_filename_component(tinyworld_root "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
include("${tinyworld_root}/cmake/WgpuNative.cmake")
find_package(glfw3 3.5.1 EXACT CONFIG REQUIRED)

if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    set(platform_surface "${tinyworld_root}/Engine/Platform/SurfaceMac.mm")
else()
    set(platform_surface "${tinyworld_root}/Engine/Platform/SurfaceWin.cpp")
endif()

# 两步共用窗口、Surface 和帧循环。主程序各自决定这一帧画什么。
function(add_direct_step target)
    add_executable(${target}
        main.cpp
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/DirectFrame.cpp"
        "${tinyworld_root}/Engine/Core/Log.cpp"
        "${tinyworld_root}/Engine/Platform/Window.cpp"
        "${platform_surface}"
        "${tinyworld_root}/Engine/Renderer/RenderPass.cpp"
        "${tinyworld_root}/Engine/Renderer/WebGpuContext.cpp"
    )
    target_include_directories(${target} PRIVATE "${tinyworld_root}" "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_link_libraries(${target} PRIVATE glfw wgpu_native::wgpu_native)
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8)
    endif()
    if(APPLE)
        target_link_libraries(${target} PRIVATE "-framework Cocoa" "-framework QuartzCore")
        set_target_properties(${target} PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "@loader_path")
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:wgpu_native::wgpu_native>" "$<TARGET_FILE_DIR:${target}>/${wgpu_runtime}"
        VERBATIM
    )
endfunction()
