cmake_minimum_required(VERSION 3.30)

project(third_party LANGUAGES C CXX)

message(STATUS "MODULE_ENABLE: ${MODULE_ENABLE}")

include(glm.cmake)
include(glfw.cmake)
include(stb.cmake)
include(tinyobjloader.cmake)
include(tinygltf.cmake)
include(ktx.cmake)