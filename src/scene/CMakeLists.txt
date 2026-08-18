cmake_minimum_required(VERSION 3.20.0)

project(SceneMotionBlur LANGUAGES CXX)

set(NVOF "${CMAKE_SOURCE_DIR}/extern/nvidia_sdk/optical_flow")

add_library(${PROJECT_NAME} OBJECT
    intern/render.cpp
    intern/scene.cpp
    "${NVOF}/NvOF.cpp"
    "${NVOF}/NvOFD3D11.cpp"
    "${NVOF}/NvOFD3DCommon.cpp"
)

include(${CMAKE_SCRIPTS}/CompileShaders.cmake)

set(VERTEX_SHADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/intern/shaders/fullscreen.hlsl"
)

set(PIXEL_SHADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/intern/shaders/convert.hlsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/intern/shaders/decode_flow.hlsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/intern/shaders/propagate_init.hlsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/intern/shaders/propagate.hlsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/intern/shaders/blur.hlsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/intern/shaders/debug.hlsl"
)

set(COMPUTE_SHADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/intern/shaders/regularize_init.hlsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/intern/shaders/regularize_flow.hlsl"
)

compile_vertex_shaders(${PROJECT_NAME} "${VERTEX_SHADERS}")
compile_pixel_shaders(${PROJECT_NAME} "${PIXEL_SHADERS}")
compile_compute_shaders(${PROJECT_NAME} "${COMPUTE_SHADERS}")

target_include_directories(${PROJECT_NAME}
    PUBLIC
        "${CMAKE_SOURCE_DIR}"
    PRIVATE
        "${NVOF}"
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        AviUtl
)

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_23)
