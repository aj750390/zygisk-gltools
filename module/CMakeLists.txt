cmake_minimum_required(VERSION 3.22.1)
project(zygisk_gltools LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(jni)

add_library(zygisk_gltools SHARED
    jni/main.cpp
    jni/hooks.cpp
)

target_link_libraries(zygisk_gltools
    log
    GLESv2
)
