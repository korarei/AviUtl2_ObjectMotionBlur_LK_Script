function(collect_shader_dependencies SHADER OUT_VARIABLE)
    set(DEPENDENCIES "${SHADER}")
    get_filename_component(SHADER_DIRECTORY "${SHADER}" DIRECTORY)

    file(STRINGS "${SHADER}" INCLUDE_LINES REGEX "^[ \\t]*#[ \\t]*include")
    foreach(INCLUDE_LINE IN LISTS INCLUDE_LINES)
        string(REGEX MATCH "#[ \\t]*include[ \\t]*[\"<]([^\">]+)[\">]" INCLUDE_MATCH "${INCLUDE_LINE}")
        if(NOT INCLUDE_MATCH)
            continue()
        endif()

        get_filename_component(INCLUDE_FILE "${CMAKE_MATCH_1}" ABSOLUTE BASE_DIR "${SHADER_DIRECTORY}")
        if(EXISTS "${INCLUDE_FILE}")
            collect_shader_dependencies("${INCLUDE_FILE}" INCLUDE_DEPENDENCIES)
            list(APPEND DEPENDENCIES ${INCLUDE_DEPENDENCIES})
        endif()
    endforeach()

    list(REMOVE_DUPLICATES DEPENDENCIES)
    set(${OUT_VARIABLE} "${DEPENDENCIES}" PARENT_SCOPE)
endfunction()

function(compile_pixel_shaders TARGET_NAME SHADER_SOURCES)
    # Find fxc.exe for shader compilation.
    find_program(FXC_EXECUTABLE fxc)
    if(NOT FXC_EXECUTABLE)
        message(FATAL_ERROR "fxc.exe not found.")
    endif()

    set(SHADER_DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/shaders/")
    file(MAKE_DIRECTORY "${SHADER_DESTINATION}")

    set(SHADER_HEADERS "")

    # Build shaders.
    foreach(SHADER IN LISTS SHADER_SOURCES)
        get_filename_component(FILE_NAME "${SHADER}" NAME)

        if(FILE_NAME MATCHES "^(.+)\\.hlsl$")
            set(SHADER_NAME "${CMAKE_MATCH_1}")
            set(SHADER_HEADER "${SHADER_DESTINATION}/${SHADER_NAME}.h")
            collect_shader_dependencies("${SHADER}" SHADER_DEPENDENCIES)

            add_custom_command(
                OUTPUT  "${SHADER_HEADER}"
                COMMAND "${FXC_EXECUTABLE}"
                /T "ps_5_0"
                /E main
                /O3
                /WX
                /Qstrip_reflect
                /Qstrip_debug
                /Fh "${SHADER_HEADER}"
                /Vn "g_${SHADER_NAME}"
                "${SHADER}"
                DEPENDS ${SHADER_DEPENDENCIES}
                VERBATIM
            )

            list(APPEND SHADER_HEADERS "${SHADER_HEADER}")
        endif()
    endforeach()

    add_custom_target(${TARGET_NAME}_PixelShaders DEPENDS ${SHADER_HEADERS})
    add_dependencies(${TARGET_NAME} ${TARGET_NAME}_PixelShaders)
    target_include_directories(${TARGET_NAME} PUBLIC "${SHADER_DESTINATION}")
endfunction()

function(compile_vertex_shaders TARGET_NAME SHADER_SOURCES)
    # Find fxc.exe for shader compilation.
    find_program(FXC_EXECUTABLE fxc)
    if(NOT FXC_EXECUTABLE)
        message(FATAL_ERROR "fxc.exe not found.")
    endif()

    set(SHADER_DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/shaders/")
    file(MAKE_DIRECTORY "${SHADER_DESTINATION}")

    set(SHADER_HEADERS "")

    # Build shaders.
    foreach(SHADER IN LISTS SHADER_SOURCES)
        get_filename_component(FILE_NAME "${SHADER}" NAME)

        if(FILE_NAME MATCHES "^(.+)\\.hlsl$")
            set(SHADER_NAME "${CMAKE_MATCH_1}")
            set(SHADER_HEADER "${SHADER_DESTINATION}/${SHADER_NAME}.h")
            collect_shader_dependencies("${SHADER}" SHADER_DEPENDENCIES)

            add_custom_command(
                OUTPUT  "${SHADER_HEADER}"
                COMMAND "${FXC_EXECUTABLE}"
                /T "vs_5_0"
                /E main
                /O3
                /WX
                /Qstrip_reflect
                /Qstrip_debug
                /Fh "${SHADER_HEADER}"
                /Vn "g_${SHADER_NAME}"
                "${SHADER}"
                DEPENDS ${SHADER_DEPENDENCIES}
                VERBATIM
            )

            list(APPEND SHADER_HEADERS "${SHADER_HEADER}")
        endif()
    endforeach()

    add_custom_target(${TARGET_NAME}_VertexShaders DEPENDS ${SHADER_HEADERS})
    add_dependencies(${TARGET_NAME} ${TARGET_NAME}_VertexShaders)
    target_include_directories(${TARGET_NAME} PUBLIC "${SHADER_DESTINATION}")
endfunction()

function(compile_compute_shaders TARGET_NAME SHADER_SOURCES)
    # Find fxc.exe for shader compilation.
    find_program(FXC_EXECUTABLE fxc)
    if(NOT FXC_EXECUTABLE)
        message(FATAL_ERROR "fxc.exe not found.")
    endif()

    set(SHADER_DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/shaders/")
    file(MAKE_DIRECTORY "${SHADER_DESTINATION}")

    set(SHADER_HEADERS "")

    # Build shaders.
    foreach(SHADER IN LISTS SHADER_SOURCES)
        get_filename_component(FILE_NAME "${SHADER}" NAME)

        if(FILE_NAME MATCHES "^(.+)\\.hlsl$")
            set(SHADER_NAME "${CMAKE_MATCH_1}")
            set(SHADER_HEADER "${SHADER_DESTINATION}/${SHADER_NAME}.h")
            collect_shader_dependencies("${SHADER}" SHADER_DEPENDENCIES)

            add_custom_command(
                OUTPUT  "${SHADER_HEADER}"
                COMMAND "${FXC_EXECUTABLE}"
                /T "cs_5_0"
                /E main
                /O3
                /WX
                /Qstrip_reflect
                /Qstrip_debug
                /Fh "${SHADER_HEADER}"
                /Vn "g_${SHADER_NAME}"
                "${SHADER}"
                DEPENDS ${SHADER_DEPENDENCIES}
                VERBATIM
            )

            list(APPEND SHADER_HEADERS "${SHADER_HEADER}")
        endif()
    endforeach()

    add_custom_target(${TARGET_NAME}_ComputeShaders DEPENDS ${SHADER_HEADERS})
    add_dependencies(${TARGET_NAME} ${TARGET_NAME}_ComputeShaders)
    target_include_directories(${TARGET_NAME} PUBLIC "${SHADER_DESTINATION}")
endfunction()
