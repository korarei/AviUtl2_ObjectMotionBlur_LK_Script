cmake_minimum_required(VERSION 3.20.0)

project(ObjectMotionBlur LANGUAGES CXX)

find_package(Eigen3 CONFIG REQUIRED)

add_library(${PROJECT_NAME} OBJECT
    intern/object.cpp
    intern/render.cpp
)

include(${CMAKE_SCRIPTS}/CompileShaders.cmake)

set(VERTEX_SHADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/intern/shaders/fullscreen.hlsl"
)

set(PIXEL_SHADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/intern/shaders/blur.hlsl"
)

compile_vertex_shaders(${PROJECT_NAME} "${VERTEX_SHADERS}")
compile_pixel_shaders(${PROJECT_NAME} "${PIXEL_SHADERS}")


target_include_directories(${PROJECT_NAME}
    PUBLIC
        "${CMAKE_SOURCE_DIR}"
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        Eigen3::Eigen
        AviUtl
)

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_23)
