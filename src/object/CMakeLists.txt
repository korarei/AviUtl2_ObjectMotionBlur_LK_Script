cmake_minimum_required(VERSION 3.20.0)

project(ObjectMotionBlur LANGUAGES CXX)

find_package(Eigen3 CONFIG REQUIRED)

add_library(${PROJECT_NAME} OBJECT
    intern/object.cpp
)

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
