cmake_minimum_required(VERSION 3.22.1)
project("zygisk_gltools")

add_library(zygisk_gltools SHARED
    main.cpp
    hooks.cpp
)

target_include_directories(zygisk_gltools PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(zygisk_gltools
    log
    EGL
    GLESv2
    GLESv3
    dl
)
