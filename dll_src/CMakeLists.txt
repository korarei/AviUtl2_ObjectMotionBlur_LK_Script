cmake_minimum_required(VERSION 3.20.0)
project(ObjectMotionBlur_LK VERSION 0.2.0 LANGUAGES CXX)

# Main target definition.
add_library(${PROJECT_NAME} SHARED
    main.cpp
    transform_utils.cpp
)

# Def file specification.
set_target_properties(${PROJECT_NAME} PROPERTIES
    LINK_FLAGS "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/main.def"
    PREFIX ""
)

# C++ version specification.
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_23)

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
# Functionality has not been confirmed.
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall
        -Wextra
        -Wpedantic

        # Release-only
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Release>:-ffunction-sections>
        $<$<CONFIG:Release>:-fdata-sections>

        # Debug-only
        $<$<CONFIG:Debug>:-O0>
        $<$<CONFIG:Debug>:-g>
    )

    target_link_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:-Wl,--gc-sections>
        $<$<CONFIG:Debug>:-g>
    )
endif()