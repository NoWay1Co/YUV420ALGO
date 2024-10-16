cmake_minimum_required (VERSION 3.8)

set(CMAKE_CXX_COMPILER "clang")

project ("YUV420Overlay")

add_executable (YUV420Overlay "main.cpp")

set_property(TARGET YUV420Overlay PROPERTY CXX_STANDARD 11)
