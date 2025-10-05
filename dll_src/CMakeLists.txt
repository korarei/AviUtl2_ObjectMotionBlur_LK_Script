cmake_minimum_required(VERSION 3.20.0)
project(ObjectMotionBlur_LK VERSION 1.0.0 LANGUAGES CXX)

# Path Settings.
set(LUA_DIR "${CMAKE_SOURCE_DIR}/../.lua")
set(LUAJIT_DEF "${CMAKE_SOURCE_DIR}/luajit/luajit.def")
set(LUAJIT_LIB "${CMAKE_CURRENT_BINARY_DIR}/luajit/luajit.lib")

# Generate the luajit.lib
if(MSVC)
    add_custom_command(
        OUTPUT ${LUAJIT_LIB}
        COMMAND lib /DEF:${LUAJIT_DEF} /OUT:${LUAJIT_LIB} /MACHINE:X64
        DEPENDS ${LUAJIT_DEF}
        VERBATIM
    )

    add_custom_target(gen_luajit_lib DEPENDS ${LUAJIT_LIB})
endif()

# Importing Lua Libraries.
add_library(luajit SHARED IMPORTED)
set_target_properties(luajit PROPERTIES
    IMPORTED_IMPLIB "${LUAJIT_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${LUA_DIR}/include/luajit-2.1"
)

# Main target definition.
add_library(${PROJECT_NAME} SHARED
    main.def
    main.cpp
    transform_utils.cpp
    lua_func.cpp
)

# Def file specification.
set_target_properties(${PROJECT_NAME} PROPERTIES
    PREFIX ""
)

# Library Link.
add_dependencies(${PROJECT_NAME} gen_luajit_lib)
target_link_libraries(${PROJECT_NAME} PRIVATE
    luajit
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
endif()
