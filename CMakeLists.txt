cmake_minimum_required(VERSION 3.16)

project(diagnose_basic)

if(LINUX)
    find_package(OpenGL REQUIRED)
endif()

add_subdirectory(src)
