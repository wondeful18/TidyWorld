include(FetchContent)

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
    message(FATAL_ERROR "wgpu-native requires macOS arm64 or Windows x64 MSVC.")
endif()

# 头文件和库必须来自同一份发行包，避免 C API ABI 不匹配。
FetchContent_Declare(wgpu_native
    URL "https://github.com/gfx-rs/wgpu-native/releases/download/${wgpu_version}/${wgpu_package}"
    URL_HASH "SHA256=${wgpu_sha}"
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
)
FetchContent_MakeAvailable(wgpu_native)

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
