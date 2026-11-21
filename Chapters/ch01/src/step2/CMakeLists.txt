cmake_minimum_required(VERSION 3.24)
project(HelloWorld LANGUAGES CXX)

add_executable(hello main.cpp)
target_compile_features(hello PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(hello PRIVATE /utf-8)
endif()
