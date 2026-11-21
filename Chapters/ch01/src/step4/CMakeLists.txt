cmake_minimum_required(VERSION 3.24)
# 沿用上一步的 GLFW 清单，额外依赖由 DirectStep.cmake 管理。
set(VCPKG_MANIFEST_DIR "${CMAKE_CURRENT_LIST_DIR}/../step3")
project(FirstClear LANGUAGES CXX)

include(../common/DirectStep.cmake)
add_direct_step(first_clear)
