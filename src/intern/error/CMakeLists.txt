cmake_minimum_required(VERSION 3.20.0)

project(Error LANGUAGES CXX)

add_library(${PROJECT_NAME} OBJECT
    intern/error.cpp
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        "${CMAKE_SOURCE_DIR}"
)

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_23)
