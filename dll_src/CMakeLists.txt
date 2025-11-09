cmake_minimum_required(VERSION 3.20.0)

set(VERSION "0.1.0" CACHE STRING "Project version")
project(ObjectMotionBlur_LK VERSION ${VERSION} LANGUAGES CXX)

# Path settings.
set(SDK_DIR "${CMAKE_SOURCE_DIR}/aviutl2_sdk")

# Main target definition.
add_library(${PROJECT_NAME} SHARED
    main.def
    main.cpp
    transform_utils.cpp
)

# Include directories.
target_include_directories(${PROJECT_NAME} PRIVATE
    ${SDK_DIR}
)

# Output settings.
set_target_properties(${PROJECT_NAME} PROPERTIES
    PREFIX ""
    SUFFIX ".mod2"
)

# C++ version specification.
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_23)

# Definitions.
target_compile_definitions(${PROJECT_NAME} PRIVATE
    VERSION=L"${VERSION}"
)

# Compiler Dependent Options.
if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        /source-charset:utf-8
        /execution-charset:utf-8
        /arch:AVX2
        /W4 # Warning Level 4.
        /permissive- # Standards-based mode.

        # Release-only optimizations.
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/GL> # Link Optimization.
        $<$<CONFIG:Release>:/Gy> # Split by function. (Remove unused functions.)

        # Debug settings.
        $<$<CONFIG:Debug>:/Od>
        $<$<CONFIG:Debug>:/Zi> # Debugging Information.
    )

    target_link_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:/LTCG> # Runtime Optimization.
        $<$<CONFIG:Debug>:/DEBUG> # Link with debug information.
    )

    # Runtime library switching.
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
endif()
