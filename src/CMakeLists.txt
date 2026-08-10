cmake_minimum_required(VERSION 3.20.0)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(VERSION "0.1.0" CACHE STRING "Project version")
set(REQUIRES_AVIUTL2 "2000100" CACHE STRING "Required AviUtl2 version")

if(NOT REQUIRES_AVIUTL2 MATCHES "^[0-9]+$")
    message(FATAL_ERROR "'REQUIRES_AVIUTL2' must be non-empty numeric string")
endif()

project(MotionBlur_K VERSION ${VERSION} LANGUAGES CXX)

if (NOT WIN32)
    message(FATAL_ERROR "This project is only for Windows")
endif()

if (NOT CMAKE_SIZEOF_VOID_P MATCHES 8)
    message(FATAL_ERROR "This project is only for x64")
endif()

if (NOT CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|amd64|AMD64)")
    message(FATAL_ERROR "This project is only for x64")
endif()

set(CMAKE_SCRIPTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(AviUtl2SDK INTERFACE)
target_include_directories(AviUtl2SDK INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/extern/aviutl2_sdk/include/aviutl2_sdk"
)

if (MSVC)
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS "19.50")
        message(FATAL_ERROR "MSVC version 19.50 or higher is required")
    endif()

    add_compile_options(
        /utf-8
        /arch:AVX2
        /sdl
        /EHsc
        /W4
        /permissive-

        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/GL>
        $<$<CONFIG:Release>:/Gy>

        $<$<CONFIG:Debug>:/Od>
        $<$<CONFIG:Debug>:/Zi>
    )

    add_link_options(
        $<$<CONFIG:Release>:/LTCG>
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>

        $<$<CONFIG:Debug>:/DEBUG>
    )
else()
    message(FATAL_ERROR "This project is only for MSVC")
endif()

add_compile_definitions(
    NOMINMAX
    UNICODE
    _UNICODE
    STRICT
    STRICT_TYPED_ITEMIDS
    WIN32_LEAN_AND_MEAN
    EIGEN_MPL2_ONLY
    VERSION=L"${PROJECT_VERSION}"
    REQUIRES_AVIUTL2=${REQUIRES_AVIUTL2}u

    $<$<CONFIG:Release>:NDEBUG>
)

add_subdirectory(intern/aviutl)

add_subdirectory(object)
add_subdirectory(scene)

include(GenerateExportHeader)

add_library(${PROJECT_NAME} SHARED
    main.cpp
)

generate_export_header(${PROJECT_NAME}
    EXPORT_MACRO_NAME API
    EXPORT_FILE_NAME ${CMAKE_CURRENT_SOURCE_DIR}/api.h
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        AviUtl
        ObjectMotionBlur
        SceneMotionBlur
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    PREFIX ""
    SUFFIX ".aux2"
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
